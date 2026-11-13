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
    string                      FileName;
    string                      FileName_Template;
    string                      FileName_StartNumber;
    string                      FileName_EndNumber;
    string                      FileList;
    string                      Flavor;
    string                      Slices;
    string                      FrameRate;
    size_t                      FrameCount = 0; // Video frames, 0 if unknown
    double                      Duration = 0; // Duration of the content in seconds, 0 if unknown
    size_t                      StreamCountMinus1 = 0;
    bool                        IsContainer = false;
    bool                        Problem = false;

    // For user interfaces
    uint32_t                    Width = 0; // Video frame size, 0 if unknown
    uint32_t                    Height = 0;
    size_t                      FileCount = 0; // Count of files of the track
    bool                        MayHavePaddingBits = false; // DPX flavor with padding bits (see dpx::MayHavePaddingBits)
    shared_ptr<metadata>        Metadata; // Summary of the metadata of the files of the track, if requested
};
struct attachment
{
    string                      FileName_In; // Complete path
    string                      FileName_Out; // Relative path
};
struct compressed
{
    string                      FileName;
    string                      LibraryNameVersion; // Program having created the reversibility data, empty if there is no reversibility data
    bool                        HasHashes = false; // Hashes of the uncompressed files are in the reversibility data
    size_t                      HashFiles_Count = 0; // Hash files in the attachments
};

class output
{
public:
    // To be filled by external means
    vector<stream>              Streams;
    vector<attachment>          Attachments;
    vector<compressed>          Compressed; // Compressed files which were parsed, for user interfaces

    // Commands
    int Process(global& Global, bool IgnoreReversibilityFile = false);

private:
    int FFmpeg_Command(const char* FileName, global& Global, bool IgnoreReversibilityFile = false);
};

#endif
