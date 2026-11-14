/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Job.h"
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QProcess>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include "Lib/Core/Core.h"
#include "Lib/Core/Help.h"
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// The content shown by the views has the values of the library
static_assert((int)gui_field::Type_Text == (int)metadata_field::Text
           && (int)gui_field::Type_Unsigned == (int)metadata_field::Unsigned
           && (int)gui_field::Type_Float == (int)metadata_field::Float
           && (int)gui_field::Type_TimeCode == (int)metadata_field::TimeCode, "gui_field::type is not metadata_field::type");
static_assert((int)gui_field::Kind_Same == (int)metadata::kind::Same
           && (int)gui_field::Kind_Sequence == (int)metadata::kind::Sequence
           && (int)gui_field::Kind_Mixed == (int)metadata::kind::Mixed, "gui_field::kind is not metadata::kind");

//---------------------------------------------------------------------------
// The external program is given up after this time (ms) when only its output is wanted
static const int Process_Timeout = 30000;

//***************************************************************************
// Strings
//***************************************************************************

//---------------------------------------------------------------------------
// Strings of the library are in the local 8-bit encoding, as the arguments of a command line
static QString FromLocal(const std::string& Text)
{
    return QString::fromLocal8Bit(Text.data(), (int)Text.size());
}

//---------------------------------------------------------------------------
static QString FromLocal(const char* Text)
{
    return Text ? QString::fromLocal8Bit(Text) : QString();
}

//---------------------------------------------------------------------------
static std::string ToLocal(const QString& Text)
{
    const QByteArray Bytes = Text.toLocal8Bit();
    return std::string(Bytes.constData(), (size_t)Bytes.size());
}

//---------------------------------------------------------------------------
// Text of the encoder: FFmpeg writes UTF-8, another program is expected to use the local 8-bit encoding
static QString FromEncoder(const std::string& Text)
{
    QString Result = QString::fromUtf8(Text.data(), (int)Text.size());
    if (Result.contains(QChar(QChar::ReplacementCharacter)))
        return FromLocal(Text);
    return Result;
}

//---------------------------------------------------------------------------
// Name of a file without its directory
static QString FileName_Short(const std::string& FileName)
{
    const QString Result = QDir::fromNativeSeparators(FromLocal(FileName));
    return Result.mid(Result.lastIndexOf(QLatin1Char('/')) + 1);
}

//---------------------------------------------------------------------------
// Argument as it would be typed on a command line
static QString Argument_Quoted(const QString& Argument)
{
    if (!Argument.isEmpty() && !Argument.contains(QLatin1Char(' ')) && !Argument.contains(QLatin1Char('\t')) && !Argument.contains(QLatin1Char('"')))
        return Argument;
    QString Result = Argument;
    Result.replace(QLatin1Char('"'), QLatin1String("\\\""));
    Result.prepend(QLatin1Char('"'));
    Result.append(QLatin1Char('"'));
    return Result;
}

//---------------------------------------------------------------------------
// Option whose value is a license key
static bool Argument_IsKeyOption(const QString& Argument)
{
    return Argument == QLatin1String("--license") || Argument == QLatin1String("--licence")
        || Argument == QLatin1String("--store-license") || Argument == QLatin1String("--store-licence");
}

//---------------------------------------------------------------------------
// Frame rate as the library provides it (e.g. 24.000000), without the zeros which say nothing
static QString FrameRate_Clean(const std::string& Value)
{
    QString Result = FromLocal(Value);
    const auto Dot = Result.indexOf(QLatin1Char('.'));
    if (Dot < 0)
        return Result;
    for (auto i = Dot + 1; i < Result.size(); i++)
    {
        const auto Char = Result.at(i).unicode();
        if (Char < '0' || Char > '9')
            return Result; // Not a decimal number (e.g. a ratio given by the user), kept as is
    }
    while (Result.endsWith(QLatin1Char('0')))
        Result.chop(1);
    if (Result.endsWith(QLatin1Char('.')))
        Result.chop(1);
    return Result;
}

//***************************************************************************
// Standard streams of the library
//***************************************************************************

//---------------------------------------------------------------------------
enum console_stream
{
    Stream_Out,
    Stream_Err,
    Stream_Encoder,         // Output of the external program, read by the job
};

//---------------------------------------------------------------------------
// Text written to a stream by a thread since its last line start
struct console_line
{
    std::string                 Text;
    bool                        CR_Pending = false;     // The last character is a carriage return: end of a progress line, or of a line if a line feed follows
    bool                        CR_Start = false;       // The text follows a carriage return
    std::chrono::steady_clock::time_point Time;         // Of the last text
};

//---------------------------------------------------------------------------
// A progress line of the library has no end before the next one, and it is written in several times: it is complete when nothing was added for this time
static const std::chrono::milliseconds Progress_Delay(20);

//---------------------------------------------------------------------------
// Progress line of the encoder (FFmpeg), also when it ends as a line (last one)
static bool Encoder_IsProgress(const std::string& Text)
{
    const size_t Pos = Text.find_first_not_of(' ');
    return Pos != std::string::npos && (!Text.compare(Pos, 6, "frame=") || !Text.compare(Pos, 5, "size="));
}

//---------------------------------------------------------------------------
// Position in the content written by the encoder (HH:MM:SS.cc) in hundredths of a second, false if there is none (e.g. N/A)
static bool Encoder_Time(const char* Text, quint64& Value)
{
    quint64 Parts[3] = {};
    for (int i = 0; i < 3; i++)
    {
        if (*Text < '0' || *Text > '9')
            return false;
        while (*Text >= '0' && *Text <= '9')
            Parts[i] = Parts[i] * 10 + (quint64)(*Text++ - '0');
        if (i < 2 && *Text++ != ':')
            return false;
    }
    Value = ((Parts[0] * 60 + Parts[1]) * 60 + Parts[2]) * 100;
    if (*Text == '.')
    {
        Text++;
        if (*Text >= '0' && *Text <= '9')
        {
            Value += (quint64)(*Text++ - '0') * 10;
            if (*Text >= '0' && *Text <= '9')
                Value += (quint64)(*Text - '0');
        }
    }
    return true;
}

//---------------------------------------------------------------------------
// What a job gets from the streams. Everything is used under the mutex of the console
struct console_target
{
    std::map<std::thread::id, console_line> Lines[Stream_Encoder]; // Standard streams, by thread: the library writes a line in several times and from several threads, the text of a thread is not cut by the text of another one
    std::thread::id             Progress_Thread;        // Thread of the last carriage return on the standard error stream
    console_line                Encoder_Line;
    QString                     Out;
    QString                     Err;
    QString                     Log;                    // Lines not yet taken
    QString                     Progress;               // Last progress line
    bool                        Out_IsLogged = true;

    // Encoder
    bool                        Encoder_IsRunning = false;
    bool                        Encoder_IsTime = false; // Progress from the position in the content instead of the count of frames
    bool                        Encoder_HasCurrent = false; // The encoder may write no progress line (log level), its progress is then unknown
    quint64                     Encoder_Current = 0;
    quint64                     Encoder_Total = 0;      // 0 if unknown

    void                        Add(console_stream Stream, const char* Text, size_t Size);
    void                        Finish(console_stream Stream); // No more text
    void                        Message_Add(const QString& Text); // Message of the job itself, as if the library had written it
    void                        Encoder_Update();
    QString                     Progress_Text() const;

private:
    void                        Line_End(console_stream Stream, console_line& Line, bool IsProgress);
    void                        Line_Finish(console_stream Stream, console_line& Line);
    void                        Encoder_Progress(const std::string& Text);
};

//---------------------------------------------------------------------------
void console_target::Add(console_stream Stream, const char* Text, size_t Size)
{
    const bool IsEncoder = Stream == Stream_Encoder;
    console_line& Line = IsEncoder ? Encoder_Line : Lines[Stream][std::this_thread::get_id()];
    if (!IsEncoder)
        Line.Time = std::chrono::steady_clock::now();
    for (size_t i = 0; i < Size; i++)
    {
        const char Char = Text[i];
        if (Line.CR_Pending)
        {
            Line.CR_Pending = false;
            if (Char == '\n')
            {
                Line_End(Stream, Line, false); // CR LF is one line end
                continue;
            }
            Line_End(Stream, Line, true);
            Line.CR_Start = true;
        }
        if (Char == '\r')
        {
            Line.CR_Pending = true;
            if (Stream == Stream_Err)
                Progress_Thread = std::this_thread::get_id();
        }
        else if (Char == '\n')
            Line_End(Stream, Line, false);
        else
            Line.Text += Char;
    }
}

//---------------------------------------------------------------------------
void console_target::Finish(console_stream Stream)
{
    if (Stream == Stream_Encoder)
    {
        Line_Finish(Stream, Encoder_Line);
        return;
    }
    for (auto& Line : Lines[Stream])
        Line_Finish(Stream, Line.second);
    Lines[Stream].clear();
}

//---------------------------------------------------------------------------
void console_target::Line_Finish(console_stream Stream, console_line& Line)
{
    // A last line without line end is a line as the others, a text after a carriage return is a progress line which was not replaced
    if (!Line.CR_Pending && !Line.CR_Start && Line.Text.find_first_not_of(' ') != std::string::npos)
        Line_End(Stream, Line, false);
    Line = console_line();
}

//---------------------------------------------------------------------------
void console_target::Message_Add(const QString& Text)
{
    Err += Text;
    Err += QLatin1Char('\n');
    Log += Text;
    Log += QLatin1Char('\n');
}

//---------------------------------------------------------------------------
void console_target::Line_End(console_stream Stream, console_line& Line, bool IsProgress)
{
    const bool IsEncoder = Stream == Stream_Encoder;
    if (IsEncoder && !IsProgress)
        IsProgress = Encoder_IsProgress(Line.Text);
    if (IsEncoder && IsProgress)
        Encoder_Progress(Line.Text);
    QString Text = IsEncoder ? FromEncoder(Line.Text) : FromLocal(Line.Text);
    const bool CR_Start = Line.CR_Start;
    Line.Text.clear();
    Line.CR_Start = false;

    // Progress line: shown instead of the previous one (nothing more is shown if it only erases it), not logged
    if (IsProgress)
    {
        Progress = Text.trimmed();
        return;
    }

    // Line
    if (CR_Start)
        while (Text.endsWith(QLatin1Char(' ')))
            Text.chop(1); // Spaces erasing the end of the progress line which was there
    Text += QLatin1Char('\n');
    if (Stream == Stream_Out)
    {
        Out += Text;
        if (Out_IsLogged)
            Log += Text;
    }
    else
    {
        Err += Text;
        Log += Text;
    }
}

//---------------------------------------------------------------------------
void console_target::Encoder_Progress(const std::string& Text)
{
    if (Encoder_IsTime)
    {
        // size=... time=00:00:01.23 ...
        const size_t Pos = Text.find("time=");
        quint64 Value;
        if (Pos != std::string::npos && Encoder_Time(Text.c_str() + Pos + 5, Value))
        {
            Encoder_Current = Value;
            Encoder_HasCurrent = true;
        }
        return;
    }

    // frame=  123 ...
    const size_t Pos = Text.find("frame=");
    if (Pos == std::string::npos)
        return;
    const char* Begin = Text.c_str() + Pos + 6;
    while (*Begin == ' ')
        Begin++;
    if (*Begin < '0' || *Begin > '9')
        return;
    quint64 Value = 0;
    while (*Begin >= '0' && *Begin <= '9')
        Value = Value * 10 + (quint64)(*Begin++ - '0');
    Encoder_Current = Value;
    Encoder_HasCurrent = true;
}

//---------------------------------------------------------------------------
void console_target::Encoder_Update()
{
    // A progress line of the encoder is complete with its carriage return, it is used without waiting for what follows it
    if (!Encoder_Line.CR_Pending || !Encoder_IsProgress(Encoder_Line.Text))
        return;
    Encoder_Progress(Encoder_Line.Text);
    Progress = FromEncoder(Encoder_Line.Text).trimmed();
}

//---------------------------------------------------------------------------
QString console_target::Progress_Text() const
{
    if (Encoder_IsRunning)
        return Progress;

    // The library writes the carriage return before a progress line: the line its thread wrote is the current one
    const auto Line = Lines[Stream_Err].find(Progress_Thread);
    if (Line != Lines[Stream_Err].end()
     && (Line->second.CR_Pending || (Line->second.CR_Start && !Line->second.Text.empty() && std::chrono::steady_clock::now() - Line->second.Time >= Progress_Delay)))
        return FromLocal(Line->second.Text).trimmed();
    return Progress;
}

//---------------------------------------------------------------------------
// Stream buffer giving what the library writes, without keeping it: the library writes from several threads
struct console_state;
static void Console_Add(console_state* Console, console_stream Stream, const char* Text, size_t Size);
class console_buffer : public std::streambuf
{
public:
    console_buffer(console_state* Console_, console_stream Stream_) : Console(Console_), Stream(Stream_) {}

protected:
    std::streamsize xsputn(const char* Text, std::streamsize Size) override
    {
        if (Size > 0)
            Console_Add(Console, Stream, Text, (size_t)Size);
        return Size;
    }
    int_type overflow(int_type Value) override
    {
        if (!traits_type::eq_int_type(Value, traits_type::eof()))
        {
            const char Char = traits_type::to_char_type(Value);
            Console_Add(Console, Stream, &Char, 1);
        }
        return traits_type::not_eof(Value);
    }

private:
    console_state*              Console;
    console_stream              Stream;
};

//---------------------------------------------------------------------------
// The standard output and error streams of the process, taken once and never given back
struct console_state
{
    std::mutex                  Mutex;
    console_target*             Target = nullptr;       // Of the running job, text is dropped if none
    std::string*                Capture = nullptr;      // Of a function wanting all what its thread writes
    std::thread::id             Capture_Thread;
    std::mutex                  Capture_Mutex;          // One capture at a time

    console_state();
    void                        Format_Reset();

private:
    console_buffer              Out;
    console_buffer              Err;
    std::ios_base::fmtflags     Flags[2];
    std::streamsize             Precision[2];
    char                        Fill[2];
};

//---------------------------------------------------------------------------
console_state::console_state() :
    Out(this, Stream_Out),
    Err(this, Stream_Err)
{
    std::ostream* const Streams[2] = { &std::cout, &std::cerr };
    for (int i = 0; i < 2; i++)
    {
        Streams[i]->flush();
        Flags[i] = Streams[i]->flags();
        Precision[i] = Streams[i]->precision();
        Fill[i] = Streams[i]->fill();
    }
    std::cout.rdbuf(&Out);
    std::cerr.rdbuf(&Err);
}

//---------------------------------------------------------------------------
void console_state::Format_Reset()
{
    // The library changes the format of the streams (e.g. hexadecimal numbers), a run starts with the format a new process has
    std::ostream* const Streams[2] = { &std::cout, &std::cerr };
    for (int i = 0; i < 2; i++)
    {
        Streams[i]->clear();
        Streams[i]->flags(Flags[i]);
        Streams[i]->precision(Precision[i]);
        Streams[i]->fill(Fill[i]);
        Streams[i]->width(0);
    }
}

//---------------------------------------------------------------------------
static console_state* Console_Get()
{
    static console_state* const Console = new console_state; // Never deleted: the streams may be used until the very end of the process
    return Console;
}

//---------------------------------------------------------------------------
static void Console_Add(console_state* Console, console_stream Stream, const char* Text, size_t Size)
{
    std::lock_guard<std::mutex> Lock(Console->Mutex);
    if (Console->Capture && Console->Capture_Thread == std::this_thread::get_id())
        Console->Capture->append(Text, Size);
    else if (Console->Target)
        Console->Target->Add(Stream, Text, Size);
}

//---------------------------------------------------------------------------
// Text written to the streams by a function of the library
template<typename function_type>
static QString Console_Capture(function_type Function)
{
    console_state* Console = Console_Get();
    std::lock_guard<std::mutex> Capture_Lock(Console->Capture_Mutex); // Not a static of this function: there is a function per type of argument
    std::string Text;
    struct capture
    {
        capture(console_state* Console_, std::string* Text_) : Console(Console_)
        {
            std::lock_guard<std::mutex> Lock(Console->Mutex);
            Console->Capture = Text_;
            Console->Capture_Thread = std::this_thread::get_id();
        }
        ~capture()
        {
            std::lock_guard<std::mutex> Lock(Console->Mutex);
            Console->Capture = nullptr;
        }
        console_state*          Console;
    };
    {
        capture Capture(Console, &Text);
        Function();
    }
    return FromLocal(Text);
}

//***************************************************************************
// Internal data of a job
//***************************************************************************

//---------------------------------------------------------------------------
// The callbacks of the library have no user data: they use the running job, there is only one at a time
static std::atomic<GUI_Job*> Job_Running{ nullptr };
static std::timed_mutex Run_Mutex;

//---------------------------------------------------------------------------
// Question to the creator of a job, the thread of the library asking it waits for the answer
struct job_question
{
    std::mutex                  Mutex;
    std::condition_variable     Changed;
    bool                        IsDone = false;         // Answered, or nobody waits for the answer anymore
    bool                        Yes = false;
    bool                        ApplyToAll = false;
};

//---------------------------------------------------------------------------
struct GUI_Job::private_data
{
    // Cancellation
    std::atomic<bool>           IsCanceled{ false };

    // Library, there is a core only during the run
    std::mutex                  Core_Mutex;
    core*                       Core = nullptr;

    // Standard streams of the library and output of the encoder
    console_target              Console;

    // Question waiting for its answer
    std::mutex                  Asking_Mutex;           // One question at a time, the library may ask from several threads
    std::mutex                  Question_Mutex;
    std::shared_ptr<job_question> Question;
    typedef std::function<bool(GUI_Job* Job, bool& ApplyToAll)> question_function; // Called in the thread of the creator of the job
    static bool                 Question_Ask(GUI_Job* Job, const question_function& Function, bool* ApplyToAll = nullptr);
    void                        Question_Abandon();

    // Callbacks of the library
    static user_mode            Ask_Callback(user_mode* Mode, const std::string& FileName, const std::string& ExtraText, bool Always, bool* ProgressIndicator_IsPaused, std::condition_variable* ProgressIndicator_IsEnd);
    static bool                 Question_Callback(const std::string& Question);
    static int                  Run_Callback(const std::string& Command, std::string* Output);
};

//---------------------------------------------------------------------------
bool GUI_Job::private_data::Question_Ask(GUI_Job* Job, const question_function& Function, bool* ApplyToAll)
{
    private_data* Data = Job->Data;
    const std::shared_ptr<job_question> State = std::make_shared<job_question>();
    {
        // Tested with the registration of the question: a canceled or destroyed job has no question waiting forever
        std::lock_guard<std::mutex> Lock(Data->Question_Mutex);
        if (Data->IsCanceled)
            return false;
        Data->Question = State;
    }

    // The function may open a dialog and the job may be destroyed before it returns: only the state of the question is used after it
    const auto Call = [Job, State, Function]()
    {
        bool All = false;
        const bool Yes = !Job->Data->IsCanceled && Function(Job, All); // Nothing is asked if the job was canceled while the question was on its way
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (!State->IsDone)
        {
            State->Yes = Yes;
            State->ApplyToAll = All;
            State->IsDone = true;
        }
        State->Changed.notify_all();
    };
    bool IsAsked = true;
    if (QThread::currentThread() == Job->thread())
        Call(); // Not from another thread (the job was not started as a thread)
    else
        IsAsked = QMetaObject::invokeMethod(Job, Call, Qt::QueuedConnection);

    bool Yes = false;
    if (IsAsked)
    {
        std::unique_lock<std::mutex> Lock(State->Mutex);
        State->Changed.wait(Lock, [&State] { return State->IsDone; });
        Yes = State->Yes;
        if (ApplyToAll)
            *ApplyToAll = State->ApplyToAll;
    }
    {
        std::lock_guard<std::mutex> Lock(Data->Question_Mutex);
        Data->Question.reset();
    }
    if (Data->IsCanceled)
    {
        // Canceled while the question was asked: whatever was answered, nothing more is done
        Yes = false;
        if (ApplyToAll)
            *ApplyToAll = false;
    }
    return Yes;
}

//---------------------------------------------------------------------------
void GUI_Job::private_data::Question_Abandon()
{
    std::shared_ptr<job_question> State;
    {
        std::lock_guard<std::mutex> Lock(Question_Mutex);
        State = Question;
    }
    if (!State)
        return;
    std::lock_guard<std::mutex> Lock(State->Mutex);
    State->IsDone = true; // The answer is no
    State->Changed.notify_all();
}

//---------------------------------------------------------------------------
user_mode GUI_Job::private_data::Ask_Callback(user_mode* Mode, const std::string& FileName, const std::string& ExtraText, bool Always, bool*, std::condition_variable*)
{
    GUI_Job* Job = Job_Running;
    if (!Job)
        return Mode && *Mode != user_mode::Ask ? *Mode : AlwaysNo;

    // The answer given for all files to a question is known when the next question comes
    std::lock_guard<std::mutex> Asking_Lock(Job->Data->Asking_Mutex);
    if (Mode && *Mode != user_mode::Ask)
        return *Mode; // Already answered for all files
    if (!Job->Ask_Overwrite)
        return AlwaysNo;

    const QString FileName_ = FromLocal(FileName);
    const QString ExtraText_ = FromLocal(ExtraText);
    const bool CanApplyToAll = Mode && Always;
    bool ApplyToAll = false;
    const bool Yes = Question_Ask(Job, [FileName_, ExtraText_, CanApplyToAll](GUI_Job* Job_, bool& ApplyToAll_)
    {
        const auto Function = Job_->Ask_Overwrite; // A copy, the job may be destroyed during the call
        return Function && Function(FileName_, ExtraText_, CanApplyToAll, ApplyToAll_);
    }, &ApplyToAll);

    const user_mode Answer = Yes ? AlwaysYes : AlwaysNo;
    if (CanApplyToAll && ApplyToAll)
        *Mode = Answer;
    return Answer;
}

//---------------------------------------------------------------------------
bool GUI_Job::private_data::Question_Callback(const std::string& Question)
{
    GUI_Job* Job = Job_Running;
    if (!Job || !Job->Ask_Question)
        return false;

    std::lock_guard<std::mutex> Asking_Lock(Job->Data->Asking_Mutex);
    const QString Question_ = FromLocal(Question);
    return Question_Ask(Job, [Question_](GUI_Job* Job_, bool&)
    {
        const auto Function = Job_->Ask_Question; // A copy, the job may be destroyed during the call
        return Function && Function(Question_);
    });
}

//---------------------------------------------------------------------------
int GUI_Job::private_data::Run_Callback(const std::string& Command, std::string* Output)
{
    GUI_Job* Job = Job_Running;
    if (!Job)
        return 1;
    private_data* Data = Job->Data;
    if (Data->IsCanceled)
        return 1;
    console_state* Console = Console_Get();

    // No shell: the command is split as a shell would do with the quotes around the arguments
    QStringList Arguments = QProcess::splitCommand(FromLocal(Command));
    if (Arguments.isEmpty())
        return 1;
    const QString Program = Arguments.takeFirst();

    // Without Output, it is the encoding: progress from the count of frames of the longest track, else from its duration
    if (!Output)
    {
        quint64 FrameCount = 0;
        double Duration = 0;
        {
            std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
            if (Data->Core)
                for (const stream& Stream : Data->Core->Output.Streams)
                {
                    if (FrameCount < Stream.FrameCount)
                        FrameCount = Stream.FrameCount;
                    if (Duration < Stream.Duration)
                        Duration = Stream.Duration;
                }
        }
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        Data->Console.Encoder_Line = console_line();
        Data->Console.Encoder_IsTime = !FrameCount && Duration > 0;
        Data->Console.Encoder_HasCurrent = false;
        Data->Console.Encoder_Current = 0;
        Data->Console.Encoder_Total = FrameCount ? FrameCount : (quint64)std::llround(Duration * 100);
        Data->Console.Encoder_IsRunning = true;
        Data->Console.Progress.clear();
    }
    struct encoder_end
    {
        ~encoder_end()
        {
            if (!IsEncoder)
                return;
            std::lock_guard<std::mutex> Lock(Console->Mutex);
            Target->Finish(Stream_Encoder);
            Target->Encoder_IsRunning = false;
            Target->Progress.clear();
        }
        console_state*          Console;
        console_target*         Target;
        bool                    IsEncoder;
    } Encoder_End{ Console, &Data->Console, !Output };

    // The program has no console window (it is started without one if this process has none) and no input (it can not wait for an answer)
    QProcess Process;
    if (Output)
        Process.setStandardErrorFile(QProcess::nullDevice()); // Only the standard output is wanted
    else
        Process.setProcessChannelMode(QProcess::MergedChannels); // Messages and progress, in the order they are written
    Process.start(Program, Arguments);
    if (!Process.waitForStarted(Process_Timeout))
    {
        if (!Output)
        {
            // Same form as the messages of the library, they are not translated
            std::lock_guard<std::mutex> Lock(Console->Mutex);
            Data->Console.Message_Add(QStringLiteral("Error: FFmpeg can not be run (%1).").arg(Program));
        }
        return 1;
    }
    Process.closeWriteChannel();

    // What the program writes, while it runs
    QByteArray Output_Bytes;
    const auto Read = [&]()
    {
        const QByteArray Bytes = Process.readAll();
        if (Bytes.isEmpty())
            return;
        if (Output)
        {
            Output_Bytes += Bytes;
            return;
        }
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        Data->Console.Add(Stream_Encoder, Bytes.constData(), (size_t)Bytes.size());
        Data->Console.Encoder_Update();
    };
    QElapsedTimer Timer;
    Timer.start();
    while (Process.state() != QProcess::NotRunning)
    {
        if (Data->IsCanceled || (Output && Timer.hasExpired(Process_Timeout)))
        {
            Process.kill();
            Process.waitForFinished(Process_Timeout);
            return 1;
        }
        Process.waitForReadyRead(100); // Not more, for the cancellation
        Read();
    }
    Read(); // What was written just before the end

    if (Process.exitStatus() != QProcess::NormalExit)
    {
        if (!Output)
        {
            std::lock_guard<std::mutex> Lock(Console->Mutex);
            Data->Console.Finish(Stream_Encoder);
            Data->Console.Message_Add(QStringLiteral("Error: FFmpeg stopped unexpectedly (%1).").arg(Program));
        }
        return 1;
    }
    if (Output)
    {
        Output_Bytes.replace("\r\n", "\n"); // As a text stream gives it
        Output->assign(Output_Bytes.constData(), (size_t)Output_Bytes.size());
    }
    return Process.exitCode();
}

//***************************************************************************
// Results
//***************************************************************************

//---------------------------------------------------------------------------
static gui_field Field_From(const metadata::field& Source)
{
    gui_field Field;
    Field.Group = FromLocal(Source.Info.Group);
    Field.Name = FromLocal(Source.Info.Name);
    Field.Description = FromLocal(Source.Info.Description);
    Field.Type = (gui_field::type)Source.Info.Type;
    Field.Size = Source.Info.Size;
    Field.IsEditable = (Source.Info.Flags & metadata_field::Editable) != 0; // Flags of the field for this track: not editable if a file has no place for it
    Field.IsStructural = (Source.Info.Flags & metadata_field::Structural) != 0;
    Field.First = FromLocal(Source.First);
    Field.Last = FromLocal(Source.Last);
    Field.Kind = (gui_field::kind)Source.Kind;
    return Field;
}

//---------------------------------------------------------------------------
static gui_track Track_From(const stream& Source)
{
    gui_track Track;
    Track.FileName = FromLocal(Source.FileName);
    if (Source.FileName_Template.empty() || Source.FileCount <= 1)
        Track.Name = Track.FileName; // Also for an image alone: the library has a template for it, but it is a file for the user
    else
    {
        Track.Name = FromLocal(Source.FileName_Template);
        Track.Numbers = FromLocal(Source.FileName_StartNumber) + QLatin1String(" --> ") + FromLocal(Source.FileName_EndNumber);
        if (!Source.FileList.empty())
            Track.Numbers += GUI_Job::tr(", with gaps");
    }
    Track.Flavor = FromLocal(Source.Flavor);
    Track.Width = Source.Width;
    Track.Height = Source.Height;
    Track.FileCount = Source.FileCount;
    Track.FrameCount = Source.FrameCount;
    Track.FrameRate = FrameRate_Clean(Source.FrameRate);
    Track.Duration = Source.Duration;
    Track.IsNotLicensed = Source.Problem;
    Track.MayHavePaddingBits = Source.MayHavePaddingBits;
    if (Source.Metadata)
    {
        Track.IsMetadataComplete = Source.Metadata->IsComplete() && Source.Metadata->FileCount() == Source.FileCount; // Also the files missing at the end
        Track.Fields.reserve(Source.Metadata->Fields().size());
        for (const metadata::field& Field : Source.Metadata->Fields())
            Track.Fields.push_back(Field_From(Field));
    }
    return Track;
}

//---------------------------------------------------------------------------
static QStringList Info_From(const std::vector<compressed>& Source)
{
    QStringList Info;
    for (const compressed& Compressed : Source)
    {
        QString Prefix;
        if (Source.size() > 1)
            Prefix = FileName_Short(Compressed.FileName) + QLatin1String(": ");
        if (Compressed.LibraryNameVersion.empty())
            Info += Prefix + GUI_Job::tr("No reversibility data found.");
        else
            Info += Prefix + GUI_Job::tr("Reversibility data created by %1.").arg(FromLocal(Compressed.LibraryNameVersion));
        if (Compressed.HasHashes)
            Info += Prefix + GUI_Job::tr("Hashes of the original files present (used by the reversibility check).");
        if (Compressed.HashFiles_Count == 1)
            Info += Prefix + GUI_Job::tr("1 hash file (used by the conformance check) found.");
        else if (Compressed.HashFiles_Count)
            Info += Prefix + GUI_Job::tr("%1 hash files (used by the conformance check) found.").arg(Compressed.HashFiles_Count);
    }
    return Info;
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Job::GUI_Job(QObject* Parent) :
    QThread(Parent),
    Data(new private_data)
{
    // The library was made for the main thread of a program, it gets the stack size the main thread usually has (other threads have less on some systems)
    #if defined(Q_OS_UNIX)
        setStackSize(8 * 1024 * 1024);
    #endif
}

//---------------------------------------------------------------------------
GUI_Job::~GUI_Job()
{
    // Not expected while running, but the thread uses this object: it is stopped, without waiting for an answer this thread could not give anymore
    if (isRunning())
    {
        Cancel();
        Data->Question_Abandon();
    }
    wait(); // Also for a job destroyed from its finished signal: the thread is ending
    delete Data;
}

//***************************************************************************
// Run
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Job::run()
{
    ExitCode = 1;
    Tracks.clear();
    Attachments.clear();
    IsCompressed = false;
    Info.clear();
    Out.clear();
    Err.clear();

    // The library has process-wide state: a job started while another one runs waits for its end, unless it is canceled
    std::unique_lock<std::timed_mutex> Run_Lock(Run_Mutex, std::defer_lock);
    while (!Run_Lock.try_lock_for(std::chrono::milliseconds(50)))
        if (Data->IsCanceled)
            return;
    if (Data->IsCanceled)
        return;

    // Streams: this job gets what the library writes from now on
    console_state* Console = Console_Get();
    {
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        const QString Log = Data->Console.Log;
        Data->Console = console_target();
        Data->Console.Log = Log;
        Data->Console.Out_IsLogged = !Arguments.contains(QLatin1String("--sublicense")) && !Arguments.contains(QLatin1String("--sublicence")); // The standard output is then a license key
        Console->Target = &Data->Console;
    }
    Console->Format_Reset();
    Job_Running = this;

    // Command line. An argument with characters the encoding of the library does not have would be another name for the library (maybe an existing one): nothing is run
    std::vector<QByteArray> Arguments_Local;
    Arguments_Local.reserve((size_t)Arguments.size() + 1);
    Arguments_Local.push_back(QByteArray("rawcooked"));
    QString Message; // Of the job itself, same form as the messages of the library (they are not translated)
    bool IsKey = false;
    for (const QString& Argument : Arguments)
    {
        Arguments_Local.push_back(Argument.toLocal8Bit());
        if (Message.isEmpty() && QString::fromLocal8Bit(Arguments_Local.back()) != Argument)
            Message = QStringLiteral("Error: %1 has characters which are not supported by the character encoding of the system.").arg(IsKey ? QStringLiteral("the license key") : Argument_Quoted(Argument));
        IsKey = Argument_IsKeyOption(Argument);
    }
    std::vector<const char*> argv;
    argv.reserve(Arguments_Local.size());
    for (const QByteArray& Argument : Arguments_Local)
        argv.push_back(Argument.constData());

    // Library
    int Value = 1;
    if (Message.isEmpty())
    {
        std::unique_ptr<core> Core(new core);
        Core->Global.Ask_Callback = private_data::Ask_Callback;
        Core->Global.Question_Callback = private_data::Question_Callback;
        Core->Global.Run_Callback = private_data::Run_Callback;
        Core->Global.AnalyzeOnly = AnalyzeOnly;
        Core->Global.Metadata_Enabled = Metadata;
        Core->Global.Edits_FileName = ToLocal(Edits_FileName);
        if (Overwrite)
            Core->Global.OutputOptions["y"] = std::string(); // Only the encoder does not ask, the library still asks its own questions
        {
            std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
            Data->Core = Core.get();
            if (Data->IsCanceled)
                Core->Global.Cancel = true;
        }

        try
        {
            Value = Core->Run(argv.data(), (int)argv.size());
        }
        catch (const std::exception& Exception)
        {
            // E.g. a value of an option which is not a number: the command line program would stop there
            Value = 1;
            Message = QStringLiteral("Error: the processing stopped unexpectedly (%1).").arg(FromLocal(Exception.what()));
        }
        catch (...)
        {
            Value = 1;
            Message = QStringLiteral("Error: the processing stopped unexpectedly.");
        }

        // Results
        Tracks.reserve(Core->Output.Streams.size());
        for (const stream& Stream : Core->Output.Streams)
            Tracks.push_back(Track_From(Stream));
        for (const attachment& Attachment : Core->Output.Attachments)
            Attachments += FromLocal(Attachment.FileName_In);
        IsCompressed = !Core->Output.Compressed.empty() && Core->Output.Streams.empty();
        if (IsCompressed)
            Info = Info_From(Core->Output.Compressed); // Not for the output of an encoding: its quick check does not read the reversibility data, it would be said missing

        // The core is destroyed here, in this thread, and it may still write to the streams (end of its progress indicator)
        {
            std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
            Data->Core = nullptr;
        }
    }

    // End of the streams for this job
    Job_Running = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        Console->Target = nullptr;
        Data->Console.Finish(Stream_Out);
        Data->Console.Finish(Stream_Err);
        Data->Console.Progress.clear();
        if (!Message.isEmpty())
            Data->Console.Message_Add(Message);
        Out = Data->Console.Out;
        Err = Data->Console.Err;
    }
    ExitCode = Value;
}

//***************************************************************************
// While running
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Job::Cancel()
{
    Data->IsCanceled = true; // Also for the callbacks: from now on they answer no without asking

    // The core is destroyed at the end of the run
    std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
    if (Data->Core)
        Data->Core->Global.Cancel = true;
}

//---------------------------------------------------------------------------
bool GUI_Job::IsCanceled() const
{
    return Data->IsCanceled;
}

//---------------------------------------------------------------------------
void GUI_Job::Progress(quint64& Current, quint64& Total, QString& Text) const
{
    Current = 0;
    Total = 0;
    Text.clear();

    // Encoder
    bool Encoder_IsRunning;
    {
        console_state* Console = Console_Get();
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        if (Console->Target != &Data->Console)
            return; // Not running
        Text = Data->Console.Progress_Text();
        Encoder_IsRunning = Data->Console.Encoder_IsRunning;
        if (Encoder_IsRunning && Data->Console.Encoder_HasCurrent)
        {
            Current = Data->Console.Encoder_Current;
            Total = Data->Console.Encoder_Total;
        }
    }

    // Library
    if (!Encoder_IsRunning)
    {
        std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
        if (Data->Core)
        {
            Current = Data->Core->Global.Progress.Current;
            Total = Data->Core->Global.Progress.Total;
        }
    }

    // The counters of the library may restart or go beyond the total
    if (Total && Current > Total)
        Current = Total;
}

//---------------------------------------------------------------------------
QString GUI_Job::Log_Take()
{
    console_state* Console = Console_Get();
    std::lock_guard<std::mutex> Lock(Console->Mutex);
    QString Result;
    Result.swap(Data->Console.Log);
    return Result;
}

//---------------------------------------------------------------------------
QString GUI_Job::CommandLine() const
{
    QString Result = QStringLiteral("rawcooked");
    bool IsKey = false;
    for (const QString& Argument : Arguments)
    {
        Result += QLatin1Char(' ');
        if (IsKey)
        {
            Result += QLatin1String("***"); // A license key is never shown
            IsKey = false;
            continue;
        }
        Result += Argument_Quoted(Argument);
        IsKey = Argument_IsKeyOption(Argument);
    }
    return Result;
}

//***************************************************************************
// Library, outside of a job
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Job::Console_Init()
{
    Console_Get();
}

//---------------------------------------------------------------------------
QString GUI_Job::Library_Name()
{
    return FromLocal(GetLibraryName());
}

//---------------------------------------------------------------------------
QString GUI_Job::Library_Version()
{
    return FromLocal(GetLibraryVersion());
}

//---------------------------------------------------------------------------
QString GUI_Job::Help_Options()
{
    return Console_Capture([] { ::Help(nullptr); });
}

//---------------------------------------------------------------------------
QString GUI_Job::Help_Edits()
{
    return Console_Capture([] { ::Help_Edit(); });
}
