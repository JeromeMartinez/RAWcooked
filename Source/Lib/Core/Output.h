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
};
struct attachment
{
    string                      FileName_In; // Complete path
    string                      FileName_Out; // Relative path
};

class output
{
public:
    // To be filled by external means
    vector<stream>              Streams;
    vector<attachment>          Attachments;

    // Commands
    int Process(global& Global, bool IgnoreReversibilityFile = false);

private:
    int FFmpeg_Command(const char* FileName, global& Global, bool IgnoreReversibilityFile = false);
};

#endif
