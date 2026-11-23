/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main.h"
#include "GUI/Qt/GUI_Job.h"
#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <clocale>
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Qt linked statically on Windows: its platform plugin is in the program
#if defined(_WIN32) && !defined(_DLL)
    #include <QtPlugin>
    Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif
//---------------------------------------------------------------------------

//***************************************************************************
// Main
//***************************************************************************

//---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // An application started from the Finder does not have the PATH of a terminal: FFmpeg is also searched where it is usually installed
    #if defined(__APPLE__)
        QByteArray Path = qgetenv("PATH");
        for (const QByteArray& Directory : { QByteArray("/opt/homebrew/bin"), QByteArray("/usr/local/bin"), QByteArray("/opt/local/bin") })
            if (!Path.split(':').contains(Directory))
            {
                if (!Path.isEmpty())
                    Path += ':';
                Path += Directory;
            }
        qputenv("PATH", Path);
    #endif

    // Qt 5 does not scale on high DPI screens by default, and its dialogs have a help button without use here
    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QCoreApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
    #endif

    QApplication App(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RAWcooked"));
    QCoreApplication::setApplicationName(QStringLiteral("RAWcooked"));

    // Icon of the windows; not on macOS, where it would replace the icon of the application bundle in the Dock
    #if !defined(__APPLE__)
        QIcon Icon;
        for (int Size : { 16, 22, 24, 32, 48, 64, 128, 256 })
            Icon.addFile(QStringLiteral(":/Image/hicolor/%1x%1/apps/rawcooked.png").arg(Size), QSize(Size, Size));
        QApplication::setWindowIcon(Icon);
    #endif

    // The library reads and writes numbers with the decimal point of C, QApplication sets the locale of the system on Unix
    setlocale(LC_NUMERIC, "C");

    // What the library writes is for the user interface
    GUI_Job::Console_Init();

    GUI_Main Window;
    Window.show();

    // Files and directories given to the program
    QStringList Paths = QCoreApplication::arguments();
    if (!Paths.isEmpty())
        Paths.removeFirst(); // Name of the program
    if (!Paths.isEmpty())
        Window.Open(Paths);

    return App.exec();
}
