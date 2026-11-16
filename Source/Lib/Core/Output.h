/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef OutputH
#define OutputH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include "Lib/Core/Config.h"
#include "Lib/Core/Global.h"
#include "Lib/Core/Metadata.h"
#include <memory>
#include <string>
#include <vector>
using namespace std;
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
struct stream
{
    string                      FileName; // First file of the track
    string                      FileName_Template; // Sequence: names of its files with the pattern of their number (e.g. Dir/Name_%06d.dpx), else empty
    string                      FileName_StartNumber; // Sequence: number of the first file as in its name (e.g. 000001), else empty
    string                      FileName_EndNumber; // Sequence: number of the last file
    string                      FileList; // Not empty if the files of the sequence are not all the files from the first number to the last one on the disk (gaps, or a single file of a longer sequence): their names, replaced by the list given to the encoder when it is run
    string                      Flavor; // e.g. DPX/Raw/RGB/10bit/U/BE/FilledA, empty if it is not supported (accepted when not encoding)
    string                      Slices; // Video: count of slices for the encoder, 0 for the default of the encoder; empty if no video
    string                      FrameRate; // The one of -framerate if provided, else the one of the video files, empty if unknown
    size_t                      FrameCount = 0; // Video frames, 0 if unknown
    double                      Duration = 0; // Duration of the content in seconds, 0 if unknown
    size_t                      StreamCountMinus1 = 0; // Container: count of its streams, minus 1
    bool                        IsContainer = false; // File with several streams (AVI)
    bool                        Problem = false; // The flavor is not in the license (tested when encoding)

    // For user interfaces
    uint32_t                    Width = 0; // Video frame size, 0 if unknown
    uint32_t                    Height = 0;
    size_t                      FileCount = 0; // Count of files of the track
    bool                        MayHavePaddingBits = false; // DPX flavor with padding bits (see dpx::MayHavePaddingBits)
    bool                        NeedsCheck = false; // The content is encoded only with the check of reversibility (--check)
    shared_ptr<metadata>        Metadata; // Summary of the metadata of the files of the track, if requested
};
struct attachment
{
    string                      FileName_In; // Complete path
    string                      FileName_Out; // Relative path
};
struct compressed
{
    enum class checked : uint8_t
    {
        No,
        Decoding, // The decoding was checked
        Reversibility, // The decoded content was compared with the hashes or the source files
    };

    string                      FileName;
    string                      LibraryNameVersion; // Program having created the reversibility data, empty if there is no reversibility data
    bool                        HasHashes = false; // Hashes of the uncompressed files are in the reversibility data
    size_t                      HashFiles_Count = 0; // Hash files in the attachments
    checked                     Checked = checked::No; // What the run checked for this file without detecting an issue
    string                      OutputDirectory; // Directory of the files decoded by the run (no path separator at its end, except for a root), empty if nothing was decoded
};

class output
{
public:
    // To be filled by external means
    vector<stream>              Streams;
    vector<attachment>          Attachments;
    vector<compressed>          Compressed; // Compressed files which were parsed, for user interfaces: the ones of the inputs, and the output of an encoding if it is checked (not with the quick check, which does not read it)

    // Commands
    int Process(global& Global, bool IgnoreReversibilityFile = false);

private:
    int FFmpeg_Command(const char* FileName, global& Global, bool IgnoreReversibilityFile = false);
};

#endif
