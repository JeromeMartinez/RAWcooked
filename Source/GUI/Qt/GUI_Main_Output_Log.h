/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_Main_Output_LogH
#define GUI_Main_Output_LogH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QPlainTextEdit>
//---------------------------------------------------------------------------

//***************************************************************************
// GUI_Main_Output_Log: what the library and the encoder wrote, for all the
// operations since the start
//***************************************************************************

class GUI_Main_Output_Log : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit GUI_Main_Output_Log(QWidget* Parent = nullptr);

    void                        Add(const QString& Text); // Appended, the view follows the end if it was at the end

private:
    // Events
    void                        resizeEvent(QResizeEvent* Event) override;
    void                        showEvent(QShowEvent* Event) override;

    // Helpers
    bool                        IsAtEnd() const;
    void                        End_Show();
};

#endif
