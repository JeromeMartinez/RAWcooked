/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main_Output_Log.h"
#include <QFontDatabase>
#include <QFontInfo>
#include <QScrollBar>
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
const int Blocks_Max = 100000; // Lines kept: a long encoding writes a lot, the oldest ones are dropped

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
    setFont(Font_Fixed());
    setMaximumBlockCount(Blocks_Max);
    setPlaceholderText(tr("Nothing yet. The commands which are run and the messages of RAWcooked and of FFmpeg are shown here."));
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

//---------------------------------------------------------------------------
QFont GUI_Main_Output_Log::Font_Fixed()
{
    // The font of the platform for such texts does not always have a fixed pitch
    QFont Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(Font).fixedPitch())
    {
        Font.setFamily(QStringLiteral("monospace"));
        Font.setStyleHint(QFont::Monospace);
        Font.setFixedPitch(true);
    }
    return Font;
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
