#include "ui/widgets/valueedit.h"
#include <QAbstractItemDelegate>
#include <QClipboard>
#include <QDialog>
#include <QLineEdit>
#include <QTreeView>
#include <QVBoxLayout>

static void colorHexChecks()
{
    NifModel model;
    VertexEditorTest::fixture(model);
    auto light=model.getBlock(5);
    Color3 precise(.12345678f,.51111111f,1.25f);
    model.set<Color3>(light,"Diffuse Color",precise);
    auto before=bytes(model);model.resetUndoHistory();
    QWidget window;auto layout=new QVBoxLayout(&window);auto tree=new QTreeView(&window);layout->addWidget(tree);
    tree->setModel(&model);tree->setItemDelegate(model.createDelegate(&window,nullptr));tree->setRootIndex(light);
    for(int c=3;c<model.columnCount();++c)tree->hideColumn(c);
    tree->setColumnWidth(0,180);tree->setColumnWidth(1,100);tree->setColumnWidth(2,260);
    window.resize(650,580);window.show();QCoreApplication::processEvents();
    auto click=[&](const QString & field) {
        auto index=model.getIndex(model.getBlock(5),field);index=index.sibling(index.row(),NifModel::ValueCol);
        tree->scrollTo(index);QCoreApplication::processEvents();auto rect=tree->visualRect(index);
        QPoint p(rect.left()+50,rect.center().y());
        QMouseEvent press(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(),&press);QApplication::sendEvent(tree->viewport(),&release);
        QCoreApplication::processEvents();
        auto editor=tree->findChild<QLineEdit *>("colorHex");
        check(editor && editor->isVisible(),"clicking a color value opens its hex text editor");
        check(editor->parentWidget()->findChildren<QDoubleSpinBox *>().isEmpty(),"inline color editing has no separate RGB spin boxes");
        return editor;
    };
    auto key=[&](QLineEdit * editor,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress,code,modifiers);QApplication::sendEvent(editor,&event);
        QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    };
    auto editor=click("Diffuse Color");editor->selectAll();key(editor,Qt::Key_C,Qt::ControlModifier);
    check(QApplication::clipboard()->text()==editor->text(),"Ctrl+C copies the hex code");
    key(editor,Qt::Key_Return);
    check(bytes(model)==before && model.undoStack->count()==0,"copying or accepting an unchanged color preserves float and HDR values exactly");
    editor=click("Diffuse Color");editor->selectAll();QApplication::clipboard()->setText("  Ff8200  ");key(editor,Qt::Key_V,Qt::ControlModifier);
    check(editor->hasAcceptableInput(),"paste accepts mixed-case hex without # and surrounding whitespace");
    window.grab().save(QCoreApplication::applicationDirPath()+"/color-hex-preview.png");key(editor,Qt::Key_Return);
    auto color=model.get<Color3>(model.getBlock(5),"Diffuse Color");
    auto changed=bytes(model);
    check(color==Color3(1,130/255.f,0) && model.undoStack->count()==1,"Enter applies the hex color in one undo step");
    editor=click("Ambient Color");editor->selectAll();QApplication::clipboard()->setText("#ff8200");key(editor,Qt::Key_V,Qt::ControlModifier);key(editor,Qt::Key_Return);
    check(model.get<Color3>(model.getBlock(5),"Ambient Color")==color,"hex codes can be pasted into another light color field");
    model.undo();check(bytes(model)==changed,"undo restores only the pasted destination color");
    model.undo();check(bytes(model)==before,"undo restores the exact original color");
    model.redo();check(bytes(model)==changed,"redo restores the hex edit");
    editor=click("Diffuse Color");editor->setText("#112233");key(editor,Qt::Key_Escape);
    check(bytes(model)==changed,"Escape cancels a hex color edit");
    editor=click("Diffuse Color");editor->setText("#12");key(editor,Qt::Key_Tab);
    check(bytes(model)==changed,"incomplete hex input cannot overwrite a stored color");
    auto index=model.getIndex(model.getBlock(5),"Diffuse Color");index=index.sibling(index.row(),NifModel::ValueCol);
    check(!index.data(Qt::UserRole).toString().isEmpty(),"color-wheel spell remains attached to the value cell");
    bool wheelOpened=false;
    QTimer::singleShot(0,[&] {
        if(auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) {wheelOpened=true;dialog->reject();}
    });
    auto rect=tree->visualRect(index);QPoint icon(rect.left()+8,rect.center().y());
    QMouseEvent wheelPress(QEvent::MouseButtonPress,icon,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent wheelRelease(QEvent::MouseButtonRelease,icon,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(),&wheelPress);QApplication::sendEvent(tree->viewport(),&wheelRelease);
    check(wheelOpened && bytes(model)==changed,"color-wheel icon still opens its dialog and Cancel preserves the color");

    ColorEdit rgba;Color4 original(.12345678f,.23456789f,.34567891f,.43219876f);rgba.setColor4(original);
    auto hex=rgba.findChild<QLineEdit *>("colorHex");
    check(rgba.getColor4()==original,"opening an RGBA color preserves its exact channels");
    hex->setText("#112233");auto rgbOnly=rgba.getColor4();
    check(rgbOnly==Color4(17/255.f,34/255.f,51/255.f,original[3]),"six-digit RGB paste preserves the existing alpha");
    hex->setText("#11223380");
    check(rgba.getColor4()==Color4(17/255.f,34/255.f,51/255.f,128/255.f),"eight-digit colors use the existing RRGGBBAA order");
    hex->setText("#zzzzzz");check(rgba.getColor4()==original,"invalid hex input leaves the original color intact");
    ValueEdit byteEditor;NifValue byteColor(NifValue::tByteColor4);byteColor.set<ByteColor4>(ByteColor4());byteEditor.setValue(byteColor);
    byteEditor.findChild<QLineEdit *>("colorHex")->setText("#11223380");
    check(byteEditor.getValue().get<ByteColor4>()==Color4(17/255.f,34/255.f,51/255.f,128/255.f),"byte-color values retain RGB and alpha ordering");
}
