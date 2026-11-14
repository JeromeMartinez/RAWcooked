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
    int                         Size = 0;           // Count of bytes of the field in the files (Text: max count of bytes of the value; Unsigned: the value must fit in them), 0 if it is not a field of the files
    bool                        IsEditable = false;
    bool                        IsStructural = false; // The content can not be read with a wrong value
    QString                     First;              // Value in the first file, empty if not set
    QString                     Last;               // Value in the last file
    kind                        Kind = Kind_Same;

    // Edition, not yet written in the files
    bool                        IsModified = false;
    QString                     NewValue;           // Value for all files

    // Text shown for the value: the new value if modified, else the value if it is the same in all files,
    // else "first --> last" (sequence) or "first ... last" (other)
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
    quint64                     FrameCount = 0;     // Video: 0 if unknown
    QString                     FrameRate;          // Empty if unknown
    double                      Duration = 0;       // In seconds, 0 if unknown
    bool                        IsNotLicensed = false; // Flavor not supported by the license key
    bool                        MayHavePaddingBits = false; // Padding bits of the content may be not zero, a choice about their check is needed for encoding
    bool                        IsMetadataComplete = true; // False if the metadata of some files could not be read
    std::vector<gui_field>      Fields;

    bool                        IsModified() const; // At least a field is modified
    size_t                      Modified_Count() const;
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
    QStringList                 Info;               // Compressed content: information lines
    QString                     Status;             // Result of the last operation, in one line
    bool                        IsError = false;    // The last operation failed
    QString                     Messages;           // Messages of the last operation (errors, warnings, information), may be several lines

    QString                     Name() const;       // Name shown for the package (the directory or the file names)
    void                        Content_Set(const std::vector<gui_track>& Tracks_, const QStringList& Attachments_); // Content as found by the library, names are made relative to the package
    bool                        IsModified() const; // At least a field of a track is modified
    size_t                      Modified_Count() const;
};

#endif
