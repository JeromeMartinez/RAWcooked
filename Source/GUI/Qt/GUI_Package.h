/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_PackageH
#define GUI_PackageH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QString>
#include <QStringList>
#include <vector>
//---------------------------------------------------------------------------

//***************************************************************************
// Content shown by the views: what was opened and what the library found in it
//***************************************************************************

//---------------------------------------------------------------------------
// Metadata field of a track, summarized for all the files of the track
struct gui_field
{
    enum type // Same values as metadata_field::type in the library
    {
        Type_Text,
        Type_Unsigned,
        Type_Float,
        Type_TimeCode,
    };
    enum kind // Same values as metadata::kind in the library
    {
        Kind_Same,      // Same value in all files
        Kind_Sequence,  // Values never decrease from a file to the next one (e.g. frame number, date)
        Kind_Mixed,     // Other
    };

    QString                     Group;
    QString                     Name;               // Also the key for editing. Unique in its group and among the editable fields of a track, not among all its fields
    QString                     Description;        // Unit or format of the value, may be empty
    type                        Type = Type_Text;
    int                         Size = 0;           // Bytes of the field in the files (maximum for a text, a number must fit in them), 0 if not a field of the files
    bool                        IsEditable = false;
    bool                        IsStructural = false; // The content can not be read with a wrong value
    QString                     First;              // Value in the first file, empty if not set
    QString                     Last;               // Value in the last file
    kind                        Kind = Kind_Same;

    // Edition, not yet written in the files
    bool                        IsModified = false;
    QString                     NewValue;           // Value for all files

    // Text shown for the value: the new value if modified, else the value if it is the same in all files,
    // else "first --> last" (sequence) or "first ... last" (other), with "(not set)" for a side without value
    QString                     Display() const;
    bool                        HasValue() const { return IsModified ? !NewValue.isEmpty() : (!First.isEmpty() || !Last.isEmpty()); }
};

//---------------------------------------------------------------------------
// Track: a sequence of files (e.g. DPX) or a file (e.g. WAV, AVI)
struct gui_track
{
    QString                     FileName;           // First file, as the library names it (identifies the track for the edits)
    QString                     Name;               // File name, or template of the names of a sequence (e.g. Reel1/%06d.dpx), relative to the package
    QString                     Numbers;            // Sequence: first and last numbers and gaps if any (e.g. "000001 --> 000013"), else empty
    QString                     Flavor;             // e.g. DPX/Raw/RGB/10bit/U/BE/FilledA
    quint32                     Width = 0;          // Video: frame size, 0 if unknown
    quint32                     Height = 0;
    quint64                     FileCount = 0;
    QString                     FrameRate;          // Empty if unknown
    double                      Duration = 0;       // In seconds, 0 if unknown
    bool                        IsNotLicensed = false; // Flavor not supported by the license key
    bool                        MayHavePaddingBits = false; // Padding bits of the content may be not zero, a choice about their check is needed for encoding
    bool                        NeedsCheck = false; // The content is encoded only with the full check after the encoding
    bool                        IsMetadataComplete = true; // False if the metadata of some files could not be read
    std::vector<gui_field>      Fields;

    bool                        IsModified() const; // At least a field is modified
    size_t                      Modified_Count() const;
};

//---------------------------------------------------------------------------
// Compressed file (Matroska)
struct gui_compressed
{
    enum checked // Same values as compressed::checked in the library
    {
        Checked_No,
        Checked_Decoding,       // The decoding was checked
        Checked_Reversibility,  // The decoded content was compared with the hashes or the original files
    };

    QString                     FileName;           // As the library names it
    QString                     Name;               // Relative to the package
    bool                        HasReversibilityData = false; // False: not created by RAWcooked, it can not be decoded to the original files
    QString                     LibraryNameVersion; // Program which created the reversibility data (e.g. RAWcooked 25.12), empty if there is none
    bool                        HasHashes = false;  // Hashes of the original files are in the reversibility data
    quint64                     HashFiles_Count = 0; // Hash files among the attachments, counted by an analysis (0 after a check or a decoding)
    checked                     Checked = Checked_No; // What the last operation checked for this file without detecting an issue: Checked_No if it checked nothing or found issues
    QString                     OutputDirectory;    // Directory of the files decoded by the last operation (no separator at its end), empty if nothing was decoded (also with a check)
};

//---------------------------------------------------------------------------
// Package: what was opened in one go (a directory, or one or several files), the unit of work of the library
struct gui_package
{
    enum kind
    {
        Kind_Unknown,           // Not analyzed or analysis failed
        Kind_Uncompressed,      // Content to encode (tracks and attachments)
        Kind_Compressed,        // Matroska file(s) to decode or check
    };

    QStringList                 Inputs;             // A directory, or one or several files (native separators)
    bool                        IsDirectory = false;
    kind                        Kind = Kind_Unknown;
    std::vector<gui_track>      Tracks;             // Uncompressed content
    QStringList                 Attachments;        // Uncompressed content: other files, relative to the package
    std::vector<gui_compressed> Compressed;         // Matroska files of the package; in an uncompressed package they are not encoded with it
    bool                        IsLicenseIgnored = false; // Tracks which are not in the license are encoded anyway
    bool                        IsNotEncodable = false; // Uncompressed content which is shown but refused by an encoding, Status says why
    QString                     Status;             // Result of the last operation, in one line
    bool                        IsError = false;    // The last operation failed
    bool                        HasWarnings = false; // The last operation gave warnings, they are in Messages
    QString                     Messages;           // Messages of the last operation (errors, warnings, information), may be several lines

    QString                     Name() const;       // Name shown for the package (the directory or the file names)
    // Content as found by the library, the names are made relative to the package
    void                        Content_Set(const std::vector<gui_track>& Tracks_, const QStringList& Attachments_); // Also the names of Compressed
    void                        Compressed_Set(const std::vector<gui_compressed>& Compressed_);
    bool                        IsModified() const; // At least a field of a track is modified
    size_t                      Modified_Count() const;

    static Qt::CaseSensitivity  Path_Sensitivity(); // Of the names of files on this system
};

#endif
