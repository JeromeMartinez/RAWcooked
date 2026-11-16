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
#include <QHash>
#include <QMetaObject>
#include <QProcess>
#include <QSet>
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
#if defined(Q_OS_WIN)
    #include <qt_windows.h>
#else
    #include <signal.h>
    #include <unistd.h>
    #if defined(Q_OS_LINUX)
        #include <sys/prctl.h>
    #endif
#endif
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
static_assert((int)gui_compressed::Checked_No == (int)compressed::checked::No
           && (int)gui_compressed::Checked_Decoding == (int)compressed::checked::Decoding
           && (int)gui_compressed::Checked_Reversibility == (int)compressed::checked::Reversibility, "gui_compressed::checked is not compressed::checked");

//---------------------------------------------------------------------------
// The external program is given up after this time (ms) when only its output is wanted
static const int Process_Timeout = 30000;

//---------------------------------------------------------------------------
// The cancellation is tested at this period (ms) while the external program runs
static const int Process_Period = 100;

//---------------------------------------------------------------------------
// The cancellation is tested at this period while a job waits for the end of another one
static const std::chrono::milliseconds Run_Period(50);

//---------------------------------------------------------------------------
// A progress line of the library has no end before the next one: it is complete when nothing was added for this time
static const std::chrono::milliseconds Progress_Delay(20);

//---------------------------------------------------------------------------
// Progress line of the library during the analysis of the files of the inputs, the other ones are about compressed content
static const char Progress_Files[] = "Analyzing files";

//---------------------------------------------------------------------------
// Count of lines kept from the end of the output, for a failure without error message
static const int Messages_Last_Max = 10;

//---------------------------------------------------------------------------
// Stack of the thread of a job: the size the main thread of a program usually has
#if defined(Q_OS_UNIX)
    static const uint Stack_Size = 8 * 1024 * 1024;
#endif

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
// Messages of the library
//***************************************************************************

//---------------------------------------------------------------------------
enum message_kind
{
    Message_None,
    Message_Error,
    Message_Warning,
    Message_Info,
    Message_Max,
};

//---------------------------------------------------------------------------
// What the library writes itself on its streams starts a line with its kind
static message_kind Message_Kind(const QString& Line)
{
    if (Line.startsWith(QLatin1String("Error:")))
        return Message_Error;
    if (Line.startsWith(QLatin1String("Warning:")))
        return Message_Warning;
    if (Line.startsWith(QLatin1String("Info:")))
        return Message_Info;
    return Message_None;
}

//---------------------------------------------------------------------------
// First line of the list of the tracks or of the attachments, the lines of the list start with a space
static bool Message_IsList(const QString& Line)
{
    if (Line == QLatin1String("Attachments:"))
        return true;
    if (Line.size() < 8 || !Line.startsWith(QLatin1String("Track ")) || !Line.endsWith(QLatin1Char(':')))
        return false;
    for (auto i = 6; i < Line.size() - 1; i++)
    {
        const auto Char = Line.at(i).unicode();
        if (Char < '0' || Char > '9')
            return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// A message: its first line and the lines indented under it, or a line which continues the previous message
struct message
{
    message_kind                Kind;
    bool                        IsOut;                  // From the standard output
    QString                     Text;
};

//---------------------------------------------------------------------------
// Messages of a text given line by line
struct message_reader
{
    message_kind                Kind = Message_None;    // Of the message the next line continues, if any
    size_t                      Pos = (size_t)-1;       // Message the next indented line is a part of

    void                        Line(std::vector<message>& Messages, const QString& Text, bool IsOut);
    void                        End();                  // An empty line ends a message
};

//---------------------------------------------------------------------------
void message_reader::Line(std::vector<message>& Messages, const QString& Text, bool IsOut)
{
    const message_kind Kind_New = Message_Kind(Text);
    if (Kind_New != Message_None)
        Kind = Kind_New;
    if (Kind == Message_None)
        return;
    if (Kind_New == Message_None && Pos < Messages.size() && Text.startsWith(QLatin1Char(' ')))
    {
        Messages[Pos].Text += QLatin1Char('\n');
        Messages[Pos].Text += Text;
        return;
    }
    Pos = Messages.size();
    Messages.push_back({ Kind, IsOut, Text });
}

//---------------------------------------------------------------------------
void message_reader::End()
{
    Kind = Message_None;
    Pos = (size_t)-1;
}

//---------------------------------------------------------------------------
// Errors and warnings recorded by the parsers of the library. Their kind is known without their text when there is only one kind
static std::vector<message> Messages_Recorded(errors& Errors)
{
    std::vector<message> Messages;
    const bool HasErrors = Errors.HasErrors();
    const bool HasWarnings = Errors.HasWarnings();
    const char* Text = (HasErrors || HasWarnings) ? Errors.ErrorMessage() : nullptr;
    if (!Text)
        return Messages;
    const message_kind Kind = HasErrors ? Message_Error : Message_Warning;
    message_reader Reader;
    Reader.Kind = Kind;
    const QStringList Lines = FromLocal(Text).split(QLatin1Char('\n'));
    for (const QString& Line : Lines)
        if (!Line.trimmed().isEmpty())
            Reader.Line(Messages, Line, false);
    if (HasErrors != HasWarnings)
        for (message& Message : Messages)
            Message.Kind = Kind;
    return Messages;
}

//---------------------------------------------------------------------------
// Lines of the messages, by kind. What the parsers recorded is written by the library a first time before a question about it
// and again at the end, and a message may be on both streams: such a message is taken once, at its first place
static void Messages_Lines(const std::vector<message>& Written, const std::vector<message>& Recorded, QStringList Lines[Message_Max])
{
    QHash<QString, size_t> Recorded_Pos;
    for (size_t i = 0; i < Recorded.size(); i++)
        Recorded_Pos.insert(Recorded[i].Text, i);
    std::vector<bool> Recorded_IsTaken(Recorded.size());
    QSet<QString> Written_Texts[2]; // Standard error, standard output
    const auto Add = [Lines](message_kind Kind, const QString& Text)
    {
        Lines[Kind] += Text.split(QLatin1Char('\n'));
    };

    for (const message& Message : Written)
    {
        const auto Found = Recorded_Pos.constFind(Message.Text);
        if (Found != Recorded_Pos.constEnd())
        {
            if (!Recorded_IsTaken[Found.value()])
            {
                Recorded_IsTaken[Found.value()] = true;
                Add(Recorded[Found.value()].Kind, Message.Text);
            }
            continue;
        }
        if (Written_Texts[!Message.IsOut].contains(Message.Text))
            continue;
        Written_Texts[Message.IsOut].insert(Message.Text);
        Add(Message.Kind, Message.Text);
    }
    for (size_t i = 0; i < Recorded.size(); i++)
        if (!Recorded_IsTaken[i])
            Add(Recorded[i].Kind, Recorded[i].Text); // Not written (yet)
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
// Exit status which is an error code of FFmpeg made of characters (e.g. option not found): on Windows it is in the range of the statuses of a crash
#if defined(Q_OS_WIN)
static bool Encoder_IsErrorTag(int ExitCode)
{
    quint32 Tag = 0 - (quint32)ExitCode;
    for (int i = 0; i < 4; i++)
    {
        const quint32 Char = (Tag >> (i * 8)) & 0xFF;
        if ((Char < 0x20 || Char > 0x7E) && (i || Char != 0xF8))
            return false;
    }
    return true;
}
#endif

//---------------------------------------------------------------------------
// What a job gets from the streams. Everything is used under the mutex of the console
struct console_target
{
    // Standard streams, by thread: the library writes a line in several times and from several threads
    std::map<std::thread::id, console_line> Lines[Stream_Encoder];
    std::thread::id             Progress_Thread;        // Thread of the last carriage return on the standard error stream
    console_line                Encoder_Line;
    QString                     Out;
    QString                     Err;
    QString                     Log;                    // Lines not yet taken
    QString                     Progress;               // Last progress line
    bool                        Progress_IsContent = false; // The last progress line of the library is about compressed content
    bool                        Out_IsLogged = true;

    // Messages
    std::vector<message>        Messages;               // Of the library, as they are written
    QStringList                 Messages_Last;          // Last paragraph of the standard error and of the output of the encoder, the list of the tracks apart

    // Encoder
    bool                        Encoder_IsRunning = false;
    bool                        Encoder_IsStarted = false; // Also after its end
    bool                        Encoder_IsTime = false; // Progress from the position in the content instead of the count of frames
    bool                        Encoder_HasCurrent = false; // The encoder may write no progress line (log level), its progress is then unknown
    quint64                     Encoder_Current = 0;
    quint64                     Encoder_Total = 0;      // 0 if unknown

    void                        Add(console_stream Stream, const char* Text, size_t Size);
    void                        Finish(console_stream Stream); // No more text
    void                        Message_Add(const QString& Text); // Message of the job itself, as if the library had written it
    void                        Paragraph_End();        // What follows is not a part of the messages written before
    void                        Encoder_Update();
    QString                     Progress_Text() const;
    void                        Progress_Kind(const QString& Text);

private:
    message_reader              Messages_Reader[Stream_Encoder];
    bool                        Messages_IsList = false; // In the list of the tracks
    bool                        Messages_Last_IsEnded = false;

    void                        Line_End(console_stream Stream, console_line& Line, bool IsProgress);
    void                        Line_Finish(console_stream Stream, console_line& Line);
    void                        Line_Message(console_stream Stream, const QString& Text);
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
    Line_Message(Stream_Err, Text);
    Err += Text;
    Err += QLatin1Char('\n');
    Log += Text;
    Log += QLatin1Char('\n');
}

//---------------------------------------------------------------------------
void console_target::Paragraph_End()
{
    Messages_Reader[Stream_Err].End();
    Messages_IsList = false;
    Messages_Last_IsEnded = true;
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
        if (!IsEncoder)
            Progress_Kind(Progress);
        return;
    }

    // Line
    if (CR_Start)
        while (Text.endsWith(QLatin1Char(' ')))
            Text.chop(1); // Spaces erasing the end of the progress line which was there
    Line_Message(Stream, Text);
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
void console_target::Line_Message(console_stream Stream, const QString& Text)
{
    const bool IsEmpty = Text.trimmed().isEmpty();
    if (Stream == Stream_Out)
    {
        if (IsEmpty)
            Messages_Reader[Stream_Out].End();
        else
            Messages_Reader[Stream_Out].Line(Messages, Text, true);
        return;
    }

    // The list of the tracks ends at the first line which is not one of its lines (the name of a track may be empty)
    if (Messages_IsList)
    {
        if (Text.startsWith(QLatin1Char(' ')) || Message_IsList(Text))
            return;
        Messages_IsList = false;
    }
    const bool IsList = Stream == Stream_Err && Message_IsList(Text);
    if (IsEmpty || IsList)
    {
        Paragraph_End();
        Messages_IsList = IsList;
        return;
    }

    // The lines of the encoder are not messages of the library, they are only the reason of a failure when it has no message
    if (Stream == Stream_Err)
        Messages_Reader[Stream_Err].Line(Messages, Text, false);
    if (Messages_Last_IsEnded)
    {
        Messages_Last_IsEnded = false;
        Messages_Last.clear();
    }
    Messages_Last.push_back(Text);
    if (Messages_Last.size() > Messages_Last_Max)
        Messages_Last.removeFirst();
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
void console_target::Progress_Kind(const QString& Text)
{
    if (!Text.isEmpty())
        Progress_IsContent = !Text.startsWith(QLatin1String(Progress_Files));
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
// External program
//***************************************************************************

//---------------------------------------------------------------------------
#if defined(Q_OS_UNIX)
// In the new process, before the program: it leads a process group, so that it can be stopped with everything it starts
static void Process_Child()
{
    setpgid(0, 0);
    #if defined(Q_OS_LINUX)
        prctl(PR_SET_PDEATHSIG, SIGKILL); // Not left running when this program ends for any reason (nothing as simple on macOS)
    #endif
}
#endif

//---------------------------------------------------------------------------
// Program started from a command line of the library, stopped with everything it started
class job_process : public QProcess
{
public:
    ~job_process();

    bool                        Start(const std::string& Command, QString& Program); // False if nothing could be started; Program: name for the messages
    bool                        Stop();                 // False if it is still running

#if defined(Q_OS_WIN)
private:
    HANDLE                      Group = nullptr;        // Job object of the program and of what it starts
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
protected:
    void                        setupChildProcess() override { Process_Child(); }
#endif
};

//---------------------------------------------------------------------------
job_process::~job_process()
{
    #if defined(Q_OS_WIN)
        if (Group)
            CloseHandle(Group); // The system ends what is still in it
    #endif
}

//---------------------------------------------------------------------------
bool job_process::Start(const std::string& Command, QString& Program)
{
    // Split as a shell would do with the quotes around the arguments
    QStringList Arguments = QProcess::splitCommand(FromLocal(Command));
    if (Arguments.isEmpty())
        return false;
    Program = Arguments.takeFirst();

    #if defined(Q_OS_WIN)
        // No shell. The program is in a job object before it runs, so that what it starts is in it too.
        // The system ends the content of a job object when it is closed, also when this program ends for any reason
        Group = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = {};
        Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (Group && !SetInformationJobObject(Group, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
        {
            CloseHandle(Group);
            Group = nullptr;
        }
        PROCESS_INFORMATION* Information = nullptr;
        if (Group)
            setCreateProcessArgumentsModifier([&Information](QProcess::CreateProcessArguments* Arguments_)
            {
                Arguments_->flags |= CREATE_SUSPENDED;
                Information = Arguments_->processInformation;
            });
        start(Program, Arguments);
        setCreateProcessArgumentsModifier(CreateProcessArgumentModifier());
        if (state() == QProcess::NotRunning)
            return false;
        if (Information)
        {
            if (!AssignProcessToJobObject(Group, Information->hProcess))
            {
                // Refused when this program is itself in a job object which accepts no other one: only the program itself can be stopped
                CloseHandle(Group);
                Group = nullptr;
            }
            if (ResumeThread(Information->hThread) == (DWORD)-1)
            {
                Stop();
                return false;
            }
        }
        if (!waitForStarted(Process_Timeout))
            return false;
    #else
        // The command goes to the shell with the bytes the library gave, as the command line program does: the names of files are not converted.
        // With exec the shell becomes the program, which is then the process this object knows
        #if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            setChildProcessModifier(Process_Child);
        #endif
        start(QStringLiteral("/bin/sh"), QStringList() << QStringLiteral("-s"));
        if (!waitForStarted(Process_Timeout))
            return false;
        QByteArray Script("exec ");
        Script += QByteArray(Command.data(), (int)Command.size());
        Script += '\n';
        write(Script);
        while (bytesToWrite() > 0)
            if (!waitForBytesWritten(Process_Timeout))
                break; // The shell ended, its exit status says why
    #endif

    // The program has no input (it can not wait for an answer)
    closeWriteChannel();
    return true;
}

//---------------------------------------------------------------------------
bool job_process::Stop()
{
    #if defined(Q_OS_WIN)
        if (Group)
            TerminateJobObject(Group, 1);
    #else
        // The process is not yet waited for by this object while it is said running: its identifier is still the one of its group
        const qint64 Id = processId();
        if (Id > 0 && state() != QProcess::NotRunning)
            ::kill(-(pid_t)Id, SIGKILL);
    #endif
    kill();
    return waitForFinished(Process_Timeout) || state() == QProcess::NotRunning;
}

//***************************************************************************
// Internal data of a job
//***************************************************************************

//---------------------------------------------------------------------------
// The callbacks of the library have no user data: they use the running job, there is only one at a time
static std::atomic<GUI_Job*> Job_Running{ nullptr };
static std::timed_mutex Run_Mutex;

//---------------------------------------------------------------------------
// Results which are known while the job runs. The job gets them in the thread of its creator with each question, and at its end
struct job_results
{
    QStringList                 Messages[Message_Max];
    bool                        Messages_IsOutput = false; // The errors are the last lines of the output
    bool                        IsEncoderStarted = false;
    bool                        IsEncoded = false;
    bool                        IsEncoderMissing = false;

    void                        Set(GUI_Job* Job) const;
};

//---------------------------------------------------------------------------
void job_results::Set(GUI_Job* Job) const
{
    Job->Messages_Error = Messages[Message_Error];
    Job->Messages_Error_IsOutput = Messages_IsOutput;
    Job->Messages_Warning = Messages[Message_Warning];
    Job->Messages_Info = Messages[Message_Info];
    Job->IsEncoderStarted = IsEncoderStarted;
    Job->IsEncoded = IsEncoded;
    Job->IsEncoderMissing = IsEncoderMissing;
}

//---------------------------------------------------------------------------
// Question to the creator of a job, the thread of the library asking it waits for the answer
struct job_question
{
    std::mutex                  Mutex;
    std::condition_variable     Changed;
    job_results                 Results;                // What the job knows when it asks, the question may be about it
    bool                        IsDone = false;         // Answered, or nobody waits for the answer anymore
    bool                        Yes = false;
    bool                        ApplyToAll = false;
};

//---------------------------------------------------------------------------
struct GUI_Job::private_data
{
    // Thread
    std::atomic<bool>           IsStarted{ false };

    // Cancellation
    std::atomic<bool>           IsCanceled{ false };

    // Library, there is a core only during the run
    std::mutex                  Core_Mutex;
    core*                       Core = nullptr;
    std::thread::id             Run_Thread;             // Thread of the run, the library also has threads of its own

    // Standard streams of the library and output of the encoder
    console_target              Console;

    // Encoder
    std::atomic<bool>           IsEncoderStarted{ false };
    std::atomic<bool>           IsEncoded{ false };
    std::atomic<bool>           IsEncoderMissing{ false };

    // Question waiting for its answer
    std::mutex                  Asking_Mutex;           // One question at a time, the library may ask from several threads
    std::mutex                  Question_Mutex;
    std::shared_ptr<job_question> Question;
    std::atomic<bool>           IsRefused{ false };     // A question was answered no: a failure of the run may be the choice of the user
    typedef std::function<bool(GUI_Job* Job, bool& ApplyToAll)> question_function; // Called in the thread of the creator of the job
    static bool                 Question_Ask(GUI_Job* Job, const question_function& Function, bool* ApplyToAll = nullptr);
    void                        Question_Abandon();

    // Results
    void                        Results_Get(job_results& Results, const std::vector<message>& Recorded);

    // Callbacks of the library
    static user_mode            Ask_Callback(user_mode* Mode, const std::string& FileName, const std::string& ExtraText, bool Always, bool* ProgressIndicator_IsPaused, std::condition_variable* ProgressIndicator_IsEnd);
    static bool                 Question_Callback(const std::string& Question);
    static int                  Run_Callback(const std::string& Command, std::string* Output);
    int                         Run(const std::string& Command, std::string* Output);
};

//---------------------------------------------------------------------------
void GUI_Job::private_data::Results_Get(job_results& Results, const std::vector<message>& Recorded)
{
    std::vector<message> Written;
    {
        std::lock_guard<std::mutex> Lock(Console_Get()->Mutex);
        Written = Console.Messages;
    }
    Messages_Lines(Written, Recorded, Results.Messages);
    Results.IsEncoderStarted = IsEncoderStarted;
    Results.IsEncoded = IsEncoded;
    Results.IsEncoderMissing = IsEncoderMissing;
}

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

    // What the parsers recorded is read in the thread of the run only: the library reads it there, and its text is made again by each reading
    std::vector<message> Recorded;
    if (std::this_thread::get_id() == Data->Run_Thread)
    {
        std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
        if (Data->Core)
            Recorded = Messages_Recorded(Data->Core->Global.Errors);
    }
    job_results Results;
    Data->Results_Get(Results, Recorded);
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        State->Results = Results;
    }

    // The function may open a dialog and the job may be destroyed before it returns: only the state of the question is used after it
    const auto Call = [Job, State, Function]()
    {
        bool All = false;
        bool Yes = false;
        if (!Job->Data->IsCanceled) // Nothing is asked if the job was canceled while the question was on its way
        {
            {
                std::lock_guard<std::mutex> Lock(State->Mutex);
                State->Results.Set(Job);
            }
            Yes = Function(Job, All);
        }
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
    else if (!Yes)
        Data->IsRefused = true;
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
    {
        Job->Data->IsRefused = true;
        return AlwaysNo;
    }

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
    if (!Job)
        return false;
    if (!Job->Ask_Question)
    {
        Job->Data->IsRefused = true;
        return false;
    }

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
    if (!Job || Job->Data->IsCanceled)
        return 1;
    private_data* Data = Job->Data;
    if (Output)
        return Data->Run(Command, Output);

    // Without Output, it is the encoding: progress from the count of frames of the longest track, else from its duration
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
    console_state* Console = Console_Get();
    {
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        Data->Console.Paragraph_End();
        Data->Console.Encoder_Line = console_line();
        Data->Console.Encoder_IsTime = !FrameCount && Duration > 0;
        Data->Console.Encoder_HasCurrent = false;
        Data->Console.Encoder_Current = 0;
        Data->Console.Encoder_Total = FrameCount ? FrameCount : (quint64)std::llround(Duration * 100);
        Data->Console.Encoder_IsRunning = true;
        Data->Console.Progress.clear();
    }
    const int Status = Data->Run(Command, nullptr);
    {
        std::lock_guard<std::mutex> Lock(Console->Mutex);
        Data->Console.Finish(Stream_Encoder);
        Data->Console.Paragraph_End();
        Data->Console.Encoder_IsRunning = false;
        Data->Console.Progress.clear();
    }
    return Status;
}

//---------------------------------------------------------------------------
int GUI_Job::private_data::Run(const std::string& Command, std::string* Output)
{
    console_state* State = Console_Get();
    const bool IsEncoder = !Output;

    // Message of the job itself: same form as the messages of the library, which are not translated
    QString Program;
    const auto Message_Add = [&](const char* Text)
    {
        if (!IsEncoder)
            return; // The library says what the missing output means
        std::lock_guard<std::mutex> Lock(State->Mutex);
        Console.Finish(Stream_Encoder);
        Console.Message_Add(QLatin1String("Error: FFmpeg ") + QLatin1String(Text) + QLatin1String(" (") + Program + QLatin1String(")."));
    };
    const auto Missing = [&]()
    {
        IsEncoderMissing = true;
        Message_Add("can not be run");
        return 1;
    };

    // The program has no console window (it is started without one if this process has none)
    job_process Process;
    if (Output)
        Process.setStandardErrorFile(QProcess::nullDevice()); // Only the standard output is wanted
    else
        Process.setProcessChannelMode(QProcess::MergedChannels); // Messages and progress, in the order they are written
    if (!Process.Start(Command, Program))
        return Missing();
    if (IsEncoder)
    {
        IsEncoderStarted = true;
        std::lock_guard<std::mutex> Lock(State->Mutex);
        Console.Encoder_IsStarted = true;
    }

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
        std::lock_guard<std::mutex> Lock(State->Mutex);
        Console.Add(Stream_Encoder, Bytes.constData(), (size_t)Bytes.size());
        Console.Encoder_Update();
    };
    QElapsedTimer Timer;
    Timer.start();
    while (Process.state() != QProcess::NotRunning)
    {
        if (IsCanceled || (Output && Timer.hasExpired(Process_Timeout)))
        {
            if (!Process.Stop())
                Message_Add("can not be stopped");
            return 1;
        }
        Process.waitForReadyRead(Process_Period);
        Read();
    }
    Read(); // What was written just before the end

    const int Status = Process.exitCode();
    bool IsCrash = Process.exitStatus() != QProcess::NormalExit;
    #if defined(Q_OS_WIN)
        if (IsCrash && Encoder_IsErrorTag(Status))
            IsCrash = false; // The encoder said why it failed
    #endif
    if (IsCrash)
    {
        Message_Add("stopped unexpectedly");
        return 1;
    }
    #if defined(Q_OS_UNIX)
        if (Status == 127 || Status == 126)
        {
            // Exit status of the shell when the program is not found or can not be executed
            if (IsEncoder)
                IsEncoderStarted = false;
            return Missing();
        }
    #endif
    if (Output)
    {
        Output_Bytes.replace("\r\n", "\n"); // As a text stream gives it
        Output->assign(Output_Bytes.constData(), (size_t)Output_Bytes.size());
    }
    else if (!Status)
        IsEncoded = true;
    return Status;
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
    Track.FrameRate = FrameRate_Clean(Source.FrameRate);
    Track.Duration = Source.Duration;
    Track.IsNotLicensed = Source.Problem;
    Track.MayHavePaddingBits = Source.MayHavePaddingBits;
    Track.NeedsCheck = Source.NeedsCheck;
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
static gui_compressed Compressed_From(const compressed& Source)
{
    gui_compressed Compressed;
    Compressed.FileName = FromLocal(Source.FileName);
    Compressed.Name = Compressed.FileName;
    Compressed.HasReversibilityData = !Source.LibraryNameVersion.empty();
    Compressed.HasHashes = Source.HasHashes;
    Compressed.Checked = (gui_compressed::checked)Source.Checked;
    Compressed.OutputDirectory = FromLocal(Source.OutputDirectory);
    return Compressed;
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
    // The library was made for the main thread of a program (other threads have a smaller stack on some systems)
    #if defined(Q_OS_UNIX)
        setStackSize(Stack_Size);
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
bool GUI_Job::Start()
{
    start();

    // What a thread which could not be created says about itself depends on the system
    if (isRunning() || Data->IsStarted)
        return true;
    const QString Message = QStringLiteral("Error: the processing can not be started (the thread can not be created).");
    ExitCode = 1;
    Err = Message + QLatin1Char('\n');
    Messages_Error = QStringList(Message);
    return false;
}

//---------------------------------------------------------------------------
void GUI_Job::run()
{
    Data->IsStarted = true;
    ExitCode = 1;
    IsLicenseIgnored = false;
    Tracks.clear();
    Attachments.clear();
    IsCompressed = false;
    Info.clear();
    Compressed.clear();
    Out.clear();
    Err.clear();
    job_results Results;
    Results.Set(this);

    // The library has process-wide state: a job started while another one runs waits for its end, unless it is canceled
    std::unique_lock<std::timed_mutex> Run_Lock(Run_Mutex, std::defer_lock);
    while (!Run_Lock.try_lock_for(Run_Period))
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
        Data->Console.Out_IsLogged = !Arguments.contains(QLatin1String("--sublicense")) && !Arguments.contains(QLatin1String("--sublicence")); // Else it is a license key
        Console->Target = &Data->Console;
    }
    Console->Format_Reset();
    Job_Running = this;

    // Command line. Characters which are not in the encoding of the library would give it another name: nothing is run
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
    std::vector<message> Recorded;
    if (Message.isEmpty())
    {
        std::unique_ptr<core> Core(new core);
        Core->Global.Ask_Callback = private_data::Ask_Callback;
        Core->Global.Question_Callback = private_data::Question_Callback;
        Core->Global.Run_Callback = private_data::Run_Callback;
        Core->Global.AnalyzeOnly = AnalyzeOnly;
        Core->Global.Metadata_Enabled = Metadata;
        Core->Global.Edits_FileName = ToLocal(Edits_FileName);
        Core->Global.Edits_FileCount = (size_t)Edits_FileCount;
        if (Overwrite)
            Core->Global.OutputOptions["y"] = std::string(); // Only the encoder does not ask, the library still asks its own questions
        Data->Run_Thread = std::this_thread::get_id();
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
        IsLicenseIgnored = Core->Global.IgnoreLicenseKey;
        Tracks.reserve(Core->Output.Streams.size());
        for (const stream& Stream : Core->Output.Streams)
            Tracks.push_back(Track_From(Stream));
        for (const attachment& Attachment : Core->Output.Attachments)
            Attachments += FromLocal(Attachment.FileName_In);
        Compressed.reserve(Core->Output.Compressed.size());
        for (const compressed& Item : Core->Output.Compressed)
            Compressed.push_back(Compressed_From(Item));
        IsCompressed = !Core->Output.Compressed.empty() && Core->Output.Streams.empty();
        if (IsCompressed)
            Info = Info_From(Core->Output.Compressed);
        Recorded = Messages_Recorded(Core->Global.Errors);

        // The core is destroyed here, in this thread, and it may still write to the streams (end of its progress indicator)
        {
            std::lock_guard<std::mutex> Lock(Data->Core_Mutex);
            Data->Core = nullptr;
        }
    }

    // End of the streams for this job
    Job_Running = nullptr;
    QStringList Last;
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
        Last = Data->Console.Messages_Last;
    }

    // Messages. Without any error message the reason of a failure is in what was written last, unless the user is the reason
    Data->Results_Get(Results, Recorded);
    if (Value && Results.Messages[Message_Error].isEmpty() && !Last.isEmpty() && !Data->IsCanceled && !Data->IsRefused)
    {
        Results.Messages[Message_Error] = Last;
        Results.Messages_IsOutput = true;
    }
    Results.Set(this);
    ExitCode = Value;
}

//***************************************************************************
// While running
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Job::phase GUI_Job::Phase() const
{
    console_state* Console = Console_Get();
    std::lock_guard<std::mutex> Lock(Console->Mutex);
    console_target& Target = Data->Console;
    if (Target.Encoder_IsRunning)
        return Phase_Encoder;
    if (Target.Encoder_IsStarted)
        return Phase_Check; // What the library does after the encoder
    if (Console->Target == &Target)
        Target.Progress_Kind(Target.Progress_Text()); // Also the progress line being written
    return Target.Progress_IsContent ? Phase_Check : Phase_Files;
}

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
