/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Help.h"
#include "GUI/Qt/GUI_Job.h"
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
//---------------------------------------------------------------------------

//***************************************************************************
// Texts
//***************************************************************************

//---------------------------------------------------------------------------
// The texts of the library are the same for the whole life of the program
static const QString& Options_Text()
{
    static QString Text;
    if (Text.isEmpty())
        Text = GUI_Job::Help_Options();
    return Text;
}

//---------------------------------------------------------------------------
static const QString& Edits_Text()
{
    static QString Text;
    if (Text.isEmpty())
        Text = GUI_Job::Help_Edits();
    return Text;
}

//---------------------------------------------------------------------------
// Shortcut as the menus of this platform show it
static QString Key(const QKeySequence& Sequence)
{
    return Sequence.toString(QKeySequence::NativeText).toHtmlEscaped();
}

//---------------------------------------------------------------------------
// The texts of the library are made for a fixed pitch, which the font of the platform for such texts does not always have
static QFont Font_Fixed()
{
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
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Help::GUI_Help(QWidget* Parent)
    : QDialog(Parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("RAWcooked - Help"));
    setWindowIcon(QIcon(":/Image/Icon.png"));

    // The key starting the edition of a value, as the keyboards of the platform name it
    #ifdef Q_OS_MACOS
        const QString EditKey = Key(Qt::Key_Return);
    #else
        const QString EditKey = tr("Enter");
    #endif

    Central = new QTabWidget(this);
    HowTo = new QTextBrowser(this);
    HowTo->setOpenExternalLinks(true);
    HowTo->setHtml(tr(
        "<p><b>Open</b> a directory or files with <i>File &gt; Open directory...</i> (%1) or <i>File &gt; Open file(s)...</i> (%2), or drop them on the window. "
        "What is opened in one go is a <b>package</b>, the unit of work: each directory is a package, files opened together are one package. "
        "A package is either uncompressed content, to encode, or a Matroska file made by RAWcooked, to decode or to check. "
        "<i>Actions &gt; Analyze again</i> (%3) reads the files again.</p>"
        "<p>A <b>track</b> is a sequence of image files, shown as one line (template of the file names, first and last numbers), or a single file (audio, video, or an image alone). "
        "The other files of the package are attachments.</p>"
        "<p>The <b>views</b> (menu <i>View</i> or tool bar): <i>Files</i> lists the packages with their tracks, their attachments and the status of the last operation; "
        "<i>Metadata</i> shows the fields of each track (a double click on a track in <i>Files</i> goes to it); <i>Log</i> keeps the commands and the messages of all the operations.</p>"
        "<p style=\"margin-bottom:4px;\"><b>Metadata</b> is shown once for all the files of a track. A field shows:</p>"
        "<ul style=\"margin-top:0px;\">"
        "<li>its value, if it is the same in all the files;</li>"
        "<li><code>first&nbsp;--&gt;&nbsp;last</code>, if the value increases from the first file to the last one (e.g. frame number, date and time);</li>"
        "<li><code>first&nbsp;...&nbsp;last</code> for other values which differ.</li>"
        "</ul>"
        "<p><b>Edit</b> a field which can be edited (they are listed in the tab <i>Metadata fields</i>) in the <i>Metadata</i> view: double click on its value, press %4 or just type. "
        "An empty value means that the field is not set. The new value is for all the files of the track. "
        "Modified fields are highlighted, they are written in the files by <i>File &gt; Save modified metadata</i> (%5); until then, the Delete key or the context menu reverts them.</p>"
        "<p><b>Encode</b>: <i>Actions &gt; Encode...</i> (%6) compresses each selected package (all of them if none is selected) to a Matroska file, "
        "by default with FFV1 for the video and FLAC for the audio. FFmpeg is needed. "
        "<i>Actions &gt; Show the command of the encoder...</i> shows the FFmpeg command instead of running it.</p>"
        "<p><b>Decode</b>: <i>Actions &gt; Decode</i> restores the original files next to the Matroska file, <i>Actions &gt; Decode to...</i> in another directory. "
        "<i>Actions &gt; Check</i> checks that the Matroska file can be decoded to the original files.</p>"
        "<p><b>Options</b> of the command line program are in the preferences (<i>Options &gt; Preferences...</i>; in the application menu on macOS), "
        "with a field for the options without a control of their own; they are described in the tab <i>Options</i>. "
        "The license key has its own dialog (<i>Options &gt; License...</i>).</p>"
        ).arg(Key(Qt::CTRL | Qt::Key_D), Key(Qt::CTRL | Qt::Key_O), Key(Qt::Key_F5), EditKey, Key(Qt::CTRL | Qt::Key_S), Key(Qt::CTRL | Qt::Key_E)));
    Central->addTab(HowTo, tr("How to"));
    Central->addTab(Text_Create(Options_Text()), tr("Options"));
    Central->addTab(Text_Create(Edits_Text()), tr("Metadata fields"));

    QDialogButtonBox* Buttons = new QDialogButtonBox(this);
    Close = Buttons->addButton(tr("&Close"), QDialogButtonBox::RejectRole);
    Close->setDefault(true);
    connect(Buttons, &QDialogButtonBox::rejected, this, &GUI_Help::close);

    QVBoxLayout* L = new QVBoxLayout();
    L->addWidget(Central);
    L->addWidget(Buttons);
    setLayout(L);

    // Sized from the fonts: the 80 columns of the texts of the library are visible and the lines of the how-to are not short; within the screen
    const QFontMetrics Metrics(Font_Fixed());
    QSize Size(qMax(Metrics.horizontalAdvance(QString(92, QLatin1Char('0'))), fontMetrics().averageCharWidth() * 110), Metrics.lineSpacing() * 30);
    if (const QScreen* Screen = Parent ? Parent->screen() : QGuiApplication::primaryScreen())
        Size = Size.boundedTo(Screen->availableGeometry().size() * 9 / 10);
    resize(Size);
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Help::showEvent(QShowEvent* Event)
{
    QDialog::showEvent(Event);

    // The first time, as high as the how-to needs for being read without scrolling, within the screen
    if (HowTo_IsFitted || Central->currentWidget() != HowTo)
        return;
    HowTo_IsFitted = true;
    const QScreen* Screen = screen();
    const int Height_Max = Screen ? Screen->availableGeometry().height() * 9 / 10 : height();
    for (int i = 0; i < 4; i++) // More than once: the text is wider when its scroll bar goes away
    {
        const int Missing = HowTo->verticalScrollBar()->maximum();
        if (Missing <= 0 || height() >= Height_Max)
            break;
        resize(width(), qMin(height() + Missing, Height_Max));
    }
    adjustPosition(parentWidget()); // Still centered on its parent
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
QWidget* GUI_Help::Text_Create(const QString& Text)
{
    QPlainTextEdit* Edit = new QPlainTextEdit(this);
    Edit->setReadOnly(true);
    Edit->setFont(Font_Fixed());
    Edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    Edit->setPlainText(Text);
    return Edit;
}
