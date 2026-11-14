/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_HelpH
#define GUI_HelpH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QDialog>
class QPushButton;
class QShowEvent;
class QString;
class QTabWidget;
class QTextBrowser;
class QWidget;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Help
//***************************************************************************

class GUI_Help : public QDialog
{
    Q_OBJECT

public:
    explicit GUI_Help(QWidget* Parent = nullptr);

private:
    // GUI
    QTabWidget*                 Central;
    QTextBrowser*               HowTo;
    QPushButton*                Close;
    bool                        HowTo_IsFitted = false;

    // Events
    void                        showEvent(QShowEvent* Event) override;

    // Helpers
    QWidget*                    Text_Create(const QString& Text); // Text of the library, shown as the command line program prints it
};

#endif
