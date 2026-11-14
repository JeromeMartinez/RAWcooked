/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_AboutH
#define GUI_AboutH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QDialog>
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QTextBrowser;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_About
//***************************************************************************

class GUI_About : public QDialog
{
    Q_OBJECT

public:
    explicit GUI_About(QWidget* Parent = nullptr);

private Q_SLOTS:
    // Actions
    void                        OnDescription();
    void                        OnAuthorLicense();
    void                        OnContact();

private:
    // GUI
    QTextBrowser*               NameVersion;
    QTextBrowser*               Text;
    QPushButton*                Button_Description;
    QPushButton*                Button_AuthorLicense;
    QPushButton*                Button_Contact;
    QPushButton*                Close;

    // Events
    void                        showEvent(QShowEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;

    // Helpers
    void                        NameVersion_Resize(); // As high as its text
    void                        Text_Show(QPushButton* Button); // Button is the one of the text shown
};

#endif
