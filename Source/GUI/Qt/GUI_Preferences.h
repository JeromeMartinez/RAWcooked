/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_PreferencesH
#define GUI_PreferencesH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QDialog>
#include <vector>
class GUI_Options;
class QAbstractButton;
class QDialogButtonBox;
class QScrollArea;
class QTabWidget;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Preferences: every option of GUI_Options, a tab per group
//***************************************************************************

class GUI_Preferences : public QDialog
{
    Q_OBJECT

public:
    GUI_Preferences(GUI_Options* Options, QWidget* Parent = nullptr); // Options are modified and saved when the dialog is accepted

private Q_SLOTS:
    void                        OnSave();
    void                        OnDialogClicked(QAbstractButton* Button);

private:
    GUI_Options*                Options;

    // GUI
    QTabWidget*                 Central;
    QDialogButtonBox*           Dialog;
    std::vector<QScrollArea*>   Pages;          // Tabs, one per group of options
    std::vector<QWidget*>       Widgets;        // Widget with the value of each option, in the order of the options
    std::vector<int>            Widgets_Page;   // Tab of each option
    void                        Create();

    // Values of the options, as the widgets have them
    QString                     Value(size_t Pos) const;
    void                        Value_Set(size_t Pos, const QString& Value);
    void                        Browse(size_t Pos); // Choice of the file of an option

    // Events
    bool                        eventFilter(QObject* Object, QEvent* Event) override;
};

#endif
