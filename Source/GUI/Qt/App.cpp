/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main.h"
#include "GUI/Qt/GUI_Job.h"
#include <QApplication>
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
    // Qt 5 does not scale on high DPI screens by default, and its dialogs have a help button without use here
    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QCoreApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
    #endif

    QApplication App(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("RAWcooked"));
    QCoreApplication::setApplicationName(QStringLiteral("RAWcooked"));

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
