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
#include <cstring>
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
// Tabs of the preferences
static const char* const Group_General = QT_TRANSLATE_NOOP("GUI_Options", "General");
static const char* const Group_Checks = QT_TRANSLATE_NOOP("GUI_Options", "Checks");
static const char* const Group_Encoding = QT_TRANSLATE_NOOP("GUI_Options", "Encoding");
static const char* const Group_Advanced = QT_TRANSLATE_NOOP("GUI_Options", "Advanced");

//---------------------------------------------------------------------------
// Texts of the empty fields
static const char* const Empty_Default = QT_TRANSLATE_NOOP("GUI_Options", "Default");
static const char* const Empty_Searched = QT_TRANSLATE_NOOP("GUI_Options", "Default (searched)");
static const char* const Empty_Name = QT_TRANSLATE_NOOP("GUI_Options", "Default name");
static const char* const Empty_None = QT_TRANSLATE_NOOP("GUI_Options", "None");

//---------------------------------------------------------------------------
// The command line takes --bin-name as a libavcodec library when the file has such a name, else as the FFmpeg program
#if defined(_WIN32) || defined(_WINDOWS)
    #define LIBRARY_FILTER QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg libavcodec library (avcodec*.dll);;All files (*)")
    #define LIBRARY_NAME "(?i)avcodec.+\\.dll"
#elif defined(__APPLE__)
    #define LIBRARY_FILTER QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg libavcodec library (libavcodec*.dylib);;All files (*)")
    #define LIBRARY_NAME "libavcodec.+\\.dylib"
#else
    #define LIBRARY_FILTER QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg libavcodec library (libavcodec*.so*);;All files (*)")
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
// Name, group, label, help, type, values, default, operations, text of the empty field
// The arguments are given in this order and the last ones win: the preset before the options it decides, the free options last
static const gui_option Options_List[] =
{
    //-----------------------------------------------------------------------
    // General
    {
        "BinName", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg program (--bin-name)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "FFmpeg program used for the encoding. "
        "By default ffmpeg-ma is searched in the directory of the program, in its ffmpeg, ffmpeg/lib or ffmpeg/bin subdirectory or in PATH, else ffmpeg in the same directories."),
        gui_option::Type_File,
        "--bin-name %1||(?!" LIBRARY_NAME "$).*",
        "", E, Empty_Searched,
    },
    {
        "LibraryName", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg libavcodec library (--bin-name)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "libavcodec library (e.g. avcodec-62.dll, libavcodec.so.62 or libavcodec.62.dylib) used by the ffmpeg and vulkan decoders instead of the searched one. "
        "It is recognized by its file name. "
        "A change is used after a restart of the program if FFmpeg decoders were already used."),
        gui_option::Type_File,
        "--bin-name %1|" LIBRARY_FILTER "|" LIBRARY_NAME,
        "", E | D | C, Empty_Searched,
    },
    {
        "Decoder", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 decoder (--decoder)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "FFV1 decoder used by a decoding, a check and the check after an encoding: internal (RAWcooked decoder), ffmpeg (FFmpeg libavcodec decoder), "
        "vulkan (FFmpeg libavcodec decoder with Vulkan hardware decoding on the default Vulkan device, vulkanN on the Vulkan device N), "
        "a combination of them with + (decoders in parallel, each frame is decoded by the first available decoder; "
        "a device twice, e.g. internal+vulkan+vulkan, is faster with big GPUs), "
        "or auto (internal and vulkan on each discrete GPU, else on the first GPU, if FFmpeg libraries and a Vulkan GPU are available, else internal). "
        "Values with ffmpeg or vulkan need the FFmpeg libavcodec and libavutil libraries. "
        "The default value is auto if the video encoder is ffv1_vulkan, else internal."),
        gui_option::Type_TextChoice,
        "--decoder %1|auto|internal|ffmpeg|vulkan|internal+ffmpeg|internal+vulkan|ffmpeg+vulkan|internal+ffmpeg+vulkan|internal+vulkan+vulkan|internal+vulkan0+vulkan1",
        "", E | D | C, Empty_Default,
    },
    {
        "Prompts", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Questions (existing files, warnings)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Ask: the questions are shown. "
        "Always yes: yes is answered to all the questions, existing files are replaced. "
        "Always no: no is answered to all the questions, existing files are not replaced and an operation with a warning is not done."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Ask") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "Always yes (-y)") "=-y|"
        QT_TRANSLATE_NOOP("GUI_Options", "Always no (-n)") "=-n",
        "", E | D | C, nullptr,
    },
    {
        "Threads", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Threads (-threads)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Count of threads of the FFmpeg encoder and of the decoding. "
        "By default FFmpeg decides for the encoding and the decoding uses all the threads of the processor."),
        gui_option::Type_Number,
        "-threads %1",
        "", E | D | C, Empty_Default,
    },
    {
        "IoThreads", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Threads reading files (--io-threads)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Count of threads which read the files. "
        "By default it comes from the count of threads of the processor."),
        gui_option::Type_Number,
        "--io-threads %1",
        "", A | M | E | D | C, Empty_Default,
    },
    {
        "Quiet", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Less information (--quiet)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "RAWcooked does not write its information lines in the Log view and FFmpeg only writes its warnings and errors. "
        "The progress of an encoding is then not shown: the progress bar only shows that the encoding is running."),
        gui_option::Type_Bool,
        "--quiet|",
        "0", E | D | C, nullptr,
    },
    {
        "ShowStats", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Show statistics about speed, CPU and GPU (--show-stats)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "The Log view gets the CPU and the GPU used and the speed of the encoding and of the decoding, "
        "with the share of each decoder if several decoders are used in parallel (e.g. CPU and GPU)."),
        gui_option::Type_Bool,
        "--show-stats|",
        "0", E | D | C, nullptr,
    },
    {
        "LogName", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Log file (--log-name)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "File written by RAWcooked at the end of an encoding, a decoding or a check, with the versions, the options and the messages of the operation. "
        "The same file is used by all the operations: each one replaces it. "
        "If the questions are answered Always no, a log file which exists is not replaced and the operation ends with an error."),
        gui_option::Type_FileSave,
        "--log-name %1",
        "", E | D | C, QT_TRANSLATE_NOOP("GUI_Options", "No log file"),
    },
    {
        // Also when editing: a file bigger than the maximum size is an error for any run
        "AttachmentMaxSize", Group_General, QT_TRANSLATE_NOOP("GUI_Options", "Maximum size of attachments, in bytes (--attachment-max-size)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "The files of a package which are not audio or video content are attachments, stored in the Matroska file. "
        "A package with such a file of this size or more is not accepted: the file may be content which is not recognized. "
        "The default value is 4194304 (4 MiB)."),
        gui_option::Type_Number,
        "--attachment-max-size %1",
        "", A | M | E, QT_TRANSLATE_NOOP("GUI_Options", "Default (4194304)"),
    },

    //-----------------------------------------------------------------------
    // Checks
    {
        "Preset", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Preset of the checks"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "All checks or Fewest checks sets the options below, down to the hash of the Matroska file, as the preset has them. "
        "Each of them can then be changed: the options are used as they are shown. "
        "With a preset the hash of the Matroska file is always computed, and with All checks the question about missing files is not available."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "No preset") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "All checks (--all)") "=--all|"
        QT_TRANSLATE_NOOP("GUI_Options", "Fewest checks (--none)") "=--none",
        "", E | D | C, nullptr,
    },
    {
        "Check", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Check after encoding"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Quick check: quick coherency checks of the Matroska file, without the time of the full check. "
        "Full check: the Matroska file is decoded and the result is compared with the files of the package; "
        "it is not possible for a package made of several files. "
        "No check: nothing is checked. "
        "With Quick check, RAWcooked does the full check by itself for the formats whose support is recent."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Quick check (--quick-check)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "Full check of reversibility (--check)") "=--check|"
        QT_TRANSLATE_NOOP("GUI_Options", "No check (--no-check)") "=--no-check",
        "", E, nullptr,
    },
    {
        "CheckPadding", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Padding bits (DPX)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Padding bits are bits of some DPX files which are not part of the image, nearly always zero. "
        "Decide from the first file: all the files are checked if the first one has padding bits which are not zero; "
        "else they are checked by the full check after encoding if it is chosen, else the choice is asked before the encoding. "
        "Check all files: slower, the files are restored exactly in all cases. "
        "Do not check: quicker, but the files whose padding bits are not zero are not restored exactly."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Decide from the first file (--quick-check-padding)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "Check all files (--check-padding)") "=--check-padding|"
        QT_TRANSLATE_NOOP("GUI_Options", "Do not check (--no-check-padding)") "=--no-check-padding",
        "", E, nullptr,
    },
    {
        "Coherency", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Check the coherency of the package (--coherency)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Checks that the package is coherent, for example that the audio has the duration of the image sequence, "
        "or that no file is missing in the numbering of a sequence. "
        "A decoding and a check apply it to the files they decode. "
        "This check is partially implemented."),
        gui_option::Type_Bool,
        "|--no-coherency",
        "1", A | E | D | C, nullptr,
    },
    {
        "Conch", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Check the conformance of the files to their format (--conch)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Checks that the files follow the specification of their format, when RAWcooked knows it. "
        "This check is partially implemented, for DPX."),
        gui_option::Type_Bool,
        "--conch|",
        "0", A | E | D | C, nullptr,
    },
    {
        "Hash", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Compute the hashes of the files (--hash)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "During an encoding the hashes of the files of the package are computed and stored in the Matroska file, and the full check uses them. "
        "During a decoding or a check of a Matroska file which has such hashes, the decoded files are compared with them: "
        "the reversibility is checked without the original files."),
        gui_option::Type_Bool,
        "--hash|",
        "0", E | D | C, nullptr,
    },
    {
        "Gaps", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Missing files in a sequence"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "What is done with a sequence whose numbering has gaps. "
        "Ask: a warning lists the missing files, and the encoding is done if the question about the warnings is answered yes. "
        "Accept: the sequence is encoded without warning. "
        "Refuse: the sequence is not encoded, also when the questions are answered Always yes. "
        "The missing files are found by the check of the coherency: without it, the sequence is encoded without warning."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Ask") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "Accept (--accept-gaps)") "=--accept-gaps|"
        QT_TRANSLATE_NOOP("GUI_Options", "Refuse (--no-accept-gaps)") "=--no-accept-gaps",
        "", A | E, nullptr,
    },
    {
        "Info", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Show more information about a Matroska file (--info)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "A decoding, a check and the check after an encoding also say in the Log view which version of RAWcooked made the Matroska file "
        "and whether it has hashes of the original files."),
        gui_option::Type_Bool,
        "--info|",
        "0", E | D | C, nullptr,
    },
    {
        "ComputeOutputHash", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Compute the hash of the Matroska file (--compute-output-hash)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "The MD5 hash of the Matroska file is computed while a decoding, a check or the full check after an encoding reads it, and written in the Log view. "
        "The quick check after an encoding does not compute it. "
        "It is always computed with a preset of the checks."),
        gui_option::Type_Bool,
        "--compute-output-hash|",
        "0", E | D | C, nullptr,
    },
    {
        "FrameMd5", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Write a framemd5 file of the frames of the package (--framemd5)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "During an encoding FFmpeg also writes the MD5 of each frame it reads in a framemd5 file (see the FFmpeg documentation about framemd5). "
        "By default the file is next to the package, with its name followed by .framemd5."),
        gui_option::Type_Bool,
        "--framemd5|",
        "0", E, nullptr,
    },
    {
        "FrameMd5An", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "Write the framemd5 file without the audio (--framemd5-an)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "The audio streams are not in the framemd5 file. "
        "The framemd5 file is then written even if the box above is not checked."),
        gui_option::Type_Bool,
        "--framemd5-an|",
        "0", E, nullptr,
    },
    {
        "FrameMd5Name", Group_Checks, QT_TRANSLATE_NOOP("GUI_Options", "framemd5 file (--framemd5-name)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Name of the framemd5 file, instead of the name of the package followed by .framemd5. "
        "The framemd5 file is then written even if the first of the two boxes above is not checked. "
        "The same file is used by all the encodings: each one replaces it."),
        gui_option::Type_FileSave,
        "--framemd5-name %1",
        "", E, Empty_Name,
    },

    //-----------------------------------------------------------------------
    // Encoding
    {
        // A number which is not 0: the command line reads the numerator only of a fraction for the durations
        "FrameRate", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "Frame rate (-framerate)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Frame rate of the image sequences, a number (e.g. 25 or 23.976). "
        "By default it comes from the metadata of the image files if they have it, else it is 24."),
        gui_option::Type_Text,
        "-framerate %1|0{0,8}[1-9][0-9]{0,8}(\\.[0-9]{1,9})?|0{1,9}\\.[0-9]{0,8}[1-9][0-9]{0,8}",
        "", A | E, Empty_Default,
    },
    {
        "AudioCodec", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "Audio format (-c:a)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Format of the audio in the Matroska file: FLAC (lossless compression), or PCM (the audio is copied as it is). "
        "By default FLAC, and PCM for audio of more than 24 bits, which FLAC does not support."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (FLAC)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "FLAC (-c:a flac)") "=-c:a flac|"
        QT_TRANSLATE_NOOP("GUI_Options", "PCM, not compressed (-c:a copy)") "=-c:a copy",
        "", A | E, nullptr,
    },
    {
        "VideoCodec", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "Video format (-c:v)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Format of the video in the Matroska file: FFV1, made by the FFV1 encoder of FFmpeg (the default one) "
        "or by its Vulkan encoder (hardware acceleration) on the default Vulkan device or on the Vulkan device of the given index."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (FFV1)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "FFV1 (-c:v ffv1)") "=-c:v ffv1|"
        QT_TRANSLATE_NOOP("GUI_Options", "FFV1 with the Vulkan encoder (-c:v ffv1_vulkan)") "=-c:v ffv1_vulkan|"
        QT_TRANSLATE_NOOP("GUI_Options", "FFV1 with the Vulkan encoder on device 1 (-c:v ffv1_vulkan:1)") "=-c:v ffv1_vulkan:1|"
        QT_TRANSLATE_NOOP("GUI_Options", "FFV1 with the Vulkan encoder on device 2 (-c:v ffv1_vulkan:2)") "=-c:v ffv1_vulkan:2",
        "", A | E, nullptr,
    },
    {
        "Coder", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 coder (-coder)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Coder of the FFV1 format: Golomb-Rice, range coder, or range coder with custom state transition table. "
        "The default value is the range coder."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (range coder)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "0: Golomb-Rice") "=-coder 0|"
        QT_TRANSLATE_NOOP("GUI_Options", "1: range coder") "=-coder 1|"
        QT_TRANSLATE_NOOP("GUI_Options", "2: range coder with custom state transition table") "=-coder 2",
        "", E, nullptr,
    },
    {
        "Context", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 context (-context)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Context model of the FFV1 format: small or large."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "0: small") "=-context 0|"
        QT_TRANSLATE_NOOP("GUI_Options", "1: large") "=-context 1",
        "", E, nullptr,
    },
    {
        "Gop", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 GOP size (-g)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Count of frames of a group of pictures of the FFV1 format: with 1 each frame is encoded on its own (strict intra-frame bitstream). "
        "The default value is 1: keep it for archives."),
        gui_option::Type_Number,
        "-g %1",
        "", E, QT_TRANSLATE_NOOP("GUI_Options", "Default (1)"),
    },
    {
        "Level", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 version (-level)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Version of the FFV1 format. "
        "By default the version 3, and the version 1 when the slices are set to 1. "
        "The versions 0 and 1 have neither slices nor slice CRC: with them the slices must be set to 1, else the encoding is refused."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (3, or 1 with 1 slice)") "=|"
        "0=-level 0|1=-level 1|3=-level 3",
        "", E, nullptr,
    },
    {
        "SliceCrc", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 slice CRC (-slicecrc)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "CRC of each slice of a frame of the FFV1 format: a damaged slice is detected. "
        "By default it is on with the version 3; the versions 0 and 1 have no slice CRC."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (on with the version 3)") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "0: off") "=-slicecrc 0|"
        QT_TRANSLATE_NOOP("GUI_Options", "1: on") "=-slicecrc 1",
        "", E, nullptr,
    },
    {
        "Slices", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFV1 slices (-slices)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Count of slices of a frame of the FFV1 format, which are encoded and decoded in parallel (e.g. 4, 6, 9, 16, 24). "
        "By default between 16 and 1024 (384 with FFmpeg older than 8.0) depending on the size and on the bit depth of the frames, "
        "or the default value of FFmpeg with the Vulkan encoder. "
        "With 1 slice and the default version, the version 1 of FFV1 is used: it has no slice CRC."),
        gui_option::Type_Number,
        "-slices %1",
        "", E, Empty_Default,
    },
    {
        "OutputVersion", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "Version of the reversibility data (--output-version)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "The default value is 1. "
        "The value 2 is for big reversibility data, the Matroska file can then not be decoded by RAWcooked older than the version 21.09. "
        "It is a preview: keep the default value unless the reversibility data is too big for the version 1."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (1)") "=|"
        "1=--output-version 1|"
        QT_TRANSLATE_NOOP("GUI_Options", "2: big reversibility data, preview (--output-version 2)") "=--output-version 2",
        "", E, nullptr,
    },
    {
        "LogLevel", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg log level (-loglevel)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Messages FFmpeg writes in the Log view during an encoding. "
        "With another value than the default one, the progress of the encoding is not shown (the progress bar only shows that the encoding is running) "
        "and RAWcooked does not write its information lines, as with Less information."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default") "=|"
        QT_TRANSLATE_NOOP("GUI_Options", "Warnings and errors (-loglevel warning)") "=-loglevel warning|"
        QT_TRANSLATE_NOOP("GUI_Options", "Errors only (-loglevel error)") "=-loglevel error",
        "", E, nullptr,
    },
    {
        // Not an option of the library: the caller builds the command line of the encoding with it
        "MoreOutputOptions", Group_Encoding, QT_TRANSLATE_NOOP("GUI_Options", "FFmpeg options after the output file"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Arguments given as they are to FFmpeg after the name of the Matroska file, e.g. for another output of FFmpeg. "
        "They are separated by spaces, with double quotes around an argument which has spaces. "
        "The license options and --display-command are not accepted here: see Options > License and Actions > Show the FFmpeg command."),
        gui_option::Type_Arguments,
        "",
        "", 0, Empty_None,
    },

    //-----------------------------------------------------------------------
    // Advanced
    {
        "Io", Group_Advanced, QT_TRANSLATE_NOOP("GUI_Options", "Reading method of Matroska files (--io)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "How a Matroska file is read when it is opened, decoded or checked, also by the check after an encoding. "
        "The default method is mmap (the file is mapped in memory)."),
        gui_option::Type_Choice,
        QT_TRANSLATE_NOOP("GUI_Options", "Default (mmap)") "=|"
        "mmap=--io mmap|fstream=--io fstream|fopen=--io fopen|open=--io open" IO_CREATEFILE,
        "", A | M | E | D | C, nullptr,
    },
    {
        "ReversibilityFileName", Group_Advanced, QT_TRANSLATE_NOOP("GUI_Options", "Reversibility data file (--rawcooked-file-name)"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "File in which RAWcooked writes the reversibility data during an encoding, before FFmpeg puts it in the Matroska file; it is then deleted. "
        "By default the name of the Matroska file followed by .rawcooked_reversibility_data. "
        "The same file is used by all the encodings."),
        gui_option::Type_FileSave,
        "--rawcooked-file-name %1",
        "", E, Empty_Name,
    },
    {
        "More", Group_Advanced, QT_TRANSLATE_NOOP("GUI_Options", "Other command line options"),
        QT_TRANSLATE_NOOP("GUI_Options",
        "Other options of the command line of rawcooked (the tab Options of the help lists them), added as they are after the options of the preferences: they have the priority. "
        "They are separated by spaces, with double quotes around an argument which has spaces. "
        "The license options and --display-command are not accepted here: see Options > License and Actions > Show the FFmpeg command."),
        gui_option::Type_Arguments,
        "",
        "", E | D | C, Empty_None,
    },
};
static const size_t Options_Count = sizeof(Options_List) / sizeof(*Options_List);

//---------------------------------------------------------------------------
// Options which a preset of the checks decides
static const struct preset_option
{
    const char*                 Name;
    const char*                 All;            // Value --all gives
    const char*                 None;           // Value --none gives
    const char*                 Default;        // Arguments of the default value after a preset (without preset it has none), null if the command line has none
}
Presets_List[] =
{
    { "Check",              "--check",          "",     "--quick-check" },
    { "CheckPadding",       "--check-padding",  "",     "--quick-check-padding" },
    { "Coherency",          "1",                "0",    "--coherency" },
    { "Conch",              "1",                "0",    "--no-conch" },
    { "Hash",               "1",                "0",    "--no-hash" },
    { "Gaps",               "--accept-gaps",    "",     nullptr }, // No option asks again once --all accepts
    { "Info",               "1",                "0",    "--no-info" },
    { "ComputeOutputHash",  "1",                "1",    nullptr }, // Both presets set it and no option removes it
};

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

//---------------------------------------------------------------------------
static const preset_option* Preset_Find(const gui_option& Option)
{
    for (const preset_option& Decided : Presets_List)
        if (!strcmp(Decided.Name, Option.Name))
            return &Decided;
    return nullptr;
}

//---------------------------------------------------------------------------
// What the arguments of an encoding ask for, read as the command line reads them: the last option wins
struct options_requests
{
    bool                        Check_IsFull = false;
    bool                        CheckPadding_IsSet = false;
    bool                        FrameMd5_IsSet = false;
    QString                     FrameMd5_FileName;
    QString                     Log_FileName;

    explicit options_requests(const QStringList& Arguments)
    {
        for (int i = 0; i < Arguments.size(); i++)
        {
            const QString& Argument = Arguments[i];
            const bool HasValue = i + 1 < Arguments.size();
            if (Argument == QLatin1String("--all"))
            {
                Check_IsFull = true;
                CheckPadding_IsSet = true;
            }
            else if (Argument == QLatin1String("--none") || Argument == QLatin1String("--quick-check") || Argument == QLatin1String("--no-check"))
                Check_IsFull = false;
            else if (Argument == QLatin1String("--check"))
            {
                // Followed by one of these values, it is the former form of the options about the padding bits
                static const char* const Padding[] = { "0", "partial", "1", "full" };
                bool IsPadding = false;
                for (const char* const Value : Padding)
                    if (HasValue && Arguments[i + 1] == QLatin1String(Value))
                        IsPadding = true;
                if (IsPadding)
                {
                    CheckPadding_IsSet = true;
                    i++;
                }
                else
                    Check_IsFull = true;
            }
            else if (Argument == QLatin1String("--check-padding") || Argument == QLatin1String("--no-check-padding"))
                CheckPadding_IsSet = true;
            else if (Argument == QLatin1String("--quick-check-padding"))
                CheckPadding_IsSet = false;
            else if (Argument == QLatin1String("--framemd5") || Argument == QLatin1String("--framemd5-an"))
                FrameMd5_IsSet = true;
            else if (Argument == QLatin1String("--framemd5-name") && HasValue)
            {
                FrameMd5_IsSet = true;
                FrameMd5_FileName = Arguments[++i];
            }
            else if (Argument == QLatin1String("--log-name") && HasValue)
                Log_FileName = Arguments[++i];
        }
    }
};

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
        case gui_option::Type_FileSave: // The last part is taken as is: a regular expression may have the separator
            return QStringList() << Values.section(QLatin1Char('|'), 0, 0) << Values.section(QLatin1Char('|'), 1, 1) << Values.section(QLatin1Char('|'), 2);
        default:
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
bool GUI_Options::Value_IsValid(const gui_option& Option, const QString& Value, QString* Why)
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
            const QStringList Arguments = QProcess::splitCommand(Value);
            for (const QString& Argument : Arguments)
            {
                // The options of the license are for the license management only: a key is never stored with the options
                if (Argument.startsWith(QLatin1String("--")) && (Argument.contains(QLatin1String("license")) || Argument.contains(QLatin1String("licence"))))
                {
                    if (Why)
                        *Why = tr("The license options are not accepted here: the license has its own dialog (Options > License).");
                    return false;
                }

                // The run would show the command instead of doing the operation which is asked
                if (Argument == QLatin1String("--display-command") || Argument == QLatin1String("-d"))
                {
                    if (Why)
                        *Why = tr("%1 is not accepted here: the FFmpeg command has its own menu item (Actions > Show the FFmpeg command).").arg(Argument);
                    return false;
                }
            }
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

//---------------------------------------------------------------------------
QString GUI_Options::Where(const QString& Name)
{
    const gui_option* Option = Find(Name);
    return Option ? tr("%1 > %2").arg(tr(Option->Group), tr(Option->Label)) : QString();
}

//***************************************************************************
// Preset of the checks
//***************************************************************************

//---------------------------------------------------------------------------
const gui_option* GUI_Options::Preset()
{
    static const gui_option* const Option = Find(QStringLiteral("Preset"));
    return Option;
}

//---------------------------------------------------------------------------
QStringList GUI_Options::Preset_Values(const QString& Preset, const gui_option& Option)
{
    QStringList List;
    const preset_option* Decided = Preset.isEmpty() ? nullptr : Preset_Find(Option);
    if (!Decided)
        return List;

    // The value of the preset, then the other ones the command line has arguments for after a preset
    const QString Given = QString::fromLatin1(Preset == QLatin1String("--all") ? Decided->All : Decided->None);
    List += Given;
    QStringList All;
    if (Option.Type == gui_option::Type_Bool)
        All << QStringLiteral("1") << QStringLiteral("0");
    else
    {
        const QStringList Choices = Values_Parts(Option);
        for (const QString& Choice : Choices)
            All += Choice.section(QLatin1Char('='), 1);
    }
    for (const QString& Value : All)
        if (Value != Given && (Value != QLatin1String(Option.Default) || Decided->Default))
            List += Value;
    return List;
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
    if (!Option)
        return QString();

    // Not set: as the preset has it, for an option the preset decides
    if (Option != Preset())
    {
        const QStringList Decided = Preset_Values(Value(QString::fromLatin1(Preset()->Name)), *Option);
        if (!Decided.isEmpty())
            return Decided.first();
    }
    return QString::fromLatin1(Option->Default);
}

//---------------------------------------------------------------------------
void GUI_Options::Value_Set(const QString& Name, const QString& Value)
{
    // The arguments are built from the values: nothing unknown, e.g. from a settings file modified by hand
    const gui_option* Option = Find(Name);
    if (!Option || !Value_IsValid(*Option, Value))
        return;
    if (Option != Preset())
    {
        const QStringList Decided = Preset_Values(this->Value(QString::fromLatin1(Preset()->Name)), *Option);
        if (!Decided.isEmpty() && !Decided.contains(Value))
            return; // No argument of the command line gives this value after the preset
    }

    // Set only if it is not the value the option has without being set
    Values.remove(Name);
    if (Value != this->Value(Name))
        Values[Name] = Value;

    // Other preset: the values which can not follow it are not kept
    if (Option == Preset())
        for (const gui_option& Other : Options_List)
        {
            const QString Other_Name = QString::fromLatin1(Other.Name);
            const QStringList Decided = Preset_Values(Value, Other);
            if (!Decided.isEmpty() && Values.contains(Other_Name) && !Decided.contains(Values[Other_Name]))
                Values.remove(Other_Name);
        }
}

//---------------------------------------------------------------------------
void GUI_Options::Load()
{
    Values.clear();

    // In the order of the table: the preset before the options it decides
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
bool GUI_Options::Save()
{
    options_settings Settings;
    Settings.beginGroup(QStringLiteral("Options"));
    Settings.remove(QString()); // The values which are not set any more are not kept
    for (auto Stored = Values.constBegin(); Stored != Values.constEnd(); ++Stored)
        Settings.setValue(Stored.key(), Stored.value());
    Settings.endGroup();
    Settings.sync();
    return Settings.status() == QSettings::NoError;
}

//---------------------------------------------------------------------------
QString GUI_Options::Settings_FileName()
{
    return QDir::toNativeSeparators(options_settings().fileName());
}

//***************************************************************************
// Arguments
//***************************************************************************

//---------------------------------------------------------------------------
QStringList GUI_Options::Arguments(gui_operation Operation) const
{
    QStringList List;

    // The key of the session first: after a free option which misses its value it would be read as an input, and shown in the messages
    if (!LicenseKey.isEmpty())
        List << QStringLiteral("--license") << LicenseKey;

    // After a preset, the options it decides are given in each of their states: what is run is what the preferences show
    const bool HasPreset = (Preset()->Operations & Operation) && !Value(QString::fromLatin1(Preset()->Name)).isEmpty();

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
        if (HasPreset && Text == QLatin1String(Option.Default))
            if (const preset_option* Decided = Preset_Find(Option))
                Arguments_Add(List, QString::fromLatin1(Decided->Default));
    }

    return List;
}

//***************************************************************************
// What the arguments of an encoding ask for
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Options::Check_IsFull() const
{
    return options_requests(Arguments(Operation_Encode)).Check_IsFull;
}

//---------------------------------------------------------------------------
bool GUI_Options::CheckPadding_IsSet() const
{
    return options_requests(Arguments(Operation_Encode)).CheckPadding_IsSet;
}

//---------------------------------------------------------------------------
bool GUI_Options::FrameMd5_IsSet() const
{
    return options_requests(Arguments(Operation_Encode)).FrameMd5_IsSet;
}

//---------------------------------------------------------------------------
QString GUI_Options::FrameMd5_FileName() const
{
    return options_requests(Arguments(Operation_Encode)).FrameMd5_FileName;
}

//---------------------------------------------------------------------------
QString GUI_Options::Log_FileName() const
{
    return options_requests(Arguments(Operation_Encode)).Log_FileName;
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
