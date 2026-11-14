/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_Main_MetadataH
#define GUI_Main_MetadataH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QWidget>
#include <QSet>
#include <QString>
#include <cstddef>
class GUI_Main;
class GUI_Main_Metadata_Delegate;
class GUI_Main_Metadata_Tree;
class QLabel;
class QLineEdit;
class QTreeWidgetItem;
struct gui_field;
struct gui_track;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Main_Metadata: the metadata of each track, one value per field for
// all the files of the track, editable
//***************************************************************************

class GUI_Main_Metadata : public QWidget
{
    Q_OBJECT

public:
    explicit GUI_Main_Metadata(GUI_Main* Main);

    // Content from Main->Packages
    void                        Refresh();

    // The track is made visible (expanded, scrolled to)
    void                        Track_Show(size_t Package, size_t Track);

Q_SIGNALS:
    void                        Modified(); // A value was edited or reverted

private:
    GUI_Main*                   Main;

    // Widgets
    QLineEdit*                  Filter;
    QLabel*                     Help;
    GUI_Main_Metadata_Tree*     Tree;
    friend class GUI_Main_Metadata_Delegate;
    friend class GUI_Main_Metadata_Tree;

    // State
    QSet<QString>               Collapsed;              // Tracks and groups closed by the user, they stay closed after a refresh
    QString                     Track_ToShow;           // Track to scroll to when the view is shown (it was hidden when asked)
    bool                        Columns_Fitting = false; // Widths are being set by the view, not by the user
    bool                        Columns_User_Field = false; // Width set by the user: it is kept, else the widths follow the content and the size of the view
    bool                        Columns_User_Files = false;
    int                         Columns_Wanted_Field = 0; // Width the names of the fields need
    int                         Columns_Wanted_Value = 0; // Width the values need
    int                         Columns_Wanted_Files = 0; // Width the last column needs for the fields and for the tracks
    int                         Columns_Wanted_Files_Fields = 0; // Width the last column needs for the fields only
    bool                        Updating = false;       // The tree is changed by the view, not by the user
    size_t                      Generation = 0;         // Count of refreshes: the items of the tree are other ones after a refresh

    // Content of an item, nullptr if it is not the one of the last refresh
    gui_track*                  Track_Get(const QTreeWidgetItem* Item) const; // Item of a track, of a group or of a field
    gui_field*                  Field_Get(const QTreeWidgetItem* Item) const; // Item of a field

    // Items
    QTreeWidgetItem*            Item_Find(size_t Package, size_t Track) const; // Item of a track, nullptr if it has none
    void                        Item_Field_Update(QTreeWidgetItem* Item); // Texts and look of a field
    void                        Item_Track_Update(QTreeWidgetItem* Item); // Texts of a track and of its groups (items of fields are not changed)

    // Edition
    bool                        Field_Edit(QTreeWidgetItem* Item, const QString& Text, bool IsDeliberate); // Text from the editor; IsDeliberate: the user typed it or validated it (else the editor was just closed)
    void                        Field_Revert(QTreeWidgetItem* Item);
    void                        Track_Revert(QTreeWidgetItem* Item);
    bool                        Field_Copy(QTreeWidgetItem* Item);
    void                        Menu_Show(QTreeWidgetItem* Item, const QPoint& Position);

    // View
    void                        Filter_Apply();
    void                        Columns_Measure();
    void                        Columns_Fit();
    void                        showEvent(QShowEvent* Event) override;
};

#endif
