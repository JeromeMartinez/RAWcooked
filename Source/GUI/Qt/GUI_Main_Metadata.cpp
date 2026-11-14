/*  Copyright (c) MediaArea.net SARL & Reto Kromer.
 *
 *  Use of this source code is governed by a BSD-style license that can
 *  be found in the License.html file in the root of the source tree.
 */

//---------------------------------------------------------------------------
#include "GUI/Qt/GUI_Main_Metadata.h"
#include "GUI/Qt/GUI_Main.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cfloat>
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
};

//---------------------------------------------------------------------------
// Property of an editor: the user validated its content with the Enter key
static const char* const Editor_IsValidated = "IsValidated";

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
// Count of files of a track and, for a sequence, the numbers of the first and last ones
static QString Track_Files(const gui_track& Track)
{
    if (!Track.FileCount)
        return QString();
    QString Text = Track.FileCount == 1 ? GUI_Main_Metadata::tr("1 file") : GUI_Main_Metadata::tr("%1 files").arg(Track.FileCount);
    if (!Track.Numbers.isEmpty())
        Text += QLatin1String(" (") + Track.Numbers + QLatin1Char(')');
    return Text;
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
// What the value shown for a field is, among the files of its track
static QString Field_Files(const gui_field& Field, const gui_track& Track)
{
    if (Field.IsModified)
        return Track.FileCount > 1 ? GUI_Main_Metadata::tr("all files, not saved") : GUI_Main_Metadata::tr("not saved");
    switch (Field.Kind)
    {
        case gui_field::Kind_Sequence:
            return GUI_Main_Metadata::tr("first --> last file");
        case gui_field::Kind_Mixed:
            return GUI_Main_Metadata::tr("different values");
        default:
            return QString();
    }
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
        Lines += GUI_Main_Metadata::tr("Can not be edited");
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
    Item->setText(Column_Field, Field.Name);
    Item->setText(Column_Value, Field.Display());
    Item->setText(Column_Files, Field_Files(Field, Track));

    QFont Bold;
    Bold.setBold(true);
    const QString ToolTip = Field_ToolTip(Field, Track);
    for (int i = 0; i < Column_Max; i++)
    {
        if (Field.IsModified)
        {
            // Green, with a text which can be read on it whatever the colors of the system
            Item->setFont(i, Bold);
            Item->setBackground(i, QColor(144, 238, 144));
            Item->setForeground(i, QColor(0, 0, 0));
        }
        else
        {
            Item->setData(i, Qt::FontRole, QVariant());
            Item->setData(i, Qt::BackgroundRole, QVariant());
            Item->setData(i, Qt::ForegroundRole, QVariant());
        }
        Item->setToolTip(i, ToolTip);
    }
    if (!Field.IsEditable)
        Item->setForeground(Column_Value, Color_Mix(Palette, 55)); // Lighter, as what can not be changed, but it is content to read: not as light as a disabled text
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
// An empty text is always accepted: the field is not set
//***************************************************************************

//---------------------------------------------------------------------------
// Decimal digits, and the number fits in the bytes of the field (the library would truncate it)
class GUI_Main_Metadata_Validator_Unsigned : public QValidator
{
public:
    GUI_Main_Metadata_Validator_Unsigned(quint64 Max_, QObject* Parent) : QValidator(Parent), Max(Max_) {}

    State validate(QString& Input, int&) const override
    {
        for (int i = 0; i < Input.size(); i++)
            if (Input.at(i) < QLatin1Char('0') || Input.at(i) > QLatin1Char('9'))
                return Invalid;
        if (Input.isEmpty())
            return Acceptable;
        bool IsOk;
        const quint64 Value = Input.toULongLong(&IsOk);
        return IsOk && Value <= Max ? Acceptable : Invalid;
    }

private:
    quint64                     Max;
};

//---------------------------------------------------------------------------
// Number as the C library reads it (the library uses it), which can be stored in the 32 bits of the field
class GUI_Main_Metadata_Validator_Float : public QDoubleValidator
{
public:
    explicit GUI_Main_Metadata_Validator_Float(QObject* Parent) : QDoubleValidator(Parent)
    {
        QLocale Locale = QLocale::c();
        Locale.setNumberOptions(QLocale::OmitGroupSeparator | QLocale::RejectGroupSeparator); // The C library stops at a group separator
        setLocale(Locale);
        setRange(-FLT_MAX, FLT_MAX, decimals());
    }

    State validate(QString& Input, int& Position) const override
    {
        if (Input.isEmpty())
            return Acceptable;
        const State Result = QDoubleValidator::validate(Input, Position);
        if (Result == Acceptable)
        {
            const double Value = locale().toDouble(Input);
            if (Value != 0 && Value > -FLT_MIN && Value < FLT_MIN)
                return Intermediate; // Too small for the field, the C library rejects it
        }
        return Result;
    }
};

//---------------------------------------------------------------------------
// HH:MM:SS:FF with decimal digits, the only form the library reads as a time code
class GUI_Main_Metadata_Validator_TimeCode : public QValidator
{
public:
    explicit GUI_Main_Metadata_Validator_TimeCode(QObject* Parent) : QValidator(Parent) {}

    State validate(QString& Input, int& Position) const override
    {
        bool IsRegular = true; // Digits and separators are at their places so far
        for (int i = 0; i < Input.size(); i++)
        {
            const QChar Char = Input.at(i);
            const bool IsDigit = Char >= QLatin1Char('0') && Char <= QLatin1Char('9');
            if (!IsDigit && Char != QLatin1Char(':'))
                return Invalid;
            if (!IsRegular)
                continue;
            if (i % 3 != 2)
                IsRegular = IsDigit; // Else a digit was removed: the text can be completed, it is not changed
            else if (IsDigit)
            {
                // The separator is typed for the user
                Input.insert(i, QLatin1Char(':'));
                if (Position > i)
                    Position++;
            }
        }
        if (Input.size() > 11)
            return Invalid;
        return Input.isEmpty() || (IsRegular && Input.size() == 11) ? Acceptable : Intermediate;
    }
};

//---------------------------------------------------------------------------
// Text which the library can receive (local 8-bit encoding, as its file names) and which fits in the bytes of the field
class GUI_Main_Metadata_Validator_Text : public QValidator
{
public:
    GUI_Main_Metadata_Validator_Text(int Size_, QObject* Parent) : QValidator(Parent), Size(Size_) {}

    State validate(QString& Input, int&) const override
    {
        for (int i = 0; i < Input.size(); i++)
            if (Input.at(i).unicode() < 0x20 || Input.at(i).unicode() == 0x7F)
                return Invalid; // Not a text
        const QByteArray Bytes = Input.toLocal8Bit();
        if (Size > 0 && Bytes.size() > Size)
            return Invalid;
        if (QString::fromLocal8Bit(Bytes) != Input)
            return Invalid; // A character is not in the encoding
        return Acceptable;
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
    int                         Text_Width(const QString& Text, bool IsBold) const; // Needed by a cell for this text

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
int GUI_Main_Metadata_Tree::Text_Width(const QString& Text, bool IsBold) const
{
    // As the style does for the text of an item
    QFont Font = font();
    Font.setBold(IsBold);
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
    if (Hint.isEmpty())
        return;
    QPainter Painter(viewport());
    QFont Font = font();
    if (Font.pointSizeF() > 0)
        Font.setPointSizeF(Font.pointSizeF() * 1.25);
    Painter.setFont(Font);
    Painter.setPen(palette().color(QPalette::PlaceholderText));
    Painter.drawText(viewport()->rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, Hint);
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

protected:
    bool                        eventFilter(QObject* Object, QEvent* Event) override;
    void                        initStyleOption(QStyleOptionViewItem* Option, const QModelIndex& Index) const override;

private:
    GUI_Main_Metadata*          View;
};

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::paint(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const
{
    if (Index.parent().isValid() || Index.column() != Column_Field)
    {
        QStyledItemDelegate::paint(Painter, Option, Index);
        return;
    }

    // Track: its line is one cell, with its title at the start and the count of its files at the end.
    // The count is under the title of its column if this column is wide enough, else it takes the place it needs
    // (the column may have only the width its fields need, when the values need the place)
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
    switch (Field->Type)
    {
        case gui_field::Type_Unsigned:
            Editor->setValidator(new GUI_Main_Metadata_Validator_Unsigned(Unsigned_Max(Field->Size), Editor));
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (number from 0 to %1)").arg(Unsigned_Max(Field->Size)));
            break;
        case gui_field::Type_Float:
            Editor->setValidator(new GUI_Main_Metadata_Validator_Float(Editor));
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (number)"));
            break;
        case gui_field::Type_TimeCode:
            Editor->setValidator(new GUI_Main_Metadata_Validator_TimeCode(Editor));
            Editor->setPlaceholderText(GUI_Main_Metadata::tr("Not set (HH:MM:SS:FF)"));
            break;
        default:
            Editor->setValidator(new GUI_Main_Metadata_Validator_Text(Field->Size, Editor));
            Editor->setPlaceholderText(Field->Size > 0 ? GUI_Main_Metadata::tr("Not set (text, %1 bytes at most)").arg(Field->Size) : GUI_Main_Metadata::tr("Not set (text)"));
    }

    // A text which is not taken as it is (incomplete, out of range) is seen at once: the Enter key does nothing with it
    const QColor Color_Normal = Editor->palette().color(QPalette::Text);
    const QColor Color_Refused = Editor->palette().color(QPalette::Base).lightness() < 128 ? QColor(255, 110, 110) : QColor(200, 0, 0);
    QObject::connect(Editor, &QLineEdit::textChanged, Editor, [Editor, Color_Normal, Color_Refused]()
    {
        QPalette Palette = Editor->palette();
        Palette.setColor(QPalette::Text, Editor->hasAcceptableInput() ? Color_Normal : Color_Refused);
        Editor->setPalette(Palette);
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
    const QLineEdit* Edit = qobject_cast<QLineEdit*>(Editor);
    if (!Edit || !Edit->hasAcceptableInput())
        return;
    View->Field_Edit(View->Tree->Item(Index), Edit->text(), Edit->isModified() || Edit->property(Editor_IsValidated).toBool());
}

//---------------------------------------------------------------------------
void GUI_Main_Metadata_Delegate::updateEditorGeometry(QWidget* Editor, const QStyleOptionViewItem& Option, const QModelIndex&) const
{
    Editor->setGeometry(Option.rect);
}

//---------------------------------------------------------------------------
bool GUI_Main_Metadata_Delegate::eventFilter(QObject* Object, QEvent* Event)
{
    if (Event->type() == QEvent::KeyPress)
    {
        const int Key = static_cast<QKeyEvent*>(Event)->key();
        if (Key == Qt::Key_Enter || Key == Qt::Key_Return)
            Object->setProperty(Editor_IsValidated, true);
    }
    else if (Event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(Event)->matches(QKeySequence::Save))
    {
        // The value being typed is part of what the user saves
        const QPointer<QLineEdit> Editor(qobject_cast<QLineEdit*>(Object));
        if (Editor && Editor->hasAcceptableInput())
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

    // Whole lines are selected: a frame around the current cell only says something when the keyboard is used
    if (!(Option->state & QStyle::State_KeyboardFocusChange))
        Option->state &= ~QStyle::State_HasFocus;
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
    Help->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    Help->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // No minimum width for the window from it
    QPalette Help_Palette = Help->palette();
    Help_Palette.setColor(QPalette::WindowText, Help_Palette.color(QPalette::PlaceholderText));
    Help->setPalette(Help_Palette);

    // Tree
    Tree = new GUI_Main_Metadata_Tree(this);
    Tree->setColumnCount(Column_Max);
    Tree->setHeaderLabels(QStringList() << tr("Field") << tr("Value") << tr("Files"));
    Tree->headerItem()->setToolTip(Column_Value, tr("Value of the field in the files of the track: one value if all the files have the same one, else the values of the first file and of the last file"));
    Tree->headerItem()->setToolTip(Column_Files, tr("Count of files of the track, and for a field what its value is among them"));
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
                    ToolTip += QLatin1String("<br>") + Track_Files(Track).toHtmlEscaped();
                    ToolTip += QLatin1String("<br>") + Track.FileName.toHtmlEscaped() + QLatin1String("</qt>");
                    Item_T = new QTreeWidgetItem(Item_Track);
                    Item_T->setData(Column_Field, Role_Package, (qulonglong)i);
                    Item_T->setData(Column_Field, Role_Track, (qulonglong)j);
                    Item_T->setData(Column_Field, Role_Key, Key);
                    Item_T->setData(Column_Field, Role_Title, Title);
                    Item_T->setText(Column_Files, Track_Files(Track));
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
    Help->setText(HasEditable ? tr("Double click on a value to edit it") : QString()); // Not hidden: it keeps the filter at the left

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
    size_t Track_Modified = 0;
    for (int i = 0; i < Item->childCount(); i++)
    {
        QTreeWidgetItem* Group = Item->child(i);
        size_t Group_Modified = 0;
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
        Title += Track_Modified == 1 ? tr("1 modified field, not saved") : tr("%1 modified fields, not saved").arg((qulonglong)Track_Modified);
    }
    Item->setText(Column_Field, Title);
}

//***************************************************************************
// Edition
//***************************************************************************

//---------------------------------------------------------------------------
bool GUI_Main_Metadata::Field_Edit(QTreeWidgetItem* Item, const QString& Text, bool IsDeliberate)
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
    if (!HasValue && Value.isEmpty() && !IsDeliberate)
        return false; // The editor was opened and closed, the user did not ask for the field to be not set

    // Back to the value of the files
    if (Field->IsModified && Field->Kind == gui_field::Kind_Same && Value_IsSame(*Field, Value, Field->First))
    {
        Field_Revert(Item);
        return true;
    }

    // Risks
    QStringList Risks;
    if (!HasValue)
        Risks += tr("This field does not have the same value in all files (%1 --> %2). The new value will be written in all %3 files.").arg(Field->First, Field->Last, QString::number(Track->FileCount));
    if (Field->IsStructural)
        Risks += tr("This field describes the structure of the files, a wrong value makes them unreadable.");
    if (!Risks.isEmpty())
    {
        const size_t Generation_Before = Generation;
        QMessageBox Box(QMessageBox::Question, tr("RAWcooked - Metadata"), Risks.join(QLatin1String("\n\n")) + QLatin1Char(' ') + tr("Continue?"), QMessageBox::Yes | QMessageBox::No, this);
        Box.setTextFormat(Qt::PlainText); // Values of the files are not rich text
        Box.setDefaultButton(Field->IsStructural ? QMessageBox::No : QMessageBox::Yes);
        if (Box.exec() != QMessageBox::Yes || Generation != Generation_Before || Main->IsBusy())
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
        Revert = Menu.addAction(tr("Revert"));
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
                const bool IsMatch = Text.isEmpty() || Field->text(Column_Field).contains(Text, Qt::CaseInsensitive) || Field->text(Column_Value).contains(Text, Qt::CaseInsensitive);
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
        Tree->Hint = tr("No metadata. Open a directory or files with DPX, TIFF, EXR, WAV, AIFF or AVI content.");
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

    // Names of the fields and counts of files of the tracks: all of them, also the ones which are not shown for now (closed track or group, filter),
    // and as when they are modified (bold): the columns do not move when a track is opened, when the filter changes or at an edit.
    // The lines of the tracks and of the groups are not cells of the columns
    const int Margin = 8; // Texts do not touch the next column
    const int Indentation = Tree->indentation() * 3; // Of the fields, with the place of the branches of the tracks
    int Field = Tree->header()->sectionSizeHint(Column_Field);
    int Files = Tree->header()->sectionSizeHint(Column_Files);
    QHash<QString, int> Names; // Tracks often have the same fields
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
                auto Width = Names.find(Name);
                if (Width == Names.end())
                    Width = Names.insert(Name, Tree->Text_Width(Name, true));
                Field = qMax(Field, Indentation + Width.value());
            }
        }
    }
    Columns_Wanted_Field = Field + Margin;

    // The longest text of the last column for a field, modified or not
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
    Columns_Wanted_Files_Fields = Files_Fields + Margin;
    Columns_Wanted_Files = qMax(Columns_Wanted_Files_Fields, Files + Margin);

    // Values: the ones which are shown
    Columns_Wanted_Value = Tree->Column_Width(Column_Value) + Margin;
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
    const int Field = Columns_User_Field ? Tree->columnWidth(Column_Field) : qMin(Columns_Wanted_Field, qMax(Available * 45 / 100, 12 * Tree->fontMetrics().height()));
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
