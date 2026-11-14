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
#include <cstddef>
#include <vector>
class GUI_Main;
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
    bool                        Selection_Track(size_t& Package, size_t& Track) const; // True if the current item is a track

Q_SIGNALS:
    void                        Selection_Changed();
    void                        Track_Activated(size_t Package, size_t Track); // Double click or Enter on a track
    void                        Close_Requested(); // Delete key on the selection

private:
    GUI_Main*                   Main;

    // State
    bool                        Refreshing = false; // The tree is changed by its rebuild, not by the user
    bool                        Columns_Fitting = false; // Widths are being set by the view, not by the user
    std::vector<int>            Columns_Wanted; // Width each column needs for the content
    std::vector<bool>           Columns_User; // Columns whose width was set by the user: it is kept, the other widths follow the content and the size of the view

    // Events
    void                        keyPressEvent(QKeyEvent* Event) override;
    void                        paintEvent(QPaintEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;
    void                        showEvent(QShowEvent* Event) override;

    // Helpers
    void                        Columns_Measure();
    void                        Columns_Fit();
};

#endif
