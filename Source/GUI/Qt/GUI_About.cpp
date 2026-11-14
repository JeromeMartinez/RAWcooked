/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_About.h"
#include "GUI/Qt/GUI_Job.h"
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
//---------------------------------------------------------------------------

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_About::GUI_About(QWidget* Parent)
    : QDialog(Parent)
{
    // Fonts are bigger outside Windows
    #ifdef Q_OS_WIN
        resize(640, 440);
    #else
        resize(840, 540);
    #endif
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("RAWcooked - About"));
    setWindowIcon(QIcon(":/Image/Icon.png"));
    setModal(true);

    // Left: the texts to choose from, and the logo
    QVBoxLayout* L00 = new QVBoxLayout();
    Button_Description = new QPushButton(tr("&Description"), this);
    Button_AuthorLicense = new QPushButton(tr("&Author and License"), this);
    Button_Contact = new QPushButton(tr("&Contact"), this);
    L00->addWidget(Button_Description);
    L00->addWidget(Button_AuthorLicense);
    L00->addWidget(Button_Contact);
    QLabel* Logo = new QLabel(this);
    Logo->setPixmap(QPixmap(":/Image/Icon.png").scaledToWidth(128, Qt::SmoothTransformation));
    Logo->setAlignment(Qt::AlignCenter);
    if (palette().color(QPalette::Window).lightness() < 128)
    {
        // The logo has black parts: with a dark theme it gets a light background of its own
        Logo->setFixedSize(Logo->sizeHint() + QSize(16, 16));
        QPalette Logo_Palette = Logo->palette();
        Logo_Palette.setColor(QPalette::Window, QColor(240, 240, 240));
        Logo->setPalette(Logo_Palette);
        Logo->setAutoFillBackground(true);
    }
    L00->addStretch(1);
    L00->addWidget(Logo, 0, Qt::AlignHCenter);
    L00->addStretch(1);

    // Right: name and version above the chosen text
    NameVersion = new QTextBrowser(this);
    NameVersion->setOpenExternalLinks(true);
    NameVersion->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    NameVersion->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QPalette NameVersion_Palette = NameVersion->palette();
    NameVersion_Palette.setBrush(QPalette::Base, NameVersion_Palette.brush(QPalette::Window)); // A title, not a text area
    NameVersion->setPalette(NameVersion_Palette);
    NameVersion->setHtml(QString("<span style=\"font-size:x-large; font-weight:bold;\">%1 %2</span>").arg(GUI_Job::Library_Name().toHtmlEscaped(), GUI_Job::Library_Version().toHtmlEscaped()));
    Text = new QTextBrowser(this);
    Text->setOpenExternalLinks(true);

    Close = new QPushButton(tr("&Close"), this);
    Close->setDefault(true);

    QGridLayout* L = new QGridLayout();
    L->addLayout(L00, 0, 0, 3, 1);
    L->addWidget(NameVersion, 0, 1, 1, 2);
    L->addWidget(Text, 1, 1, 1, 2);
    L->addWidget(Close, 2, 2);
    L->setColumnStretch(1, 1);
    L->setRowStretch(1, 1);
    setLayout(L);

    connect(Button_Description, &QPushButton::clicked, this, &GUI_About::OnDescription);
    connect(Button_AuthorLicense, &QPushButton::clicked, this, &GUI_About::OnAuthorLicense);
    connect(Button_Contact, &QPushButton::clicked, this, &GUI_About::OnContact);
    connect(Close, &QPushButton::clicked, this, &GUI_About::close);

    OnDescription();
}

//***************************************************************************
// Actions
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_About::OnDescription()
{
    Text->clear();
    Text->setHtml(tr(
        "<h3>DESCRIPTION</h3>"
        "<p><b>RAWcooked</b> encodes RAW audio-visual data into the Matroska container (MKV), using the video codec FFV1 for the image and audio codec FLAC for the sound.</p>"
        "<p>The metadata accompanying the RAW data is fully preserved, along with sidecar files such as MD5 checksums, LUT or XML. The Matroska file can be decoded back to files which are bit-by-bit identical to the originals.</p>"
        "<p>RAWcooked is ready for production. As with any other tool, keep doing basic checks e.g. testing reversibility with your own files.</p>"
        "<p>RAWcooked needs FFmpeg for encoding. The FFmpeg libraries are also used for decoding with the GPU (Vulkan), which is faster. We recommend the <a href=\"https://mediaarea.net/download/snapshots/binary/ffmpeg/latest/\">FFmpeg builds of MediaArea</a>, they have GPU support and RAWcooked finds them automatically.</p>"
        "<p>Information on this project and builds for different platforms can be found at <a href=\"https://mediaarea.net/RAWcooked\">https://mediaarea.net/RAWcooked</a>.</p>"
    ));
    Text_Show(Button_Description);
}

//---------------------------------------------------------------------------
void GUI_About::OnAuthorLicense()
{
    Text->clear();
    Text->setSource(QUrl("qrc:/License.html"));
    Text_Show(Button_AuthorLicense);
}

//---------------------------------------------------------------------------
void GUI_About::OnContact()
{
    Text->clear();
    Text->setHtml(tr(
        "<h3>CONTACT</h3>"
        "<p>Questions, comments and requests about RAWcooked are welcome at MediaArea.net.</p>"
        "<p>Website: <a href=\"https://mediaarea.net/RAWcooked\">https://mediaarea.net/RAWcooked</a><br>"
        "Email: <a href=\"mailto:info@mediaarea.net\">info@mediaarea.net</a><br>"
        "Source code and issues: <a href=\"https://github.com/MediaArea/RAWcooked\">https://github.com/MediaArea/RAWcooked</a></p>"
    ));
    Text_Show(Button_Contact);
}

//***************************************************************************
// Events
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_About::showEvent(QShowEvent* Event)
{
    QDialog::showEvent(Event);
    NameVersion_Resize();
}

//---------------------------------------------------------------------------
void GUI_About::resizeEvent(QResizeEvent* Event)
{
    QDialog::resizeEvent(Event);
    NameVersion_Resize();
}

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_About::NameVersion_Resize()
{
    NameVersion->setFixedHeight((int)std::ceil(NameVersion->document()->size().height()) + 2 * NameVersion->frameWidth());
}

//---------------------------------------------------------------------------
void GUI_About::Text_Show(QPushButton* Button)
{
    // The button of the text shown is the disabled one
    Button_Description->setEnabled(Button != Button_Description);
    Button_AuthorLicense->setEnabled(Button != Button_AuthorLicense);
    Button_Contact->setEnabled(Button != Button_Contact);
}
