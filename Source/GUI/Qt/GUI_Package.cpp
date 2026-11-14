/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Package.h"
#include <QDir>
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
// Path with / as separator and without separator at the end (the root / is kept)
static QString Path_Clean(const QString& Path)
{
    QString Result = QDir::fromNativeSeparators(Path);
    while (Result.size() > 1 && Result.endsWith(QLatin1Char('/')))
        Result.chop(1);
    return Result;
}

//---------------------------------------------------------------------------
// Last part of a path (name of a file or of a directory), the path itself if it has no name (root)
static QString Path_Name(const QString& Path)
{
    QString Result = Path_Clean(Path);
    const auto Pos = Result.lastIndexOf(QLatin1Char('/'));
    if (Pos < 0 || Pos + 1 >= Result.size())
        return Result;
    return Result.mid(Pos + 1);
}

//---------------------------------------------------------------------------
// Name of a file of a package (or template of the names of a sequence), relative to the package
static QString Name_Relative(const QString& Name, const gui_package& Package)
{
    // Files: the names are shown without their directory
    if (!Package.IsDirectory || Package.Inputs.isEmpty())
        return Path_Name(Name);

    // Directory: the library names the files from the directory as it was given
    QString Result = QDir::fromNativeSeparators(Name);
    QString Directory = Path_Clean(Package.Inputs.first());
    if (!Directory.endsWith(QLatin1Char('/')))
        Directory += QLatin1Char('/');
    #if defined(Q_OS_WIN)
        const Qt::CaseSensitivity Sensitivity = Qt::CaseInsensitive;
    #else
        const Qt::CaseSensitivity Sensitivity = Qt::CaseSensitive;
    #endif
    if (Result.size() > Directory.size() && Result.startsWith(Directory, Sensitivity))
        return Result.mid(Directory.size());
    return Result; // Not in the directory, the whole name is the only one which is not ambiguous
}

//***************************************************************************
// gui_field
//***************************************************************************

//---------------------------------------------------------------------------
QString gui_field::Display() const
{
    if (IsModified)
        return NewValue;
    switch (Kind)
    {
        case Kind_Sequence: return First + QStringLiteral(" --> ") + Last; // As the library shows the numbers of a sequence of files
        case Kind_Mixed:    return First + QStringLiteral(" ... ") + Last;
        default:            return First;
    }
}

//***************************************************************************
// gui_track
//***************************************************************************

//---------------------------------------------------------------------------
bool gui_track::IsModified() const
{
    for (const gui_field& Field : Fields)
        if (Field.IsModified)
            return true;
    return false;
}

//---------------------------------------------------------------------------
size_t gui_track::Modified_Count() const
{
    size_t Count = 0;
    for (const gui_field& Field : Fields)
        if (Field.IsModified)
            Count++;
    return Count;
}

//***************************************************************************
// gui_package
//***************************************************************************

//---------------------------------------------------------------------------
QString gui_package::Name() const
{
    if (Inputs.isEmpty())
        return QString();
    QString Result = Path_Name(Inputs.first());
    if (!IsDirectory && Inputs.size() > 1)
        Result += QStringLiteral(" (+%1)").arg(Inputs.size() - 1);
    return Result;
}

//---------------------------------------------------------------------------
void gui_package::Content_Set(const std::vector<gui_track>& Tracks_, const QStringList& Attachments_)
{
    // In copies first: the arguments may be the content of this package
    std::vector<gui_track> Tracks_New = Tracks_;
    for (gui_track& Track : Tracks_New)
        Track.Name = Name_Relative(Track.Name, *this); // FileName stays as the library gave it, it identifies the track for the library
    QStringList Attachments_New;
    Attachments_New.reserve(Attachments_.size());
    for (const QString& Attachment : Attachments_)
        Attachments_New.push_back(Name_Relative(Attachment, *this));

    Tracks.swap(Tracks_New);
    Attachments.swap(Attachments_New);
}

//---------------------------------------------------------------------------
bool gui_package::IsModified() const
{
    for (const gui_track& Track : Tracks)
        if (Track.IsModified())
            return true;
    return false;
}

//---------------------------------------------------------------------------
size_t gui_package::Modified_Count() const
{
    size_t Count = 0;
    for (const gui_track& Track : Tracks)
        Count += Track.Modified_Count();
    return Count;
}
