/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_Main_FilesH
#define GUI_Main_FilesH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QTreeWidget>
#include <QIcon>
#include <cstddef>
#include <vector>
class GUI_Main;
class QAbstractScrollArea;
class QStyleOptionViewItem;
struct gui_track;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Main_Files: the opened packages with their tracks and attachments
//***************************************************************************

class GUI_Main_Files : public QTreeWidget
{
    Q_OBJECT

public:
    explicit GUI_Main_Files(GUI_Main* Main);

    // Content from Main->Packages (the selection is kept if possible)
    void                        Refresh();

    // Selection
    std::vector<size_t>         Selection() const; // Indexes of the selected packages, in order (an item of a package selects its package)

    // Look which the views share
    static constexpr int        Column_Margin = 8; // Pixels after the longest text of a column: texts do not touch the next column
    static QString              Track_Files(const gui_track& Track); // Count of files of a track and, for a sequence, the numbers of the first and last ones
    static QColor               Color_Error(const QPalette& Palette); // For the text of an error, readable on the background of a view
    static QColor               Color_Warning(const QPalette& Palette); // For the text of a warning
    static void                 Hint_Paint(QAbstractScrollArea* View, const QString& Text); // Text in the middle of a view which has no content
    static void                 FocusFrame_Hide(QStyleOptionViewItem* Option); // Whole lines are selected: the frame of the current cell is only for the keyboard

Q_SIGNALS:
    void                        Selection_Changed();
    void                        Track_Activated(size_t Package, size_t Track); // Double click or Enter on a track
    void                        Close_Requested(); // Delete key on the selection

private:
    GUI_Main*                   Main;

    // State
    QIcon                       Icon_Directory; // Asked once: the style takes time to give them
    QIcon                       Icon_File;
    bool                        Refreshing = false; // The tree is changed by its rebuild, not by the user
    bool                        Columns_Fitting = false; // Widths are being set by the view, not by the user
    std::vector<int>            Columns_Wanted; // Width each column needs for the content
    int                         Status_Lines_Wanted = 0; // Width the status of the lines inside the packages (tracks, files) needs
    std::vector<bool>           Columns_User; // Columns whose width was set by the user: it is kept, the other widths follow the content and the size of the view

    // Events
    void                        changeEvent(QEvent* Event) override;
    void                        keyPressEvent(QKeyEvent* Event) override;
    void                        paintEvent(QPaintEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;
    void                        showEvent(QShowEvent* Event) override;

    // Helpers
    QTreeWidgetItem*            Package_Add(size_t Pos); // Items of a package and of its content, at the end of the list
    void                        Columns_Measure();
    void                        Columns_Fit();
};

#endif
