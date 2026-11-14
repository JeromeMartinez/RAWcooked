/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Options.h"
#include <QDir>
#include <QMetaType>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QVariant>
//---------------------------------------------------------------------------

//***************************************************************************
// Options
//***************************************************************************

//---------------------------------------------------------------------------
// Operations an option is given to
static const int A = Operation_Analyze;
static const int M = Operation_Edit;
static const int E = Operation_Encode;
static const int D = Operation_Decode;
static const int C = Operation_Check;

//---------------------------------------------------------------------------
// The command line recognizes a libavcodec library by its file name (same rule here): a file with another name would be taken as the FFmpeg program, and the reverse
#if defined(_WIN32) || defined(_WINDOWS)
    #define LIBRARY_FILTER "FFmpeg libavcodec library (avcodec*.dll);;All files (*)"
    #define LIBRARY_NAME "(?i)avcodec.+\\.dll"
#elif defined(__APPLE__)
    #define LIBRARY_FILTER "FFmpeg libavcodec library (libavcodec*.dylib);;All files (*)"
    #define LIBRARY_NAME "libavcodec.+\\.dylib"
#else
    #define LIBRARY_FILTER "FFmpeg libavcodec library (libavcodec*.so*);;All files (*)"
    #define LIBRARY_NAME "libavcodec.*\\.so.*"
#endif

//---------------------------------------------------------------------------
// File reading method of Windows only
#if defined(_WIN32) || defined(_WINDOWS)
    #define IO_CREATEFILE "|createfile=--io createfile"
#else
    #define IO_CREATEFILE
#endif

//---------------------------------------------------------------------------
// Name, group, label, help, type, values, default, operations
// The arguments are given in the order of the table: a preset first, the free options last, so the last ones have the priority
static const gui_option Options_List[] =
{
    //-----------------------------------------------------------------------
    // General
    {
        "Preset", "General", "Actions preset",
        "--all: same as --info --conch --decode --encode --hash --coherency --check-padding --check --accept-gaps. "
        "--none: same as --no-info --no-conch --no-decode --no-encode --no-hash --no-coherency --quick-check-padding --quick-check. "
        "The action which is run and the options which are not at their default value are added after the preset, so they have the priority; "
        "an option at its default value adds nothing, the preset decides for it.",
        gui_option::Type_Choice,
        "None=|All actions (--all)=--all|No action (--none)=--none",
        "", E | D | C,
    },
    {
        "BinName", "General", "FFmpeg program (--bin-name)",
        "FFmpeg program used for the encoding. "
        "By default ffmpeg-ma is searched in the directory of the program, in its ffmpeg, ffmpeg/lib or ffmpeg/bin subdirectory or in PATH, else ffmpeg in the same directories.",
        gui_option::Type_File,
        "--bin-name %1||(?!" LIBRARY_NAME "$).*",
        "", E,
    },
    {
        "LibraryName", "General", "FFmpeg libavcodec library (--bin-name)",
        "libavcodec library (e.g. avcodec-62.dll, libavcodec.so.62 or libavcodec.62.dylib) used by the ffmpeg and vulkan decoders instead of the searched one. "
        "It is recognized by its file name. "
        "A change is used after a restart of the program if FFmpeg decoders were already used.",
        gui_option::Type_File,
        "--bin-name %1|" LIBRARY_FILTER "|" LIBRARY_NAME,
        "", E | D | C,
    },
    {
        "Prompts", "General", "Questions (existing files, warnings)",
        "Ask: the questions are shown. "
        "Always yes (-y): assume yes in answer to all prompts, existing files are overwritten. "
        "Always no (-n): assume no as answer to all prompts.",
        gui_option::Type_Choice,
        "Ask=|Always yes (-y)=-y|Always no (-n)=-n",
        "", E | D | C,
    },
    {
        "Threads", "General", "Threads (-threads)",
        "Number of threads of the FFmpeg encoder and of the decoding. "
        "By default FFmpeg decides for the encoding and the decoding uses the hardware concurrency of the system.",
        gui_option::Type_Number,
        "-threads %1",
        "", E | D | C,
    },
    {
        "IoThreads", "General", "Threads reading files (--io-threads)",
        "Force the number of IO threads. "
        "The default value is found in the system hardware concurrency, if available, otherwise it will default to 4.",
        gui_option::Type_Number,
        "--io-threads %1",
        "", A | M | E | D | C,
    },
    {
        "Quiet", "General", "Less information (--quiet)",
        "Do not show information related to RAWcooked. "
        "The encoder then shows only its warnings and errors, not its progress.",
        gui_option::Type_Bool,
        "--quiet|",
        "0", E | D | C,
    },
    {
        "ShowStats", "General", "Statistics about speed, CPU and GPU (--show-stats)",
        "Show the CPU and the GPU used, and the speed of the encoding and of the decoding, "
        "with the share of each decoder if several decoders are used in parallel (e.g. CPU and GPU).",
        gui_option::Type_Bool,
        "--show-stats|",
        "0", E | D | C,
    },
    {
        "LogName", "General", "Log file (--log-name)",
        "Name of the RAWcooked log file written after successful processing. "
        "No log file is written by default.",
        gui_option::Type_FileSave,
        "--log-name %1",
        "", E | D | C,
    },
    {
        // Also when editing: a file bigger than the maximum size is an error for any run
        "AttachmentMaxSize", "General", "Maximum size of attachments, in bytes (--attachment-max-size)",
        "Maximum size of the attachments (the files which are not audio-visual content), in bytes. "
        "The default value is 1048576.",
        gui_option::Type_Number,
        "--attachment-max-size %1",
        "", A | M | E,
    },

    //-----------------------------------------------------------------------
    // Checks
    {
        "Check", "Checks", "Check after encoding",
        "Quick check: quick coherency checks of the encoded file, without the additional time taken by the full check. "
        "Full check: check that the output would be the same as the input content. "
        "No check: do not run any check.",
        gui_option::Type_Choice,
        "Quick check (--quick-check)=|Full check of reversibility (--check)=--check|No check (--no-check)=--no-check",
        "", E,
    },
    {
        "CheckPadding", "Checks", "Padding bits (DPX)",
        "Decide from the first file: all files are checked if the first one has padding bits which are not zero; "
        "else they are checked by the full check after encoding if it is requested, else the choice is asked. "
        "Check all files: slower, it guarantees reversibility, the data found in the padding is stored in the reversibility data. "
        "Do not check: quicker, but it may lead to partial reversibility with files that do not conform.",
        gui_option::Type_Choice,
        "Decide from the first file (--quick-check-padding)=|Check all files (--check-padding)=--check-padding|Do not check (--no-check-padding)=--no-check-padding",
        "", E,
    },
    {
        "Coherency", "Checks", "Coherency of the package (--coherency)",
        "Check that the package and contents are coherent, for example that the audio file duration is the same as the image sequence duration, "
        "or that there are no gaps in the sequence numbering. "
        "This is currently partially implemented.",
        gui_option::Type_Bool,
        "|--no-coherency",
        "1", A | E,
    },
    {
        "Conch", "Checks", "Conformance of the files to their format (--conch)",
        "Conformance check of the format, effective only when the format is supported. "
        "This is currently partially implemented for DPX.",
        gui_option::Type_Bool,
        "--conch|",
        "0", A | E | D | C,
    },
    {
        "Hash", "Checks", "Hashes of the files (--hash)",
        "Compute the hash of the audio-visual RAW data files. "
        "During encoding the hashes are stored in the reversibility data, for comparison during the check. "
        "During decoding or checking of a Matroska file with hashes, the decoded files are tested against them: "
        "it permits a reversibility check without the original files.",
        gui_option::Type_Bool,
        "--hash|",
        "0", E | D | C,
    },
    {
        "AcceptGaps", "Checks", "Accept missing files in sequences (--accept-gaps)",
        "Use if there are missing files within the sequence numbering. "
        "RAWcooked creates a concatenated list of all files ensuring the sequence can be encoded.",
        gui_option::Type_Bool,
        "--accept-gaps|",
        "0", A | E,
    },
    {
        "Info", "Checks", "More information about compressed files (--info)",
        "Provide extra information about the compressed file, for example the presence of hashes for the raw data.",
        gui_option::Type_Bool,
        "--info|",
        "0", D | C,
    },
    {
        "ComputeOutputHash", "Checks", "Hash of the output file (--compute-output-hash)",
        "Compute the hash of the output file while checking it.",
        gui_option::Type_Bool,
        "--compute-output-hash|",
        "0", E | D | C,
    },
    {
        "FrameMd5", "Checks", "framemd5 of the input frames (--framemd5)",
        "Compute the framemd5 of the input frames and store it to a sidecar file. "
        "See the FFmpeg framemd5 documentation for more information.",
        gui_option::Type_Bool,
        "--framemd5|",
        "0", E,
    },
    {
        "FrameMd5An", "Checks", "framemd5 without audio (--framemd5-an)",
        "Disable the audio streams in the framemd5 output. "
        "It implies --framemd5.",
        gui_option::Type_Bool,
        "--framemd5-an|",
        "0", E,
    },
    {
        "FrameMd5Name", "Checks", "framemd5 file (--framemd5-name)",
        "Name of the framemd5 file. "
        "It implies --framemd5. "
        "The default name is the name of the input followed by .framemd5.",
        gui_option::Type_FileSave,
        "--framemd5-name %1",
        "", E,
    },

    //-----------------------------------------------------------------------
    // Encoding
    {
        // A number which is not 0, as the command line reads it: it takes only the numerator of a fraction for the durations (the A/V sync is then said incoherent)
        "FrameRate", "Encoding", "Frame rate (-framerate)",
        "Force the video frame rate, a number (e.g. 25 or 23.976). "
        "The default frame rate is found in the image file metadata, if available, otherwise it is 24.",
        gui_option::Type_Text,
        "-framerate %1|0{0,8}[1-9][0-9]{0,8}(\\.[0-9]{1,9})?|0{1,9}\\.[0-9]{0,8}[1-9][0-9]{0,8}",
        "", A | E,
    },
    {
        "AudioCodec", "Encoding", "Audio format (-c:a)",
        "Force the audio encoding format: FLAC, or copy (PCM to PCM, without modification). "
        "The default value is FLAC, and copy for audio of more than 24 bits, which FLAC does not support.",
        gui_option::Type_Choice,
        "Default (FLAC)=|FLAC (-c:a flac)=-c:a flac|PCM, not compressed (-c:a copy)=-c:a copy",
        "", A | E,
    },
    {
        "VideoCodec", "Encoding", "Video format (-c:v)",
        "Force the video encoding format: FFV1 by the FFmpeg encoder, which is the default value, "
        "or by the FFmpeg Vulkan encoder (hardware acceleration) on the default Vulkan device or on the Vulkan device of the given index.",
        gui_option::Type_Choice,
        "Default (FFV1)=|FFV1 (-c:v ffv1)=-c:v ffv1|FFV1 with the Vulkan encoder (-c:v ffv1_vulkan)=-c:v ffv1_vulkan"
        "|FFV1 with the Vulkan encoder on device 1 (-c:v ffv1_vulkan:1)=-c:v ffv1_vulkan:1"
        "|FFV1 with the Vulkan encoder on device 2 (-c:v ffv1_vulkan:2)=-c:v ffv1_vulkan:2",
        "", A | E,
    },
    {
        "Coder", "Encoding", "FFV1 coder (-coder)",
        "If the video encoding format is FFV1, set the coder: 0 (Golomb-Rice), 1 (range coder), 2 (range coder with custom state transition table). "
        "The default value is 1.",
        gui_option::Type_Choice,
        "Default (range coder)=|0: Golomb-Rice=-coder 0|1: range coder=-coder 1|2: range coder with custom state transition table=-coder 2",
        "", E,
    },
    {
        "Context", "Encoding", "FFV1 context (-context)",
        "If the video encoding format is FFV1, set the context: 0 (small), 1 (large).",
        gui_option::Type_Choice,
        "Default=|0: small=-context 0|1: large=-context 1",
        "", E,
    },
    {
        "Gop", "Encoding", "FFV1 GOP size (-g)",
        "If the video encoding format is FFV1, set the GOP size: 1 generates a strict intra-frame bitstream. "
        "The default value is 1. "
        "Ensure you leave the setting at 1 for archival use.",
        gui_option::Type_Number,
        "-g %1",
        "", E,
    },
    {
        "Level", "Encoding", "FFV1 version (-level)",
        "Version of the video encoding format FFV1: 0, 1 or 3. "
        "The default value is the latest version 3. "
        "The versions 0 and 1 do not permit slices: set also the slices to 1.",
        gui_option::Type_Choice,
        "Default (3)=|0=-level 0|1=-level 1|3=-level 3",
        "", E,
    },
    {
        "SliceCrc", "Encoding", "FFV1 slice CRC (-slicecrc)",
        "If the video encoding format is FFV1, set the CRC checksum: 0 (CRC checksums off), 1 (CRC checksum on). "
        "The default value is 1.",
        gui_option::Type_Choice,
        "Default (on)=|0: off=-slicecrc 0|1: on=-slicecrc 1",
        "", E,
    },
    {
        "Slices", "Encoding", "FFV1 slices (-slices)",
        "If the video encoding format is FFV1, set the count of slices of the multithreaded encoding (e.g. 4, 6, 9, 16, 24), 1 for no slices. "
        "The default value is variable between 16 and 1024 (384 with FFmpeg older than 8.0), depending on the video frame size and depth; "
        "it is the FFmpeg default value if the video encoder is ffv1_vulkan.",
        gui_option::Type_Number,
        "-slices %1",
        "", E,
    },
    {
        "OutputVersion", "Encoding", "Version of the reversibility data (--output-version)",
        "The default value is 1. "
        "The value 2 is for big reversibility data, decoding is then not possible with RAWcooked older than version 21.09: "
        "this is a preview release, this value should be used only when you know why you do it.",
        gui_option::Type_Choice,
        "Default (1)=|1=--output-version 1|2: big reversibility data, preview (--output-version 2)=--output-version 2",
        "", E,
    },
    {
        "LogLevel", "Encoding", "FFmpeg log level (-loglevel)",
        "Level of the messages of FFmpeg: warnings and errors, or errors only. "
        "As with --quiet, the information related to RAWcooked and the progress of the encoding are then not shown.",
        gui_option::Type_Choice,
        "Default=|warning=-loglevel warning|error=-loglevel error",
        "", E,
    },
    {
        // Not an option of the library: the caller builds the command line of the encoding with it
        "MoreOutputOptions", "Encoding", "FFmpeg options after the output file",
        "Arguments given as is to FFmpeg after the name of the output file, e.g. for another output of FFmpeg. "
        "The arguments are separated by spaces, with quotes around an argument with spaces. "
        "The options of the license are not accepted here, they have their own dialog.",
        gui_option::Type_Arguments,
        "",
        "", 0,
    },

    //-----------------------------------------------------------------------
    // Decoding
    {
        "Decoder", "Decoding", "FFV1 decoder (--decoder)",
        "FFV1 decoder used for decoding and checking: internal (RAWcooked decoder), ffmpeg (FFmpeg libavcodec decoder), "
        "vulkan (FFmpeg libavcodec decoder with Vulkan hardware decoding on the default Vulkan device, vulkanN on the Vulkan device N), "
        "a combination of them with + (decoders in parallel, each frame is decoded by the first available decoder; "
        "a device twice, e.g. internal+vulkan+vulkan, is faster with big GPUs), "
        "or auto (internal and vulkan on each discrete GPU, else on the first GPU, if FFmpeg libraries and a Vulkan GPU are available, else internal). "
        "Values with ffmpeg or vulkan need the FFmpeg libavcodec and libavutil libraries. "
        "The default value is auto if the video encoder is ffv1_vulkan, else internal.",
        gui_option::Type_TextChoice,
        "--decoder %1|auto|internal|ffmpeg|vulkan|internal+ffmpeg|internal+vulkan|ffmpeg+vulkan|internal+ffmpeg+vulkan|internal+vulkan+vulkan|internal+vulkan0+vulkan1",
        "", E | D | C,
    },

    //-----------------------------------------------------------------------
    // Advanced
    {
        "Io", "Advanced", "File reading method (--io)",
        "Method used for reading the files. "
        "The default value is mmap (files mapped in memory).",
        gui_option::Type_Choice,
        "Default (mmap)=|mmap=--io mmap|fstream=--io fstream|fopen=--io fopen|open=--io open" IO_CREATEFILE,
        "", A | M | E | D | C,
    },
    {
        "ReversibilityFileName", "Advanced", "Reversibility data file (--rawcooked-file-name)",
        "Name of the RAWcooked reversibility data file written during the encoding. "
        "The default name is the name of the output followed by .rawcooked_reversibility_data. "
        "This file is deleted after the encoding if the reversibility data is embedded in the Matroska file.",
        gui_option::Type_FileSave,
        "--rawcooked-file-name %1",
        "", E,
    },
    {
        "More", "Advanced", "Other command line options",
        "Other options of the command line of rawcooked (see the help), added as is after the options above, e.g. --no-conch with the preset --all. "
        "The arguments are separated by spaces, with quotes around an argument with spaces. "
        "The options of the license are not accepted here, they have their own dialog.",
        gui_option::Type_Arguments,
        "",
        "", E | D | C,
    },
};
static const size_t Options_Count = sizeof(Options_List) / sizeof(*Options_List);

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
// Settings of the user interface, in a file of its own next to the configuration file of the library
struct options_settings : QSettings
{
    options_settings()
    : QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("RAWcooked"), QStringLiteral("RAWcooked-GUI"))
    {
        #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            #if QT_CONFIG(textcodec)
                setIniCodec("UTF-8"); // As Qt 6 always does, so the file is the same for both
            #endif
        #endif
    }
};

//---------------------------------------------------------------------------
static QString Settings_Text(const QVariant& Stored)
{
    // A text with commas written by hand without quotes is read as a list
    if (Stored.userType() == QMetaType::QStringList)
        return Stored.toStringList().join(QLatin1Char(','));
    return Stored.toString();
}

//---------------------------------------------------------------------------
// Arguments from a part of the values of an option, the text takes the place of %1 inside its argument
static void Arguments_Add(QStringList& List, const QString& Part, const QString& Text = QString())
{
    const QStringList Arguments = Part.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString Argument : Arguments)
    {
        Argument.replace(QLatin1String("%1"), Text);
        List += Argument;
    }
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Options::GUI_Options()
{
    Load();
}

//***************************************************************************
// Description
//***************************************************************************

//---------------------------------------------------------------------------
const gui_option* GUI_Options::List(size_t& Count)
{
    Count = Options_Count;
    return Options_List;
}

//---------------------------------------------------------------------------
const gui_option* GUI_Options::Find(const QString& Name)
{
    for (const gui_option& Option : Options_List)
        if (Name == QLatin1String(Option.Name))
            return &Option;
    return nullptr;
}

//---------------------------------------------------------------------------
QStringList GUI_Options::Values_Parts(const gui_option& Option)
{
    const QString Values = QString::fromLatin1(Option.Values);
    switch (Option.Type)
    {
        case gui_option::Type_Bool:
        case gui_option::Type_Choice:
        case gui_option::Type_TextChoice:
            return Values.split(QLatin1Char('|'));
        case gui_option::Type_Arguments:
            return QStringList();
        case gui_option::Type_File:
        case gui_option::Type_FileSave: // Arguments, filter, then what the name of the file must be, as is (it may contain the separator)
            return QStringList() << Values.section(QLatin1Char('|'), 0, 0) << Values.section(QLatin1Char('|'), 1, 1) << Values.section(QLatin1Char('|'), 2);
        default: // Arguments then, if any, what the text must be, as is (it may contain the separator)
            return QStringList() << Values.section(QLatin1Char('|'), 0, 0) << Values.section(QLatin1Char('|'), 1);
    }
}

//---------------------------------------------------------------------------
QString GUI_Options::Values_Pattern(const gui_option& Option)
{
    switch (Option.Type)
    {
        case gui_option::Type_Number:
            return QStringLiteral("[1-9][0-9]{0,8}|1[0-9]{9}"); // 0 is refused by the command line or is the same as no value, and the command line can not read more than an int
        case gui_option::Type_Text:
            return Values_Parts(Option).value(1);
        case gui_option::Type_File:
        case gui_option::Type_FileSave:
            return Values_Parts(Option).value(2);
        default:
            return QString();
    }
}

//---------------------------------------------------------------------------
bool GUI_Options::Value_IsValid(const gui_option& Option, const QString& Value)
{
    switch (Option.Type)
    {
        case gui_option::Type_Bool:
            return Value == QLatin1String("0") || Value == QLatin1String("1");
        case gui_option::Type_Choice:
        {
            const QStringList Choices = Values_Parts(Option);
            for (const QString& Choice : Choices)
                if (Choice.section(QLatin1Char('='), 1) == Value)
                    return true;
            return false;
        }
        case gui_option::Type_Arguments:
        {
            // The options of the license are for the license management only: a key is never stored with the options
            const QStringList Arguments = QProcess::splitCommand(Value);
            for (const QString& Argument : Arguments)
                if (Argument.startsWith(QLatin1String("--")) && (Argument.contains(QLatin1String("license")) || Argument.contains(QLatin1String("licence"))))
                    return false;
            return true;
        }
        default:
        {
            const QString Pattern = Values_Pattern(Option);
            if (Value.isEmpty() || Pattern.isEmpty())
                return true;
            QString Text = Value;
            if (Option.Type == gui_option::Type_File || Option.Type == gui_option::Type_FileSave)
                Text = QDir::fromNativeSeparators(Text).section(QLatin1Char('/'), -1); // Name of the file, without its directory
            return QRegularExpression(QRegularExpression::anchoredPattern(Pattern)).match(Text).hasMatch();
        }
    }
}

//***************************************************************************
// Values
//***************************************************************************

//---------------------------------------------------------------------------
QString GUI_Options::Value(const QString& Name) const
{
    const auto Stored = Values.constFind(Name);
    if (Stored != Values.constEnd())
        return Stored.value();
    const gui_option* Option = Find(Name);
    return Option ? QString::fromLatin1(Option->Default) : QString();
}

//---------------------------------------------------------------------------
void GUI_Options::Value_Set(const QString& Name, const QString& Value)
{
    // The arguments are built from the values: nothing unknown, e.g. from a settings file modified by hand
    const gui_option* Option = Find(Name);
    if (!Option || !Value_IsValid(*Option, Value))
        return;

    if (Value == QLatin1String(Option->Default))
        Values.remove(Name);
    else
        Values[Name] = Value;
}

//---------------------------------------------------------------------------
bool GUI_Options::IsDefault(const QString& Name) const
{
    return !Values.contains(Name);
}

//---------------------------------------------------------------------------
void GUI_Options::Defaults_Restore()
{
    Values.clear();
}

//---------------------------------------------------------------------------
void GUI_Options::Load()
{
    Values.clear();

    options_settings Settings;
    Settings.beginGroup(QStringLiteral("Options"));
    for (const gui_option& Option : Options_List)
    {
        const QString Name = QString::fromLatin1(Option.Name);
        if (Settings.contains(Name))
            Value_Set(Name, Settings_Text(Settings.value(Name)));
    }
}

//---------------------------------------------------------------------------
void GUI_Options::Save()
{
    options_settings Settings;
    Settings.beginGroup(QStringLiteral("Options"));
    Settings.remove(QString()); // The values which are back to their default are not kept
    for (auto Stored = Values.constBegin(); Stored != Values.constEnd(); ++Stored)
        Settings.setValue(Stored.key(), Stored.value());
    Settings.endGroup();
    Settings.sync();
}

//***************************************************************************
// Arguments
//***************************************************************************

//---------------------------------------------------------------------------
QStringList GUI_Options::Arguments(gui_operation Operation) const
{
    QStringList List;

    // The key of the session, the license management handles it itself
    // First: free options which miss a value would take what follows them, the key would then be an input and be shown in the messages
    if (!LicenseKey.isEmpty() && Operation != Operation_License)
        List << QStringLiteral("--license") << LicenseKey;

    for (const gui_option& Option : Options_List)
    {
        if (!(Option.Operations & Operation))
            continue;

        const QString Text = Value(QString::fromLatin1(Option.Name));
        switch (Option.Type)
        {
            case gui_option::Type_Bool:
                Arguments_Add(List, Values_Parts(Option).value(Text == QLatin1String("1") ? 0 : 1));
                break;
            case gui_option::Type_Choice:
                Arguments_Add(List, Text);
                break;
            case gui_option::Type_Arguments:
                List += QProcess::splitCommand(Text);
                break;
            default:
                if (!Text.isEmpty())
                    Arguments_Add(List, Values_Parts(Option).value(0), Text);
        }
    }

    return List;
}

//***************************************************************************
// Other settings of the user interface
//***************************************************************************

//---------------------------------------------------------------------------
QString GUI_Options::Directory_Get() const
{
    return Settings_Text(options_settings().value(QStringLiteral("Interface/Directory")));
}

//---------------------------------------------------------------------------
void GUI_Options::Directory_Set(const QString& Directory)
{
    options_settings().setValue(QStringLiteral("Interface/Directory"), Directory);
}
