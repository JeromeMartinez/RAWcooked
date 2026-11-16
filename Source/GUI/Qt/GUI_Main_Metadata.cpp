/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main_Metadata.h"
#include "GUI/Qt/GUI_Main.h"
#include "GUI/Qt/GUI_Main_Files.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QValidator>
#include <cfloat>
#include <climits>
//---------------------------------------------------------------------------

//***************************************************************************
// Helpers
//***************************************************************************

//---------------------------------------------------------------------------
enum
{
    Column_Field,
    Column_Value,
    Column_Files,
    Column_Max
};

//---------------------------------------------------------------------------
// Kind of an item (type of the item) and what it shows (data of its first column)
enum
{
    Item_Track = QTreeWidgetItem::UserType,
    Item_Group,
    Item_Field,
};
enum
{
    Role_Package = Qt::UserRole,    // Tracks: index in the packages
    Role_Track,                     // Tracks: index in the tracks of the package
    Role_Field,                     // Fields: index in the fields of the track
    Role_Key,                       // Tracks and groups: what they show, same after a rebuild of the tree
    Role_Title,                     // Tracks and groups: their text, without the mark of the modified ones
    Role_Value,                     // Fields: their value as a text, empty if not set
};

//---------------------------------------------------------------------------
const int Field_Percent_Max = 45;       // Percent of the view for the names of the fields, at most: the values need the rest
const int Field_Lines_Min = 12;         // Width the names keep in a narrow view, in heights of a line of text

//---------------------------------------------------------------------------
// Property of an editor: the user changed its content
static const char* const Editor_IsEdited = "IsEdited";

//---------------------------------------------------------------------------
// A track is the same one after a rebuild of the tree if it has the same first file in the same package
static QString Track_Key(const gui_package& Package, const gui_track& Track)
{
    QString Key = Package.Inputs.join(QLatin1Char('\n'));
    Key += QLatin1Char('\n');
    Key += Track.FileName;
    return Key;
}

//---------------------------------------------------------------------------
// Largest number which fits in the bytes of an unsigned integer field
static quint64 Unsigned_Max(int Size)
{
    return Size <= 0 || Size >= 8 ? (quint64)-1 : (((quint64)1 << (8 * Size)) - 1);
}

//---------------------------------------------------------------------------
// Same value in the files, whatever the way it is written
static bool Value_IsSame(const gui_field& Field, const QString& Left, const QString& Right)
{
    if (Left == Right)
        return true;
    if (Left.isEmpty() || Right.isEmpty())
        return false; // Not set is not a number
    bool Left_IsOk = false, Right_IsOk = false;
    switch (Field.Type)
    {
        case gui_field::Type_Unsigned:
            return Left.toULongLong(&Left_IsOk) == Right.toULongLong(&Right_IsOk) && Left_IsOk && Right_IsOk;
        case gui_field::Type_Float:
            return Left.toFloat(&Left_IsOk) == Right.toFloat(&Right_IsOk) && Left_IsOk && Right_IsOk;
        default:
            return false;
    }
}

//---------------------------------------------------------------------------
// What the value shown for a field is among the files of its track, and the mark of a field of the structure of the files
static QString Field_Files(const gui_field& Field, const gui_track& Track)
{
    QString Text;
    if (Field.IsModified)
        Text = Track.FileCount > 1 ? GUI_Main_Metadata::tr("all files, not saved") : GUI_Main_Metadata::tr("not saved");
    else if (Field.Kind == gui_field::Kind_Sequence)
        Text = GUI_Main_Metadata::tr("first --> last file");
    else if (Field.Kind == gui_field::Kind_Mixed)
        Text = GUI_Main_Metadata::tr("different values");
    if (!Field.IsStructural)
        return Text;
    return Text.isEmpty() ? GUI_Main_Metadata::tr("structure of the file") : GUI_Main_Metadata::tr("%1, structure of the file").arg(Text);
}

//---------------------------------------------------------------------------
// Value in a tool tip
static QString ToolTip_Value(const QString& Value)
{
    if (Value.isEmpty())
        return QLatin1String("<i>") + GUI_Main_Metadata::tr("not set") + QLatin1String("</i>");
    return Value.toHtmlEscaped();
}

//---------------------------------------------------------------------------
// Everything known about a field
static QString Field_ToolTip(const gui_field& Field, const gui_track& Track)
{
    QStringList Lines;

    // Name, description
    QString Name = QLatin1String("<b>") + Field.Name.toHtmlEscaped() + QLatin1String("</b>");
    if (!Field.Group.isEmpty())
        Name += QLatin1String(" (") + Field.Group.toHtmlEscaped() + QLatin1Char(')');
    Lines += Name;
    if (!Field.Description.isEmpty())
        Lines += Field.Description.toHtmlEscaped();

    // Type and maximum length
    switch (Field.Type)
    {
        case gui_field::Type_Unsigned:
            Lines += Field.Size > 0 ? GUI_Main_Metadata::tr("Unsigned integer, %1 at most (%1 means not set)").arg(Unsigned_Max(Field.Size)) : GUI_Main_Metadata::tr("Unsigned integer");
            break;
        case gui_field::Type_Float:
            Lines += GUI_Main_Metadata::tr("Number");
            break;
        case gui_field::Type_TimeCode:
            Lines += GUI_Main_Metadata::tr("Time code, HH:MM:SS:FF");
            break;
        default:
            Lines += Field.Size > 0 ? GUI_Main_Metadata::tr("Text, %1 bytes at most").arg(Field.Size) : GUI_Main_Metadata::tr("Text");
    }

    // Values
    if (Field.Kind == gui_field::Kind_Same)
        Lines += (Track.FileCount > 1 ? GUI_Main_Metadata::tr("Value in all files: %1") : GUI_Main_Metadata::tr("Value: %1")).arg(ToolTip_Value(Field.First));
    else
    {
        Lines += GUI_Main_Metadata::tr("First file: %1").arg(ToolTip_Value(Field.First));
        Lines += GUI_Main_Metadata::tr("Last file: %1").arg(ToolTip_Value(Field.Last));
        Lines += Field.Kind == gui_field::Kind_Sequence ? GUI_Main_Metadata::tr("The value increases from the first file to the last one") : GUI_Main_Metadata::tr("The files have different values");
    }
    if (Field.IsModified)
    {
        Lines += GUI_Main_Metadata::tr("New value: %1").arg(ToolTip_Value(Field.NewValue));
        Lines += QLatin1String("<b>") + GUI_Main_Metadata::tr("Modified, not saved") + QLatin1String("</b>");
    }

    // Edition
    if (!Field.IsEditable)
        Lines += GUI_Main_Metadata::tr("Cannot be edited");
    else if (Field.IsStructural)
        Lines += GUI_Main_Metadata::tr("Describes the structure of the files: a wrong value makes them unreadable");

    return QLatin1String("<qt>") + Lines.join(QLatin1String("<br>")) + QLatin1String("</qt>");
}

//---------------------------------------------------------------------------
// Color of the texts of the view over the color of its background, with this opacity in percent
static QColor Color_Mix(const QPalette& Palette, int Percent)
{
    const QColor Text = Palette.color(QPalette::Text);
    const QColor Base = Palette.color(QPalette::Base);
    return QColor((Text.red() * Percent + Base.red() * (100 - Percent)) / 100,
                  (Text.green() * Percent + Base.green() * (100 - Percent)) / 100,
                  (Text.blue() * Percent + Base.blue() * (100 - Percent)) / 100);
}

//---------------------------------------------------------------------------
// Texts and look of the item of a field
static void Field_Show(QTreeWidgetItem* Item, const gui_field& Field, const gui_track& Track, const QPalette& Palette)
{
    // A cell without value says it, else nothing shows that there is a field to set
    const bool HasValue = Field.HasValue();
    Item->setText(Column_Field, Field.Name);
    Item->setText(Column_Value, HasValue ? Field.Display() : GUI_Main_Metadata::tr("not set"));
    Item->setData(Column_Value, Role_Value, HasValue ? Field.Display() : QString());
    Item->setText(Column_Files, Field_Files(Field, Track));

    const QString ToolTip = Field_ToolTip(Field, Track);
    for (int i = 0; i < Column_Max; i++)
    {
        // Modified: bold. Not as the other ones (the name of a field of the structure, a text which is not a value): italic
        const bool IsItalic = i == Column_Field ? Field.IsStructural : (i == Column_Value && !HasValue);
        if (Field.IsModified || IsItalic)
        {
            QFont Font;
            if (Field.IsModified)
                Font.setBold(true);
            if (IsItalic)
                Font.setItalic(true);
            Item->setFont(i, Font);
        }
        else
            Item->setData(i, Qt::FontRole, QVariant());
        if (Field.IsModified)
        {
            // Green, with a text which can be read on it whatever the colors of the system
            Item->setBackground(i, QColor(144, 238, 144));
            Item->setForeground(i, QColor(0, 0, 0));
        }
        else
        {
            Item->setData(i, Qt::BackgroundRole, QVariant());
            Item->setData(i, Qt::ForegroundRole, QVariant());
        }
        Item->setToolTip(i, ToolTip);
    }
    if (Field.IsModified)
        return;
    if (!Field.IsEditable)
        Item->setForeground(Column_Value, Color_Mix(Palette, 55)); // Lighter, as what can not be changed, but it is content to read: not as light as a disabled text
    else if (!HasValue)
        Item->setForeground(Column_Value, Palette.color(QPalette::PlaceholderText));
}

//---------------------------------------------------------------------------
// Item of the track of an item
static const QTreeWidgetItem* Item_Track_Get(const QTreeWidgetItem* Item)
{
    while (Item && Item->type() != Item_Track)
        Item = Item->parent();
    return Item;
}
static QTreeWidgetItem* Item_Track_Get(QTreeWidgetItem* Item)
{
    while (Item && Item->type() != Item_Track)
        Item = Item->parent();
    return Item;
}

//***************************************************************************
// Validators of the editors
// An empty text is always accepted: the field is not set.
// No key is refused (the text would not be what was typed, without a word):
// a text which is not valid stays, shown as such, and can not be validated
//***************************************************************************

//---------------------------------------------------------------------------
static bool IsDigit(QChar Char)
{
    return Char >= QLatin1Char('0') && Char <= QLatin1Char('9');
}

//---------------------------------------------------------------------------
// Line ends and tabs come with a text copied from a list or from a table, they are not content:
// removed at the ends of the text, one space inside it. IsFollowed: the text is the start of a longer one
static QString Text_Clean(const QString& Text, bool IsFollowed)
{
    QString Result;
    Result.reserve(Text.size());
    bool IsPending = false; // Some were met since the last character taken
    for (const QChar Char : Text)
    {
        if (Char == QLatin1Char('\n') || Char == QLatin1Char('\r') || Char == QLatin1Char('\t'))
        {
            IsPending = true;
            continue;
        }
        if (IsPending && !Result.isEmpty())
            Result += QLatin1Char(' ');
        IsPending = false;
        Result += Char;
    }
    if (IsPending && IsFollowed && !Result.isEmpty())
        Result += QLatin1Char(' ');
    return Result;
}

//---------------------------------------------------------------------------
class GUI_Main_Metadata_Validator : public QValidator
{
public:
    explicit GUI_Main_Metadata_Validator(QObject* Parent) : QValidator(Parent) {}

    State validate(QString& Input, int& Position) const override
    {
        if (Input.contains(QLatin1Char('\n')) || Input.contains(QLatin1Char('\r')) || Input.contains(QLatin1Char('\t')))
        {
            Position = (int)Text_Clean(Input.left(Position), !Text_Clean(Input.mid(Position), false).isEmpty()).size();
            Input = Text_Clean(Input, false);
        }
        return Check(Input, Position).isEmpty() ? Acceptable : Intermediate;
    }

    // What is wrong with the text of the editor, empty if the text is valid
    QString Problem(QString Text) const
    {
        int Position = 0;
        return Check(Text, Position);
    }

protected:
    // What is wrong with the text, empty if it is valid. The text may be changed to what is meant, where it is seen at once
    virtual QString Check(QString& Input, int& Position) const = 0;
};

//---------------------------------------------------------------------------
// Decimal digits, and the number fits in the bytes of the field
class GUI_Main_Metadata_Validator_Unsigned : public GUI_Main_Metadata_Validator
{
public:
    GUI_Main_Metadata_Validator_Unsigned(quint64 Max_, QObject* Parent) : GUI_Main_Metadata_Validator(Parent), Max(Max_) {}

protected:
    QString Check(QString& Input, int&) const override
    {
        for (const QChar Char : Input)
            if (!IsDigit(Char))
                return GUI_Main_Metadata::tr("Only digits: the value is a number from 0 to %1").arg(Max);
        bool IsOk = true;
        if (!Input.isEmpty() && (Input.toULongLong(&IsOk) > Max || !IsOk))
            return GUI_Main_Metadata::tr("Too large: %1 at most").arg(Max);
        return QString();
    }

private:
    quint64                     Max;
};

//---------------------------------------------------------------------------
// Decimal number as the C library reads it (the library uses it), which can be stored in the 32 bits of the field
class GUI_Main_Metadata_Validator_Float : public GUI_Main_Metadata_Validator
{
public:
    explicit GUI_Main_Metadata_Validator_Float(QObject* Parent) : GUI_Main_Metadata_Validator(Parent) {}

protected:
    QString Check(QString& Input, int&) const override
    {
        Input.replace(QLatin1Char(','), QLatin1Char('.')); // The decimal separator of many keyboards
        if (Input.isEmpty())
            return QString();

        // Sign, digits with at most one point, exponent
        int Pos = 0;
        const auto Sign = [&]()
        {
            if (Pos < Input.size() && (Input.at(Pos) == QLatin1Char('+') || Input.at(Pos) == QLatin1Char('-')))
                Pos++;
        };
        const auto Digits = [&]()
        {
            const int Begin = Pos;
            while (Pos < Input.size() && IsDigit(Input.at(Pos)))
                Pos++;
            return Pos - Begin;
        };
        Sign();
        int Count = Digits();
        if (Pos < Input.size() && Input.at(Pos) == QLatin1Char('.'))
        {
            Pos++;
            Count += Digits();
        }
        if (Count && Pos < Input.size() && (Input.at(Pos) == QLatin1Char('e') || Input.at(Pos) == QLatin1Char('E')))
        {
            Pos++;
            Sign();
            Count = Digits();
        }
        if (!Count || Pos < Input.size())
            return GUI_Main_Metadata::tr("Not a number (examples: 24, 23.976, -0.5, 1.5e3)");

        // The conversion gives an infinite value or 0 for a number which is out of its own range
        const double Value = Input.toDouble();
        if (Value < -FLT_MAX || Value > FLT_MAX)
            return GUI_Main_Metadata::tr("Too large for the field");
        if (Value > -FLT_MIN && Value < FLT_MIN)
            for (const QChar Char : Input)
            {
                if (Char == QLatin1Char('e') || Char == QLatin1Char('E'))
                    break;
                if (Char >= QLatin1Char('1') && Char <= QLatin1Char('9'))
                    return GUI_Main_Metadata::tr("Too close to 0 for the field"); // The C library rejects it
            }
        return QString();
    }
};

//---------------------------------------------------------------------------
// HH:MM:SS:FF with decimal digits, the only form the library reads as a time code
class GUI_Main_Metadata_Validator_TimeCode : public GUI_Main_Metadata_Validator
{
public:
    explicit GUI_Main_Metadata_Validator_TimeCode(QObject* Parent) : GUI_Main_Metadata_Validator(Parent) {}

protected:
    QString Check(QString& Input, int& Position) const override
    {
        bool IsRegular = true; // Digits and separators are at their places so far
        for (int i = 0; i < Input.size() && IsRegular; i++)
        {
            const QChar Char = Input.at(i);
            if (i % 3 != 2)
                IsRegular = IsDigit(Char); // Else a digit was removed or something else was typed: the text is not changed
            else if (IsDigit(Char))
            {
                // The separator is typed for the user
                Input.insert(i, QLatin1Char(':'));
                if (Position > i)
                    Position++;
            }
            else if (Char == QLatin1Char(';') || Char == QLatin1Char('.') || Char == QLatin1Char(','))
                Input[i] = QLatin1Char(':'); // The separators of other notations of a time code
            else
                IsRegular = Char == QLatin1Char(':');
        }
        if (Input.isEmpty())
            return QString();
        if (!IsRegular || Input.size() != 11)
            return GUI_Main_Metadata::tr("Not a time code: HH:MM:SS:FF, 2 digits each");
        const auto Part = [&](int Pos) { return Input.at(Pos).digitValue() * 10 + Input.at(Pos + 1).digitValue(); };
        if (Part(0) > 23)
            return GUI_Main_Metadata::tr("Hours: 23 at most");
        if (Part(3) > 59)
            return GUI_Main_Metadata::tr("Minutes: 59 at most");
        if (Part(6) > 59)
            return GUI_Main_Metadata::tr("Seconds: 59 at most");
        return QString();
    }
};

//---------------------------------------------------------------------------
// Text which the library can receive (local 8-bit encoding, as its file names), which fits in the bytes of the field
// and which is read back as it is written
class GUI_Main_Metadata_Validator_Text : public GUI_Main_Metadata_Validator
{
public:
    GUI_Main_Metadata_Validator_Text(int Size_, QObject* Parent) : GUI_Main_Metadata_Validator(Parent), Size(Size_) {}

protected:
    QString Check(QString& Input, int&) const override
    {
        for (int i = 0; i < Input.size(); i++)
        {
            // Character by character, for the name of the one which is not valid
            if (Input.at(i).unicode() < 0x20 || Input.at(i).unicode() == 0x7F)
                return GUI_Main_Metadata::tr("A control character (code %1) cannot be written").arg((int)Input.at(i).unicode()); // As a number whatever the version of Qt
            const QString Char = Input.mid(i, Input.at(i).isHighSurrogate() ? 2 : 1);
            i += Char.size() - 1;
            const QByteArray Bytes = Char.toLocal8Bit();
            if (QString::fromLocal8Bit(Bytes) != Char)
                return GUI_Main_Metadata::tr("The character '%1' cannot be written: it is not in the character set of the system").arg(Char);
            if (Bytes.contains('\xFF'))
                return GUI_Main_Metadata::tr("The character '%1' cannot be written: its code (255) is the end of a text in the files").arg(Char);
        }
        const int Bytes_Count = (int)Input.toLocal8Bit().size();
        if (Size > 0 && Bytes_Count > Size)
            return GUI_Main_Metadata::tr("Too long: %1 bytes, %2 at most").arg(Bytes_Count).arg(Size);
        return QString();
    }

private:
    int                         Size;
};

//***************************************************************************
// GUI_Main_Metadata_Tree: the tree of the view
//***************************************************************************

class GUI_Main_Metadata_Tree : public QTreeWidget
{
public:
    explicit GUI_Main_Metadata_Tree(GUI_Main_Metadata* View_) : QTreeWidget(View_), View(View_) {}

    QString                     Hint; // Shown in place of the content when there is none

    // Not public in the tree of Qt (in all its versions)
    QTreeWidgetItem*            Item(const QModelIndex& Index) const { return itemFromIndex(Index); }
    int                         Column_Width(int Column) const { return qMax(sizeHintForColumn(Column), header()->sectionSizeHint(Column)); } // Needed by the fields and the title
    int                         Text_Width(const QString& Text, bool IsBold, bool IsItalic = false) const; // Needed by a cell for this text

protected:
    bool                        edit(const QModelIndex& Index, EditTrigger Trigger, QEvent* Event) override;
    void                        drawRow(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const override;
    void                        keyPressEvent(QKeyEvent* Event) override;
    void                        contextMenuEvent(QContextMenuEvent* Event) override;
    void                        paintEvent(QPaintEvent* Event) override;
    void                        resizeEvent(QResizeEvent* Event) override;

private:
    GUI_Main_Metadata*          View;
};

//---------------------------------------------------------------------------
int GUI_Main_Metadata_Tree::Text_Width(const QString& Text, bool IsBold, bool IsItalic) const
{
    // As the style does for the text of an item
    QFont Font = font();
    Font.setBold(IsBold);
    Font.setItalic(IsItalic);
    return QFontMetrics(Font).horizontalAdvance(Text) + 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this) + 1);
}

//---------------------------------------------------------------------------
bool GUI_Main_Metadata_Tree::edit(const QModelIndex& Index, EditTrigger Trigger, QEvent* Event)
{
    // Only the value of a field which can be changed, from any column of its line
    const gui_field* Field = View->Field_Get(itemFromIndex(Index));
    if (!Field || !Field->IsEditable || View->Main->IsBusy())
        return false;
    return QTreeWidget::edit(Index.sibling(Index.row(), Column_Value), Trigger, Event);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Tree::drawRow(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const
{
    const QTreeWidgetItem* Item_ = itemFromIndex(Index);
    if (!Item_ || Item_->type() == Item_Field)
    {
        QTreeWidget::drawRow(Painter, Option, Index);
        return;
    }

    // Tracks and groups are titles: a band on the whole line, the place of the branches included, and a line over a track
    const bool IsTrack = Item_->type() == Item_Track;
    const QRect Line(0, Option.rect.top(), viewport()->width(), Option.rect.height());
    Painter->fillRect(Line, Color_Mix(palette(), IsTrack ? 12 : 6));
    QTreeWidget::drawRow(Painter, Option, Index);
    if (IsTrack)
        Painter->fillRect(QRect(Line.left(), Line.top(), Line.width(), 1), Color_Mix(palette(), 32));
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Tree::keyPressEvent(QKeyEvent* Event)
{
    if (Event->key() == Qt::Key_Delete || Event->key() == Qt::Key_Backspace)
    {
        // A modified value is reverted. These keys never start the editing of a value: it would be emptied
        View->Field_Revert(currentItem());
        Event->accept();
        return;
    }
    if (Event->matches(QKeySequence::Copy) && View->Field_Copy(currentItem()))
    {
        Event->accept();
        return;
    }
    if ((Event->key() == Qt::Key_Return || Event->key() == Qt::Key_Enter) && state() != EditingState && edit(currentIndex(), EditKeyPressed, nullptr))
    {
        // The key which ends the editing of a value also starts it
        // (not while editing: the key of the editor comes here after it. The key is not given to the new editor: it would end the editing at once)
        Event->accept();
        return;
    }

    QTreeWidget::keyPressEvent(Event);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Tree::contextMenuEvent(QContextMenuEvent* Event)
{
    // From the keyboard: for the current item, the mouse may be anywhere
    const bool IsMouse = Event->reason() == QContextMenuEvent::Mouse;
    QTreeWidgetItem* Item_ = IsMouse ? itemAt(Event->pos()) : currentItem();
    if (!Item_)
        return;
    View->Menu_Show(Item_, IsMouse ? Event->globalPos() : viewport()->mapToGlobal(visualItemRect(Item_).center()));
    Event->accept();
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Tree::paintEvent(QPaintEvent* Event)
{
    QTreeWidget::paintEvent(Event);

    // Nothing to show: why is said where the content would be
    if (!Hint.isEmpty())
        GUI_Main_Files::Hint_Paint(this, Hint);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Tree::resizeEvent(QResizeEvent* Event)
{
    QTreeWidget::resizeEvent(Event);

    View->Columns_Fit();
}

//***************************************************************************
// GUI_Main_Metadata_Delegate: look of the items, editor of the value of a field
//***************************************************************************

class GUI_Main_Metadata_Delegate : public QStyledItemDelegate
{
public:
    explicit GUI_Main_Metadata_Delegate(GUI_Main_Metadata* View_) : QStyledItemDelegate(View_), View(View_) {}

    void                        paint(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const override;
    QWidget*                    createEditor(QWidget* Parent, const QStyleOptionViewItem& Option, const QModelIndex& Index) const override;
    void                        setEditorData(QWidget* Editor, const QModelIndex& Index) const override;
    void                        setModelData(QWidget* Editor, QAbstractItemModel* Model, const QModelIndex& Index) const override;
    void                        updateEditorGeometry(QWidget* Editor, const QStyleOptionViewItem& Option, const QModelIndex& Index) const override;
    void                        destroyEditor(QWidget* Editor, const QModelIndex& Index) const override;

protected:
    bool                        eventFilter(QObject* Object, QEvent* Event) override;
    void                        initStyleOption(QStyleOptionViewItem* Option, const QModelIndex& Index) const override;

private:
    GUI_Main_Metadata*          View;
};

//---------------------------------------------------------------------------
// What is wrong with the text of an editor (its tool tip), shown under it without waiting for the mouse; hidden if nothing is wrong
static void Problem_Show(QWidget* Editor)
{
    QToolTip::showText(Editor->mapToGlobal(QPoint(0, Editor->height() / 2)), Editor->toolTip(), Editor);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::paint(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const
{
    if (Index.parent().isValid() || Index.column() != Column_Field)
    {
        QStyledItemDelegate::paint(Painter, Option, Index);
        return;
    }

    // Track: its line is one cell, with its title at the start and the count of its files at the end,
    // under the title of its column if this column is wide enough for it
    const QModelIndex Index_Files = Index.sibling(Index.row(), Column_Files);
    QStyleOptionViewItem Option_Title = Option;
    initStyleOption(&Option_Title, Index);
    QStyleOptionViewItem Option_Files = Option;
    initStyleOption(&Option_Files, Index_Files);
    const QRect Line = Option.rect;
    const int Files_Wanted = QStyledItemDelegate::sizeHint(Option, Index_Files).width();
    int Files_Width = qMin(View->Tree->columnWidth(Column_Files), Line.width());
    if (Files_Width < Files_Wanted)
        Files_Width = qMin(Files_Wanted, Line.width() * 2 / 3); // The title keeps a part of the line
    Option_Title.rect = QStyle::visualRect(Option.direction, Line, QRect(Line.left(), Line.top(), Line.width() - Files_Width, Line.height()));
    Option_Files.rect = QStyle::visualRect(Option.direction, Line, QRect(Line.left() + Line.width() - Files_Width, Line.top(), Files_Width, Line.height()));
    Option_Title.viewItemPosition = QStyleOptionViewItem::Beginning;
    Option_Files.viewItemPosition = QStyleOptionViewItem::End;
    Option_Files.state &= ~QStyle::State_HasFocus; // Once for the line, with the title
    const QWidget* Widget = Option.widget;
    QStyle* Style = Widget ? Widget->style() : QApplication::style();
    Style->drawControl(QStyle::CE_ItemViewItem, &Option_Title, Painter, Widget);
    Style->drawControl(QStyle::CE_ItemViewItem, &Option_Files, Painter, Widget);
}

//---------------------------------------------------------------------------
QWidget* GUI_Main_Metadata_Delegate::createEditor(QWidget* Parent, const QStyleOptionViewItem&, const QModelIndex& Index) const
{
    const gui_field* Field = View->Field_Get(View->Tree->Item(Index));
    if (!Field || !Field->IsEditable || Index.column() != Column_Value || View->Main->IsBusy())
        return nullptr;

    // A text, checked while it is typed. The empty editor says what an empty text means
    QLineEdit* Editor = new QLineEdit(Parent);
    Editor->setFrame(Editor->style()->styleHint(QStyle::SH_ItemView_DrawDelegateFrame, nullptr, Editor));
    GUI_Main_Metadata_Validator* Validator;
    switch (Field->Type)
    {
        case gui_field::Type_Unsigned:
            Validator = new GUI_Main_Metadata_Validator_Unsigned(Unsigned_Max(Field->Size), Editor);
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (number from 0 to %1)").arg(Unsigned_Max(Field->Size)));
            break;
        case gui_field::Type_Float:
            Validator = new GUI_Main_Metadata_Validator_Float(Editor);
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (number)"));
            break;
        case gui_field::Type_TimeCode:
            Validator = new GUI_Main_Metadata_Validator_TimeCode(Editor);
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (HH:MM:SS:FF)"));
            break;
        default:
            Validator = new GUI_Main_Metadata_Validator_Text(Field->Size, Editor);
            Editor->setPlaceholderText(Field->Size > 0 ? GUI_Main_Metadata::tr("Not set (text, %1 bytes at most)").arg(Field->Size) : GUI_Main_Metadata::tr("Not set (text)"));
    }
    Editor->setValidator(Validator);

    // A text which is not valid is seen at once, what is wrong with it is its tool tip: the Enter key does nothing with it
    const QColor Color_Normal = Editor->palette().color(QPalette::Text);
    const QColor Color_Refused = GUI_Main_Files::Color_Error(Editor->palette());
    QObject::connect(Editor, &QLineEdit::textChanged, Editor, [Editor, Validator, Color_Normal, Color_Refused](const QString& Text)
    {
        const QString Problem_Before = Editor->toolTip();
        const QString Problem = Validator->Problem(Text);
        QPalette Palette = Editor->palette();
        Palette.setColor(QPalette::Text, Problem.isEmpty() ? Color_Normal : Color_Refused);
        Editor->setPalette(Palette);
        Editor->setToolTip(Problem);
        if (!Problem_Before.isEmpty() && QToolTip::isVisible() && QToolTip::text() == Problem_Before)
            Problem_Show(Editor); // It was about the previous text
    });
    QObject::connect(Editor, &QLineEdit::textEdited, Editor, [Editor]()
    {
        Editor->setProperty(Editor_IsEdited, true);
    });
    return Editor;
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::setEditorData(QWidget* Editor, const QModelIndex& Index) const
{
    // The value if there is one for all files, else nothing: the files have several values
    const gui_field* Field = View->Field_Get(View->Tree->Item(Index));
    QLineEdit* Edit = qobject_cast<QLineEdit*>(Editor);
    if (!Field || !Edit)
        return;
    if (Field->IsModified)
        Edit->setText(Field->NewValue);
    else if (Field->Kind == gui_field::Kind_Same)
        Edit->setText(Field->First);
    else
        Edit->clear();
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::setModelData(QWidget* Editor, QAbstractItemModel*, const QModelIndex& Index) const
{
    // Not from an editor which was only looked at: the text it starts with is not always the value (several values in the files)
    const QLineEdit* Edit = qobject_cast<QLineEdit*>(Editor);
    if (!Edit || !Edit->hasAcceptableInput() || !Edit->property(Editor_IsEdited).toBool())
        return;
    View->Field_Edit(View->Tree->Item(Index), Edit->text());
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::updateEditorGeometry(QWidget* Editor, const QStyleOptionViewItem& Option, const QModelIndex&) const
{
    Editor->setGeometry(Option.rect);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::destroyEditor(QWidget* Editor, const QModelIndex& Index) const
{
    // What was wrong with its text does not stay on the screen without it
    if (!Editor->toolTip().isEmpty() && QToolTip::isVisible() && QToolTip::text() == Editor->toolTip())
        QToolTip::hideText();

    QStyledItemDelegate::destroyEditor(Editor, Index);
}

//---------------------------------------------------------------------------
bool GUI_Main_Metadata_Delegate::eventFilter(QObject* Object, QEvent* Event)
{
    if (Event->type() == QEvent::KeyPress)
    {
        // The keys which validate do nothing with a text which is not valid: why is said at once
        const int Key = static_cast<QKeyEvent*>(Event)->key();
        QLineEdit* Editor = qobject_cast<QLineEdit*>(Object);
        if ((Key == Qt::Key_Enter || Key == Qt::Key_Return || Key == Qt::Key_Tab || Key == Qt::Key_Backtab) && Editor && !Editor->hasAcceptableInput())
            Problem_Show(Editor);
    }
    else if (Event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(Event)->matches(QKeySequence::Save))
    {
        // The value being typed is part of what the user saves: nothing is saved without it
        const QPointer<QLineEdit> Editor(qobject_cast<QLineEdit*>(Object));
        if (Editor && !Editor->hasAcceptableInput())
        {
            Problem_Show(Editor);
            Event->accept(); // The key is for the editor, not a shortcut
            return true;
        }
        if (Editor)
        {
            Q_EMIT commitData(Editor);
            if (Editor) // A question may have been asked meanwhile
                Q_EMIT closeEditor(Editor);
        }
        return false;
    }

    return QStyledItemDelegate::eventFilter(Object, Event);
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::initStyleOption(QStyleOptionViewItem* Option, const QModelIndex& Index) const
{
    QStyledItemDelegate::initStyleOption(Option, Index);

    // Tracks: when the view is too narrow the name of the track, at the end of the text, is more useful than the middle of the text
    // (not when the count of modified fields is after it)
    if (!Index.parent().isValid() && Index.column() == Column_Field && Option->text == Index.data(Role_Title).toString())
        Option->textElideMode = Qt::ElideMiddle;

    GUI_Main_Files::FocusFrame_Hide(Option);
}

//***************************************************************************
// Constructor/Destructor
//***************************************************************************

//---------------------------------------------------------------------------
GUI_Main_Metadata::GUI_Main_Metadata(GUI_Main* Main_)
    : QWidget(Main_)
    , Main(Main_)
{
    // Filter
    Filter = new QLineEdit(this);
    Filter->setPlaceholderText(tr("Filter fields"));
    Filter->setClearButtonEnabled(true);
    Filter->setMaximumWidth(320);
    Filter->setToolTip(tr("Only the fields having this text in their name or in their value are shown"));
    connect(Filter, &QLineEdit::textChanged, this, [this]()
    {
        Filter_Apply();
    });
    QShortcut* Find = new QShortcut(QKeySequence(QKeySequence::Find), this);
    Find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(Find, &QShortcut::activated, this, [this]()
    {
        Filter->setFocus(Qt::ShortcutFocusReason);
        Filter->selectAll();
    });

    // How to edit, for who does not know that the values can be changed
    Help = new QLabel(this);
    Help->setAlignment(Qt::AlignLeft | Qt::AlignVCenter); // Over the start of the values
    Help->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // No minimum width for the window from it
    QPalette Help_Palette = Help->palette();
    Help_Palette.setColor(QPalette::WindowText, Help_Palette.color(QPalette::PlaceholderText));
    Help->setPalette(Help_Palette);

    // Tree
    Tree = new GUI_Main_Metadata_Tree(this);
    Tree->setColumnCount(Column_Max);
    Tree->setHeaderLabels(QStringList() << tr("Field") << tr("Value") << tr("Files"));
    Tree->headerItem()->setToolTip(Column_Value, tr("Value of the field in the files of the track: one value if all the files have the same one, else the values of the first file and of the last file"));
    Tree->headerItem()->setToolTip(Column_Files, tr("Count of files of the track. For a field: what its value is among them, and whether the field describes the structure of the file (a wrong value makes the file unreadable)"));
    Tree->setItemDelegate(new GUI_Main_Metadata_Delegate(this));
    Tree->setSelectionMode(QAbstractItemView::SingleSelection);
    Tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    Tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    Tree->setAlternatingRowColors(false); // The bands of the tracks and of the groups are the structure of the list
    Tree->setUniformRowHeights(true);
    Tree->setSortingEnabled(false);
    Tree->header()->setSectionsMovable(false);
    Tree->header()->setStretchLastSection(false);
    Tree->header()->setSectionResizeMode(Column_Value, QHeaderView::Stretch); // Values take what the other columns do not need
    connect(Tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* Item)
    {
        if (!Updating && Filter->text().trimmed().isEmpty())
            Collapsed.insert(Item->data(Column_Field, Role_Key).toString());
    });
    connect(Tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* Item)
    {
        if (Updating)
            return;
        if (Filter->text().trimmed().isEmpty())
            Collapsed.remove(Item->data(Column_Field, Role_Key).toString());
        Columns_Measure(); // More values are shown
        Columns_Fit();
    });
    connect(Tree->header(), &QHeaderView::sectionResized, this, [this](int Column)
    {
        // A width set by the user, for the content which is shown
        if (Columns_Fitting || !Columns_Wanted_Field || !isVisible())
            return;
        if (Column == Column_Field)
            Columns_User_Field = true;
        if (Column == Column_Files)
            Columns_User_Files = true;
    });

    // Layout
    QHBoxLayout* Top = new QHBoxLayout();
    Top->setContentsMargins(6, 6, 6, 6);
    Top->setSpacing(12);
    Top->addWidget(Filter, 1);
    Top->addWidget(Help, 2);
    QVBoxLayout* Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    Layout->setSpacing(0);
    Layout->addLayout(Top);
    Layout->addWidget(Tree);

    // The tree is what the keyboard is for when the view is shown, the filter comes after it
    setFocusProxy(Tree);
    setTabOrder(Tree, Filter);

    Filter_Apply(); // Text of the empty view
}

//***************************************************************************
// Content
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Refresh()
{
    // What the user looks at is found again after the rebuild
    QString Current_Key;
    qlonglong Current_Field = -1;
    const int Current_Column = Tree->currentColumn();
    if (const QTreeWidgetItem* Item = Tree->currentItem())
    {
        if (Item->type() == Item_Field)
        {
            Current_Key = Item_Track_Get(Item)->data(Column_Field, Role_Key).toString();
            Current_Field = Item->data(Column_Field, Role_Field).toLongLong();
        }
        else
            Current_Key = Item->data(Column_Field, Role_Key).toString();
    }
    const int Scroll = Tree->verticalScrollBar()->value();

    // Rebuild
    Updating = true;
    Generation++;
    Tree->setUpdatesEnabled(false);
    Tree->clear();
    QTreeWidgetItem* Current = nullptr;
    bool HasEditable = false;
    QFont Bold;
    Bold.setBold(true);
    const QPalette Palette = Tree->palette();
    for (size_t i = 0; i < Main->Packages.size(); i++)
    {
        const gui_package& Package = Main->Packages[i];
        if (Package.Kind != gui_package::Kind_Uncompressed)
            continue;
        for (size_t j = 0; j < Package.Tracks.size(); j++)
        {
            const gui_track& Track = Package.Tracks[j];
            const QString Key = Track_Key(Package, Track);
            QTreeWidgetItem* Item_T = nullptr;
            QTreeWidgetItem* Item_G = nullptr;
            for (size_t k = 0; k < Track.Fields.size(); k++)
            {
                const gui_field& Field = Track.Fields[k];
                if (!Field.IsEditable && !Field.HasValue())
                    continue; // Not in the files

                // Track
                if (!Item_T)
                {
                    QString Title = Package.Name();
                    if (Title != Track.Name) // Else a file opened alone
                        Title += QLatin1String(" / ") + Track.Name;
                    QString ToolTip = QLatin1String("<qt><b>") + Title.toHtmlEscaped() + QLatin1String("</b>");
                    if (!Track.Flavor.isEmpty())
                        ToolTip += QLatin1String("<br>") + Track.Flavor.toHtmlEscaped();
                    ToolTip += QLatin1String("<br>") + GUI_Main_Files::Track_Files(Track).toHtmlEscaped();
                    ToolTip += QLatin1String("<br>") + Track.FileName.toHtmlEscaped() + QLatin1String("</qt>");
                    Item_T = new QTreeWidgetItem(Item_Track);
                    Item_T->setData(Column_Field, Role_Package, (qulonglong)i);
                    Item_T->setData(Column_Field, Role_Track, (qulonglong)j);
                    Item_T->setData(Column_Field, Role_Key, Key);
                    Item_T->setData(Column_Field, Role_Title, Title);
                    Item_T->setText(Column_Files, GUI_Main_Files::Track_Files(Track));
                    for (int Column = 0; Column < Column_Max; Column++)
                    {
                        Item_T->setFont(Column, Bold);
                        Item_T->setToolTip(Column, ToolTip);
                    }
                }

                // Group
                if (!Item_G || Item_G->data(Column_Field, Role_Title).toString() != Field.Group)
                {
                    Item_G = nullptr;
                    for (int Group = 0; Group < Item_T->childCount() && !Item_G; Group++)
                        if (Item_T->child(Group)->data(Column_Field, Role_Title).toString() == Field.Group)
                            Item_G = Item_T->child(Group);
                }
                if (!Item_G)
                {
                    Item_G = new QTreeWidgetItem(Item_T, Item_Group);
                    Item_G->setData(Column_Field, Role_Key, QString(Key + QLatin1Char('\n') + Field.Group));
                    Item_G->setData(Column_Field, Role_Title, Field.Group);
                    if (Item_G->data(Column_Field, Role_Key).toString() == Current_Key)
                        Current = Item_G;
                }

                // Field
                QTreeWidgetItem* Item_F = new QTreeWidgetItem(Item_G, Item_Field);
                Item_F->setData(Column_Field, Role_Field, (qulonglong)k);
                Item_F->setFlags(Field.IsEditable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable) : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
                Field_Show(Item_F, Field, Track, Palette);
                if ((qlonglong)k == Current_Field && Key == Current_Key)
                    Current = Item_F;
                if (Field.IsEditable)
                    HasEditable = true;
            }
            if (!Item_T)
                continue; // Nothing to show for this track
            Item_Track_Update(Item_T);
            Tree->addTopLevelItem(Item_T);
            if (Current_Field < 0 && Key == Current_Key)
                Current = Item_T;
        }
    }
    for (int i = 0; i < Tree->topLevelItemCount(); i++)
    {
        // Titles, not cells of the columns: they have the place of the whole line, whatever the widths of the columns
        QTreeWidgetItem* Item_T = Tree->topLevelItem(i);
        Item_T->setFirstColumnSpanned(true);
        for (int j = 0; j < Item_T->childCount(); j++)
            Item_T->child(j)->setFirstColumnSpanned(true);
    }
    Filter_Apply();
    Help->setText(HasEditable ? tr("Double-click a value to edit it") : QString()); // Not hidden: it keeps the filter at the left

    // Back to what the user was looking at
    if (Current && !Current->isHidden())
        Tree->setCurrentItem(Current, Current_Column < 0 ? 0 : Current_Column);
    Tree->doItemsLayout(); // The range of the scroll bar is the one of the new content
    Tree->verticalScrollBar()->setValue(Scroll);
    Tree->setUpdatesEnabled(true);
    Updating = false;

    Columns_Measure();
    Columns_Fit();
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Track_Show(size_t Package, size_t Track)
{
    QTreeWidgetItem* Item = Item_Find(Package, Track);
    if (!Item || !Track_Get(Item))
    {
        // The tree is not the one of the current content
        Refresh();
        Item = Item_Find(Package, Track);
    }
    if (!Item)
        return; // No metadata for this track

    if (Item->isHidden())
        Filter->clear(); // No field of the track has the text of the filter
    const QString Key = Item->data(Column_Field, Role_Key).toString();
    Collapsed.remove(Key);
    Item->setExpanded(true);
    Tree->setCurrentItem(Item);
    Tree->scrollToItem(Item, QAbstractItemView::PositionAtTop);
    Tree->setFocus();
    Track_ToShow = isVisible() ? QString() : Key; // The place depends on the size of the view, not known while it is hidden
}

//---------------------------------------------------------------------------
gui_track* GUI_Main_Metadata::Track_Get(const QTreeWidgetItem* Item) const
{
    Item = Item_Track_Get(Item);
    if (!Item)
        return nullptr;
    const size_t Package_Pos = (size_t)Item->data(Column_Field, Role_Package).toULongLong();
    const size_t Track_Pos = (size_t)Item->data(Column_Field, Role_Track).toULongLong();
    if (Package_Pos >= Main->Packages.size())
        return nullptr;
    gui_package& Package = Main->Packages[Package_Pos];
    if (Package.Kind != gui_package::Kind_Uncompressed || Track_Pos >= Package.Tracks.size())
        return nullptr;
    gui_track& Track = Package.Tracks[Track_Pos];
    if (Track_Key(Package, Track) != Item->data(Column_Field, Role_Key).toString())
        return nullptr;
    return &Track;
}

//---------------------------------------------------------------------------
gui_field* GUI_Main_Metadata::Field_Get(const QTreeWidgetItem* Item) const
{
    if (!Item || Item->type() != Item_Field)
        return nullptr;
    gui_track* Track = Track_Get(Item);
    const size_t Field_Pos = (size_t)Item->data(Column_Field, Role_Field).toULongLong();
    if (!Track || Field_Pos >= Track->Fields.size())
        return nullptr;
    gui_field& Field = Track->Fields[Field_Pos];
    if (Field.Name != Item->text(Column_Field))
        return nullptr;
    return &Field;
}

//***************************************************************************
// Items
//***************************************************************************

//---------------------------------------------------------------------------
QTreeWidgetItem* GUI_Main_Metadata::Item_Find(size_t Package, size_t Track) const
{
    for (int i = 0; i < Tree->topLevelItemCount(); i++)
    {
        QTreeWidgetItem* Item = Tree->topLevelItem(i);
        if (Item->data(Column_Field, Role_Package).toULongLong() == Package && Item->data(Column_Field, Role_Track).toULongLong() == Track)
            return Item;
    }
    return nullptr;
}

//---------------------------------------------------------------------------
bool GUI_Main_Metadata::Editor_Close()
{
    return true;
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Item_Field_Update(QTreeWidgetItem* Item)
{
    const gui_field* Field = Field_Get(Item);
    const gui_track* Track = Track_Get(Item);
    if (Field && Track)
        Field_Show(Item, *Field, *Track, Tree->palette());
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Item_Track_Update(QTreeWidgetItem* Item)
{
    Item = Item_Track_Get(Item);
    const gui_track* Track = Track_Get(Item);
    if (!Track)
        return;

    // Modified fields are seen from the closed groups and tracks too
    const QString Mark = QStringLiteral(" *");
    int Track_Modified = 0;
    for (int i = 0; i < Item->childCount(); i++)
    {
        QTreeWidgetItem* Group = Item->child(i);
        int Group_Modified = 0;
        for (int j = 0; j < Group->childCount(); j++)
        {
            const size_t Field_Pos = (size_t)Group->child(j)->data(Column_Field, Role_Field).toULongLong();
            if (Field_Pos < Track->Fields.size() && Track->Fields[Field_Pos].IsModified)
                Group_Modified++;
        }
        const QString Title = Group->data(Column_Field, Role_Title).toString();
        Group->setText(Column_Field, Group_Modified ? Title + Mark : Title);
        Track_Modified += Group_Modified;
    }
    QString Title = Item->data(Column_Field, Role_Title).toString();
    if (Track_Modified)
    {
        Title += Mark;
        Title += QLatin1Char(' ');
        Title += Track_Modified == 1 ? tr("%n modified field, not saved", nullptr, Track_Modified) : tr("%n modified fields, not saved", nullptr, Track_Modified);
    }
    Item->setText(Column_Field, Title);
}

//***************************************************************************
// Edition
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Main_Metadata::Field_Edit(QTreeWidgetItem* Item, const QString& Text)
{
    gui_field* Field = Field_Get(Item);
    const gui_track* Track = Track_Get(Item);
    if (!Field || !Track || !Field->IsEditable || Main->IsBusy())
        return false;

    // The value as it will be read back from the files
    QString Value = Text;
    if (Field->Type == gui_field::Type_Unsigned && !Value.isEmpty())
    {
        const quint64 Number = Value.toULongLong();
        Value = Field->Size > 0 && Number == Unsigned_Max(Field->Size) ? QString() : QString::number(Number); // All bits set is how a field which is not set is written
    }

    // Is it a change?
    const bool HasValue = Field->IsModified || Field->Kind == gui_field::Kind_Same; // Else several values in the files, the editor shows none
    if (HasValue && Value_IsSame(*Field, Value, Field->IsModified ? Field->NewValue : Field->First))
        return false;

    // Back to the value of the files
    if (Field->IsModified && Field->Kind == gui_field::Kind_Same && Value_IsSame(*Field, Value, Field->First))
    {
        Field_Revert(Item);
        return true;
    }

    // What the value alone does not show is confirmed by the user, who may have validated the editor by habit: nothing is done by default
    QStringList Question; // Why it is asked, then what will be done
    if (!HasValue)
        Question += tr("This field does not have the same value in all files (%1).").arg(Field->Display());
    if (Field->IsStructural)
        Question += tr("This field describes the structure of the files: a wrong value makes them unreadable.");
    if (!Question.isEmpty())
    {
        const int Count = (int)qMin<quint64>(Track->FileCount, INT_MAX);
        if (Value.isEmpty())
            Question += Count > 1 ? tr("The field will be not set in the %n files.", nullptr, Count) : tr("The field will be not set in the file.");
        else
            Question += (Count > 1 ? tr("The value '%1' will be written in the %n files.", nullptr, Count) : tr("The value '%1' will be written in the file.")).arg(Value);
        const size_t Generation_Before = Generation;
        QMessageBox Box(QMessageBox::Question, tr("RAWcooked - Metadata"), Question.join(QLatin1String("\n\n")), QMessageBox::Cancel, this);
        Box.setTextFormat(Qt::PlainText); // Values of the files are not rich text
        const QAbstractButton* Replace = Box.addButton(tr("&Replace"), QMessageBox::AcceptRole);
        Box.setDefaultButton(QMessageBox::Cancel);
        Box.setEscapeButton(QMessageBox::Cancel);
        Box.exec();
        if (Box.clickedButton() != Replace || Generation != Generation_Before || Main->IsBusy())
            return false;
        Field = Field_Get(Item); // The content may have changed while the question was shown
        if (!Field)
            return false;
    }

    Field->IsModified = true;
    Field->NewValue = Value;
    Item_Field_Update(Item);
    Item_Track_Update(Item);
    Columns_Measure(); // The new value may need more place than the other ones
    Columns_Fit();
    Q_EMIT Modified();
    return true;
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Field_Revert(QTreeWidgetItem* Item)
{
    gui_field* Field = Field_Get(Item);
    if (!Field || !Field->IsModified || Main->IsBusy())
        return;

    Field->IsModified = false;
    Field->NewValue.clear();
    Item_Field_Update(Item);
    Item_Track_Update(Item);
    Columns_Measure();
    Columns_Fit();
    Q_EMIT Modified();
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Track_Revert(QTreeWidgetItem* Item)
{
    Item = Item_Track_Get(Item);
    gui_track* Track = Track_Get(Item);
    if (!Track || !Track->IsModified() || Main->IsBusy())
        return;

    for (gui_field& Field : Track->Fields)
    {
        Field.IsModified = false;
        Field.NewValue.clear();
    }
    for (int i = 0; i < Item->childCount(); i++)
        for (int j = 0; j < Item->child(i)->childCount(); j++)
            Item_Field_Update(Item->child(i)->child(j));
    Item_Track_Update(Item);
    Columns_Measure();
    Columns_Fit();
    Q_EMIT Modified();
}

//---------------------------------------------------------------------------
bool GUI_Main_Metadata::Field_Copy(QTreeWidgetItem* Item)
{
    const gui_field* Field = Field_Get(Item);
    if (!Field)
        return false;
    QApplication::clipboard()->setText(Field->Display());
    return true;
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Menu_Show(QTreeWidgetItem* Item, const QPoint& Position)
{
    const gui_track* Track = Track_Get(Item);
    if (!Track)
        return;
    const gui_field* Field = Field_Get(Item);
    const bool CanChange = !Main->IsBusy();

    QMenu Menu(this);
    QAction* Edit = nullptr;
    QAction* Revert = nullptr;
    QAction* Copy = nullptr;
    if (Field && Field->IsEditable)
    {
        Edit = Menu.addAction(tr("Edit value"));
        Edit->setEnabled(CanChange);
    }
    if (Field && Field->IsModified)
    {
        Revert = Menu.addAction(tr("Revert this field"));
        Revert->setEnabled(CanChange);
    }
    QAction* Revert_All = Menu.addAction(tr("Revert all fields of this track"));
    Revert_All->setEnabled(CanChange && Track->IsModified());
    if (Field)
    {
        Menu.addSeparator();
        Copy = Menu.addAction(tr("Copy value"));
    }

    const size_t Generation_Before = Generation;
    const QAction* Action = Menu.exec(Position);
    if (!Action || Generation != Generation_Before)
        return; // The items of the tree may be other ones
    if (Action == Edit)
    {
        Tree->setCurrentItem(Item, Column_Value);
        Tree->editItem(Item, Column_Value);
    }
    else if (Action == Revert)
        Field_Revert(Item);
    else if (Action == Revert_All)
        Track_Revert(Item);
    else if (Action == Copy)
        Field_Copy(Item);
}

//***************************************************************************
// View
//***************************************************************************

//---------------------------------------------------------------------------
void GUI_Main_Metadata::Filter_Apply()
{
    // Fields having the text in their name or in their value, with their group and their track
    // While there is a filter everything shown is open, without change of what the user closed
    const QString Text = Filter->text().trimmed();
    const bool WasUpdating = Updating;
    Updating = true;
    bool HasMatch = false;
    for (int i = 0; i < Tree->topLevelItemCount(); i++)
    {
        QTreeWidgetItem* Track = Tree->topLevelItem(i);
        bool Track_HasMatch = false;
        for (int j = 0; j < Track->childCount(); j++)
        {
            QTreeWidgetItem* Group = Track->child(j);
            bool Group_HasMatch = false;
            for (int k = 0; k < Group->childCount(); k++)
            {
                QTreeWidgetItem* Field = Group->child(k);
                const bool IsMatch = Text.isEmpty() || Field->text(Column_Field).contains(Text, Qt::CaseInsensitive) || Field->data(Column_Value, Role_Value).toString().contains(Text, Qt::CaseInsensitive);
                Field->setHidden(!IsMatch);
                if (IsMatch)
                    Group_HasMatch = true;
            }
            Group->setHidden(!Group_HasMatch);
            Group->setExpanded(!Text.isEmpty() || !Collapsed.contains(Group->data(Column_Field, Role_Key).toString()));
            if (Group_HasMatch)
                Track_HasMatch = true;
        }
        Track->setHidden(!Track_HasMatch);
        Track->setExpanded(!Text.isEmpty() || !Collapsed.contains(Track->data(Column_Field, Role_Key).toString()));
        if (Track_HasMatch)
            HasMatch = true;
    }
    Updating = WasUpdating;

    // Empty view
    if (!Tree->topLevelItemCount())
    {
        // Matroska files have none: what is opened is not the reason
        bool HasCompressed = false;
        for (const gui_package& Package : Main->Packages)
            if (Package.Kind == gui_package::Kind_Compressed)
                HasCompressed = true;
        Tree->Hint = HasCompressed ? tr("The metadata of the original files is not shown for a Matroska file: decode it and open the decoded files.")
                                   : tr("No metadata. Open a directory or DPX, TIFF, EXR, WAV, AIFF or AVI files.");
    }
    else if (!HasMatch)
        Tree->Hint = tr("No field has this text in its name or in its value.");
    else
        Tree->Hint.clear();
    Tree->viewport()->update();
}

//---------------------------------------------------------------------------
// Width the columns need for their content
void GUI_Main_Metadata::Columns_Measure()
{
    Columns_Wanted_Field = 0;
    if (!Tree->topLevelItemCount())
    {
        // As at the start: the widths, the ones of the user too, were for a content which is not there any more
        Columns_User_Field = false;
        Columns_User_Files = false;
        Columns_Fitting = true;
        Tree->setColumnWidth(Column_Field, Tree->header()->defaultSectionSize());
        Tree->setColumnWidth(Column_Files, Tree->header()->defaultSectionSize());
        Columns_Fitting = false;
        return;
    }

    // Names of all the fields, shown or not, as when they are modified (bold): the columns do not move
    // when a track is opened, when the filter changes or at an edit
    const int Indentation = Tree->indentation() * 3; // Of the fields, with the place of the branches of the tracks
    int Field = Tree->header()->sectionSizeHint(Column_Field);
    int Files = Tree->header()->sectionSizeHint(Column_Files);
    QHash<QString, int> Names[2]; // Tracks often have the same fields. Second one: names in italic
    for (int i = 0; i < Tree->topLevelItemCount(); i++)
    {
        const QTreeWidgetItem* Track = Tree->topLevelItem(i);
        Files = qMax(Files, Tree->Text_Width(Track->text(Column_Files), true));
        for (int j = 0; j < Track->childCount(); j++)
        {
            const QTreeWidgetItem* Group = Track->child(j);
            for (int k = 0; k < Group->childCount(); k++)
            {
                const QString Name = Group->child(k)->text(Column_Field);
                const bool IsItalic = Group->child(k)->font(Column_Field).italic();
                auto Width = Names[IsItalic].find(Name);
                if (Width == Names[IsItalic].end())
                    Width = Names[IsItalic].insert(Name, Tree->Text_Width(Name, true, IsItalic));
                Field = qMax(Field, Indentation + Width.value());
            }
        }
    }
    Columns_Wanted_Field = Field + GUI_Main_Files::Column_Margin;

    // The longest of the usual texts of the last column for a field (the text of a field which is two things at once may be cut)
    gui_track Probe_Track;
    Probe_Track.FileCount = 2;
    gui_field Probe;
    int Files_Fields = Tree->header()->sectionSizeHint(Column_Files);
    for (const gui_field::kind Kind : { gui_field::Kind_Sequence, gui_field::Kind_Mixed })
    {
        Probe.Kind = Kind;
        Files_Fields = qMax(Files_Fields, Tree->Text_Width(Field_Files(Probe, Probe_Track), false));
    }
    Probe.IsModified = true;
    Files_Fields = qMax(Files_Fields, Tree->Text_Width(Field_Files(Probe, Probe_Track), true));
    Probe = gui_field();
    Probe.IsStructural = true;
    Files_Fields = qMax(Files_Fields, Tree->Text_Width(Field_Files(Probe, Probe_Track), false));
    Columns_Wanted_Files_Fields = Files_Fields + GUI_Main_Files::Column_Margin;
    Columns_Wanted_Files = qMax(Columns_Wanted_Files_Fields, Files + GUI_Main_Files::Column_Margin);

    // Values: the ones which are shown
    Columns_Wanted_Value = Tree->Column_Width(Column_Value) + GUI_Main_Files::Column_Margin;
}

//---------------------------------------------------------------------------
// Widths of the columns for the content and the size of the view, but the widths set by the user. The values take what remains
void GUI_Main_Metadata::Columns_Fit()
{
    if (Columns_Fitting || !Columns_Wanted_Field || !isVisible())
        return;

    // The names do not take the place of the values.
    // If the values lack place, the last column only keeps what its fields need (the count of files of a track is written from where it needs)
    const int Available = Tree->viewport()->width();
    const int Field_Max = qMax(Available * Field_Percent_Max / 100, Field_Lines_Min * Tree->fontMetrics().height());
    const int Field = Columns_User_Field ? Tree->columnWidth(Column_Field) : qMin(Columns_Wanted_Field, Field_Max);
    int Files = Columns_Wanted_Files;
    if (Field + Columns_Wanted_Value + Files > Available)
        Files = qMax(Columns_Wanted_Files_Fields, Available - Field - Columns_Wanted_Value);

    Columns_Fitting = true;
    if (!Columns_User_Files)
        Tree->setColumnWidth(Column_Files, Files);
    if (!Columns_User_Field)
        Tree->setColumnWidth(Column_Field, Field);
    Columns_Fitting = false;
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata::showEvent(QShowEvent* Event)
{
    QWidget::showEvent(Event);

    // What depends on the size of the view could not be done while it was hidden
    Columns_Fit();
    if (!Track_ToShow.isEmpty())
    {
        for (int i = 0; i < Tree->topLevelItemCount(); i++)
            if (Tree->topLevelItem(i)->data(Column_Field, Role_Key).toString() == Track_ToShow)
                Tree->scrollToItem(Tree->topLevelItem(i), QAbstractItemView::PositionAtTop);
        Track_ToShow.clear();
    }
}
