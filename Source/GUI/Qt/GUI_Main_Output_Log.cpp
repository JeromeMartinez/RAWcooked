/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main_Output_Log.h"
#include <QFontDatabase>
#include <QScrollBar>
//---------------------------------------------------------------------------

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Main_Output_Log::GUI_Main_Output_Log(QWidget* Parent)
    : QPlainTextEdit(Parent)
{
    // Configuration
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setMaximumBlockCount(100000); // A long encoding writes a lot, the oldest lines are dropped
    setPlaceholderText(tr("No message yet. The commands run and the messages of RAWcooked and of the encoder are shown here."));
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Output_Log::Add(const QString& Text)
{
    if (Text.isEmpty())
        return;

    // Whole lines are added, each call starts a line: the line end at the end of the text would be an empty line
    QString Lines = Text;
    Lines.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (Lines.endsWith(QLatin1Char('\n')))
        Lines.chop(1);

    const bool AtEnd = IsAtEnd();
    appendPlainText(Lines);
    if (AtEnd)
        End_Show(); // Also while the view is hidden, appendPlainText() only does it for a visible view
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Output_Log::resizeEvent(QResizeEvent* Event)
{
    // The range of the scroll bar depends on the size
    const bool AtEnd = IsAtEnd();
    QPlainTextEdit::resizeEvent(Event);
    if (AtEnd)
        End_Show();
}

//---------------------------------------------------------------------------
void GUI_Main_Output_Log::showEvent(QShowEvent* Event)
{
    // The first time the view is shown it would go to the text cursor, which is at the start
    const bool AtEnd = IsAtEnd();
    QPlainTextEdit::showEvent(Event);
    if (AtEnd)
        End_Show();
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Main_Output_Log::IsAtEnd() const
{
    return verticalScrollBar()->value() >= verticalScrollBar()->maximum();
}

//---------------------------------------------------------------------------
void GUI_Main_Output_Log::End_Show()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}
