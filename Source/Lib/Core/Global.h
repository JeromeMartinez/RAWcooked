/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GlobalH
#define GlobalH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include "Lib/Core/Config.h"
#include "Lib/Common/Common.h"
#include "Lib/Uncompressed/HashSum/HashSum.h"
#include "Lib/License/License.h"
#include <atomic>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <thread>
#include <bitset>
using namespace std;
//---------------------------------------------------------------------------

//--------------------------------------------------------------------------
class global
{
public:
    // Constructor/Destructor
                                ~global();

    // Options
    map<string, string>         VideoInputOptions;
    map<string, string>         OutputOptions;
    vector<string>              MoreOutputOptions;
    map<string, string>         Edits;
    bool                        Edits_Enabled = false;
    bool                        Edits_Help = false;
    size_t                      IoThreads = 0;
    size_t                      AttachmentMaxSize = (size_t)-1;
    string                      rawcooked_reversibility_FileName;
    string                      OutputFileName;
    string                      FrameMd5FileName;
    string                      LogFileName;
    string                      BinName;
    string                      LicenseKey;
    uint64_t                    SubLicenseId = 0;
    uint64_t                    SubLicenseDur = 1;
    filemap::method             FileOpenMethod = filemap::method::mmap;
    bool                        IgnoreLicenseKey = false;
    bool                        ShowLicenseKey = false;
    bool                        StoreLicenseKey = false;
    bool                        DisplayCommand = false;
    bool                        AcceptFiles = false;
    bool                        HasAtLeastOneDir = false;
    bool                        HasAtLeastOneFile = false;
    bool                        OutputFileName_IsProvided = false;
    bool                        Decoder_IsProvided = false;
    bool                        Quiet = false;
    bool                        ShowStats = false;
    bitset<Action_Max>          Actions;
    vector<size_t>              VulkanDecoders; // Index of the Vulkan device of each vulkan decoder

    // Options not from the command line
    bool                        AnalyzeOnly = false; // Inputs are analyzed as for an encoding but nothing of the encoding is written or run (no reversibility data, no encoder, no check), and the choices needed for a real encoding are not requested. Other actions of the command line (decoding of a compressed input, edits, log file) are not changed: an analysis is requested with --info --encode
    bool                        Metadata_Enabled = false; // The summary of the metadata of each track is computed (Output.Streams[].Metadata)
    string                      Edits_FileName; // If not empty, edits are only for the track whose first file has this name (as in Output.Streams[].FileName), the run fails if no track takes them

    // Intermediate info
    size_t                      Path_Pos_Global = 0;
    vector<string>              Inputs;
    set<int>                    LogFile_IgnorePos;
    license                     License;
    user_mode                   Mode = Ask;
    hashes*                     Hashes = nullptr;
    errors                      Errors;
    ask_callback                Ask_Callback = nullptr;
    question_callback           Question_Callback = nullptr;
    run_callback                Run_Callback = nullptr;
    string*                     Log = nullptr;

    // Conformance check intermediary info
    vector<double>              Durations;
    vector<string>              Durations_FileName;

    // Options
    int ManageCommandLine(const char* argv[], int argc);
    int SetDefaults();

    // FFmpeg
    const string& FFmpeg_Version(); // Version of the FFmpeg binary, empty if it can not be run
    bool FFmpeg_LimitedSlices(); // True if the FFmpeg binary is older than FFmpeg 8.0 (its FFV1 encoder does not accept 2x more slices per line than per column or 32 slices per column), false if unknown
    bool CheckFFmpeg(bool IsCompressed); // False if the FFmpeg binary (encoding) or the FFmpeg libraries (FFmpeg decoders) are needed and not available, FFmpeg_Error is then set
    string FFmpeg_Error;

    // Stats
    static string CPU_Name(); // Name of the CPU with its count of threads

    // Progress indicator
    void                        ProgressIndicator_Start(size_t Total);
    void                        ProgressIndicator_Increment() { ProgressIndicator_Current++; Progress.Current++; }
    void                        ProgressIndicator_Stop();
    condition_variable          ProgressIndicator_IsEnd;
    bool                        ProgressIndicator_IsPaused = false;

    // Progress and cancellation, for user interfaces
    progress                    Progress; // Analysis of the inputs: files, counted as the progress indicator does (a file is counted by each parser trying it, except the files of a sequence after the first one, so Current may be much more than Total, and both restart if a message was shown before a sequence); parsing of a compressed file: bytes of the file
    atomic<bool>                Cancel{ false }; // Set (maybe from another thread) for stopping the processing as soon as possible, the run then fails; tested before each file and before each block of a compressed file, what is between two tests is not interrupted (e.g. listing of the inputs, hash of a big file, external program)

    // Theading relating functions
    void                        ProgressIndicator_Show();

    int SetCheck(bool Value);
    int SetVersion(const char* Value);
    int SetOption(const char* argv[], int& i, int argc);
    int SetOutputFileName(const char* FileName);
    int SetComputeOutputHash();
    int SetBinName(const char* FileName);
    int SetLicenseKey(const char* Key, bool Add);
    int SetSubLicenseId(uint64_t Id);
    int SetSubLicenseDur(uint64_t Dur);
    int SetDisplayCommand();
    int SetAcceptFiles();
    int SetCheck(const char* Value, int& i);
    int SetQuickCheck();
    int SetCheckPadding(bool Value);
    int SetQuickCheckPadding();
    int SetAcceptGaps(bool Value);
    int SetCoherency(bool Value);
    int SetConch(bool Value);
    int SetDecode(bool Value);
    int SetDecoder(const char* Value);
    int SetEdit(const char* Value);
    int SetEncode(bool Value);
    int SetInfo(bool Value);
    int SetFrameMd5(bool Value);
    int SetFrameMd5An(bool Value);
    int SetFrameMd5FileName(const char* FileName);
    int SetHash(bool Value);
    int SetFileOpenMethod(const char* Value);
    int SetLogFileName(const char* FileName);
    int SetAll(bool Value);

private:
    // Progress indicator
    thread*                     ProgressIndicator_Thread = nullptr;
    atomic<size_t>              ProgressIndicator_Current{ 0 };
    size_t                      ProgressIndicator_Total = 0;

    // FFmpeg binary
    string                      FFmpeg_Version_Value;
    uint32_t                    FFmpeg_Libavcodec_Version = 0; // As AV_VERSION_INT, 0 if unknown
    bool                        FFmpeg_Version_IsComputed = false;
    bool                        BinName_IsProvided = false;

    // Temp
    string                      Decoder_Value;
    bool                        NextFileNameIsOutput = false;
    bool                        OptionsForOtherFiles = false;
};

#endif
