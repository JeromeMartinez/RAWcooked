/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_LicenseH
#define GUI_LicenseH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QDialog>
#include <QString>
#include <QStringList>
class GUI_Options;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_License: status of the license, license key, sub-license keys
// (never while an operation is running)
//***************************************************************************

class GUI_License : public QDialog
{
    Q_OBJECT

public:
    GUI_License(GUI_Options* Options, QWidget* Parent = nullptr);

private Q_SLOTS:
    void                        OnStatus_Refresh();
    void                        OnKey_Changed();
    void                        OnKey_Show(bool Checked);
    void                        OnKey_Use();
    void                        OnKey_Store();
    void                        OnSubLicense_Create();
    void                        OnSubLicense_Copy();

private:
    GUI_Options*                Options;

    // GUI
    QPlainTextEdit*             Status;
    QLabel*                     Message;            // Result of the last action
    QLineEdit*                  Key;
    QCheckBox*                  Key_Show;
    QPushButton*                Key_Use;
    QPushButton*                Key_Store;
    QSpinBox*                   SubLicense_Id;
    QSpinBox*                   SubLicense_Dur;
    QPushButton*                SubLicense_Create;
    QLineEdit*                  SubLicense_Key;
    QPushButton*                SubLicense_Copy;
    void                        Buttons_Update();

    // Jobs of the library, one at a time, the dialog stays open until the end of the current one
    bool                        IsBusy = false;
    bool                        IsStatusRead = false;
    bool                        Run(const QStringList& Arguments, QString& Out, QString& Err); // True if the job has no issue
    bool                        Status_Read(const QString& LicenseKey); // Status of the license with this key, or without key if empty
    void                        Status_Show(const QString& Text);
    QString                     Key_Get() const;    // Key typed by the user, without the spaces a copy may have added

    // Events
    void                        showEvent(QShowEvent* Event) override;
    void                        done(int Result) override;
};

#endif
