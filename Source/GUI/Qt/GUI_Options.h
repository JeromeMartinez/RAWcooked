/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#ifndef GUI_OptionsH
#define GUI_OptionsH
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#include <QString>
#include <QStringList>
#include <QMap>
#include <cstddef>
//---------------------------------------------------------------------------

//***************************************************************************
// Options of the library, as the command line of rawcooked has them:
// one table describes each option (how it is shown in the preferences and
// which arguments it gives to the library), so every option of the command
// line is reachable from the user interface.
//***************************************************************************

//---------------------------------------------------------------------------
// Operations of the library, an option is given to the operations it is relevant for
enum gui_operation
{
    Operation_Analyze   = 1 << 0,   // Opening: analysis of the inputs, nothing is written
    Operation_Edit      = 1 << 1,   // Saving the modified metadata
    Operation_Encode    = 1 << 2,   // Uncompressed content to Matroska (also for showing the command of the encoder)
    Operation_Decode    = 1 << 3,   // Matroska to uncompressed content
    Operation_Check     = 1 << 4,   // Check of a Matroska file
    Operation_License   = 1 << 5,   // License management
};

//---------------------------------------------------------------------------
struct gui_option
{
    enum type
    {
        Type_Bool,      // Check box. Values: "arguments if checked|arguments if not checked", each part may be empty
        Type_Choice,    // List. Values: "label=arguments|label=arguments|...", the stored value is the arguments part
        Type_Text,      // Text. Values: arguments with %1 for the text (a single argument), nothing is given if the text is empty; may be followed by "|regular expression" the text must match
        Type_Number,    // As Type_Text, with digits only (1 to 1999999999)
        Type_File,      // As Type_Text, with a button for choosing an existing file; may be followed by "|filter" of the names of the files to choose from, then by "|regular expression" the name of the file (without its directory) must match
        Type_FileSave,  // As Type_File, with a button for choosing the name of a file to create
        Type_TextChoice,// As Type_Text, with a list of suggested texts. Values: "arguments with %1|suggested text|suggested text|..."
        Type_Arguments, // Text split as a command line (quotes for arguments with spaces) and given as is, the options of the license are refused. Values is not used
    };

    const char*                 Name;           // Key in the settings
    const char*                 Group;          // Tab in the preferences
    const char*                 Label;
    const char*                 Help;           // Tool tip, from the help of the command line
    type                        Type;
    const char*                 Values;
    const char*                 Default;        // Type_Bool: "1" or "0", Type_Choice: arguments part of the default choice, else the default text
    int                         Operations;     // Combination of gui_operation
};

//---------------------------------------------------------------------------
class GUI_Options
{
public:
    GUI_Options(); // Values are loaded from the settings

    // Description
    static const gui_option*    List(size_t& Count); // In the order of the preferences and of the arguments
    static const gui_option*    Find(const QString& Name); // nullptr if unknown

    // Values
    QString                     Value(const QString& Name) const; // Default value if not set
    void                        Value_Set(const QString& Name, const QString& Value);
    bool                        IsDefault(const QString& Name) const;
    void                        Defaults_Restore(); // All values, not saved
    void                        Load();
    void                        Save();

    // Arguments for the library, for an operation (options only; actions and inputs are added by the caller)
    QStringList                 Arguments(gui_operation Operation) const;

    // Not stored in the settings
    QString                     LicenseKey;     // License key for this session (--license), if any

    // Other settings of the user interface
    QString                     Directory_Get() const; // Last directory used for opening
    void                        Directory_Set(const QString& Directory);

private:
    QMap<QString, QString>      Values;         // Only the values which are not the default ones

    // Description of the values of an option, also for the preferences
    friend class GUI_Preferences;
    static QStringList          Values_Parts(const gui_option& Option); // Parts of gui_option::Values, as gui_option::type describes them
    static QString              Values_Pattern(const gui_option& Option); // Regular expression a text (the name of a file, without its directory) must match, empty if any text is fine
    static bool                 Value_IsValid(const gui_option& Option, const QString& Value); // False for a value the option can not have
};

#endif
