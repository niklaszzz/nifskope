#include "model/uveditdata.h"
#include "ui/widgets/uvedit.h"
#include <QPointer>
#include <QImage>
#include <QInputDialog>
#include <QDoubleSpinBox>
#include <QTemporaryDir>

class UVEditorTest {
public:
	static void storage() {
		for(QString file:{QString("Working.nif"),QString("NotWorking.nif")}) {
			QString path="build/qt-check/fixtures/"+file;if(!QFile::exists(path))continue;
			NifModel model;check(model.loadFromFile(path),"load NiMesh UV fixture");
			int total=0,editable=0,first=-1;QString error;
			for(int b=0;b<model.getBlockCount();++b) {
				auto block=model.getBlock(b);auto sets=UVEditData::coordinateSets(&model,block);if(sets.isEmpty())continue;++total;
				UVEditData data;if(data.read(&model,block,sets.first(),&error)){++editable;if(first<0)first=b;}else qWarning("UV %d: %s",b,qPrintable(error));
			}
			std::printf("Editable UV meshes in %s: %d/%d\n",qPrintable(file),editable,total);
			check(total>0 && total==editable,"all sample meshes with UVs have editable layouts");
			UVEditData data;check(data.read(&model,model.getBlock(first),0),"read NiMesh UV coordinates and triangles");
			auto before=bytes(model);model.resetUndoHistory();auto original=model.get<QByteArray>(data.storage);auto changed=data.coordinates;changed[0]+=Vector2(.125f,.25f);
			int owner=model.getBlockNumber(data.storage),stride=0,offset=-1,width=0;
			auto refs=model.getIndex(data.mesh,"Datastreams");
			for(int s=0;s<model.rowCount(refs);++s) {
				auto ref=refs.child(s,0);if(model.getLink(ref,"Stream")!=owner)continue;
				auto formats=model.getArray<uint>(model.getBlock(owner),"Component Formats");auto sem=model.getIndex(ref,"Component Semantics");
				for(int c=0;c<formats.size();++c){int size=((formats[c]>>16)&15)*((formats[c]>>8)&15);if(model.get<QString>(sem.child(c,0),"Name")=="TEXCOORD" && model.get<uint>(sem.child(c,0),"Index")==0){offset=stride;width=size;}stride+=size;}
			}
			check(offset>=0 && width>0,"locate the UV component in its raw stream");
			model.beginUndoTransaction("UV test");check(data.write(&model,changed),"write one NiMesh UV");model.endUndoTransaction();
			auto after=bytes(model),buffer=model.get<QByteArray>(data.storage);bool preserved=buffer.size()==original.size();
			for(int p=0;p<original.size();++p)if(p<offset || p>=offset+width)preserved=preserved && buffer[p]==original[p];
			check(preserved,"UV edit preserves positions, normals, colors, padding and every unselected UV");
			check(data.read(&model,model.getBlock(first),0) && std::abs(data.coordinates[0][0]-changed[0][0])<.001f && std::abs(data.coordinates[0][1]-changed[0][1])<.001f,"read back the edited UV");
			NifModel loaded;QBuffer input(&after);input.open(QIODevice::ReadOnly);check(loaded.load(input),"reload UV-edited NIF");
			UVEditData loadedData;check(loadedData.read(&loaded,loaded.getBlock(first),0) && loadedData.coordinates==data.coordinates,"UV edits survive save and reload");
			check(!data.write(&loaded,changed),"reject UV writes to another document");
			model.undo();check(bytes(model)==before,"global undo restores original UV document bytes");model.redo();check(bytes(model)==after,"global redo restores UV edits exactly");
		}
	}
	static void run() {
		{
			NifModel classic;VertexEditorTest::fixture(classic);auto data=classic.getBlock(2);
			classic.set<quint16>(data,"Vector Flags",1);classic.set<quint16>(data,"BS Vector Flags",1);
			auto sets=classic.getIndex(data,"UV Sets");classic.updateArray(sets);classic.updateArray(sets.child(0,0));
			QVector<Vector2> uvs={Vector2(0,0),Vector2(1,0),Vector2(0,1),Vector2(1,1)};classic.setArray<Vector2>(sets.child(0,0),uvs);
			classic.resetUndoHistory();auto legacy=new UVWidget;
			check(legacy->setNifData(&classic,classic.getBlock(1)),"classic UV editor remains supported without a texture");
			legacy->select(0);legacy->moveSelection(.125,.25);classic.undo();
			check(legacy->texcoords==uvs,"global UV undo also restores classic UV arrays");delete legacy;
		}
		QString path="build/qt-check/fixtures/Working.nif";if(!QFile::exists(path))return;
		NifModel model;check(model.loadFromFile(path),"load UV interaction fixture");model.resetUndoHistory();
		int block=-1;for(int b=0;b<model.getBlockCount();++b)if(!UVEditData::coordinateSets(&model,model.getBlock(b)).isEmpty()){block=b;break;}
		check(block>=0,"find a mesh for UV interaction tests");
		QPointer<UVWidget> editor=new UVWidget;
		check(editor->setNifData(&model,model.getBlock(block)),"open existing UV editor on NiMesh");
		editor->show();QCoreApplication::processEvents();editor->frameUVs();editor->updateGL();
		check(!editor->texcoords.isEmpty() && !editor->faces.isEmpty(),"render NiMesh UV wireframe");
		int faces=editor->faces.size();editor->setTexCoords();check(editor->faces.size()==faces,"refreshing UV coordinates does not duplicate faces");
		editor->selectAll();auto original=editor->texcoords;check(model.undoStack->count()==0,"UV selection creates no document edits");
		QPoint start=editor->mapFromContents(original.first()),end=start+QPoint(20,10);
		QMouseEvent press(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(editor,&press);
		for(int step=1;step<=4;++step){QPoint p=start+(end-start)*step/4;QMouseEvent move(QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(editor,&move);QCoreApplication::processEvents();}
		QMouseEvent release(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(editor,&release);
		check(editor->texcoords!=original && model.undoStack->count()==1,"UV mouse drag changes coordinates in one global undo step");
		auto moved=editor->texcoords;editor->documentUndo->trigger();QCoreApplication::processEvents();
		check(editor && editor->isVisible() && editor->texcoords==original,"UV undo restores mapping while keeping the editor open");
		editor->documentRedo->trigger();check(editor && editor->texcoords==moved,"UV redo restores the mapping");
		QTimer::singleShot(0,[](){auto dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());if(dialog){dialog->setDoubleValue(30);dialog->accept();}});
		editor->rotateSelection();check(editor->texcoords!=moved && model.undoStack->count()==2,"UV rotation dialog edits the selected UVs in one undo step");
		model.undo();check(editor->texcoords==moved,"undo UV rotation");
		QTimer::singleShot(0,[](){auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(dialog){auto fields=dialog->findChildren<QDoubleSpinBox *>();if(!fields.isEmpty())fields.first()->setValue(2);dialog->accept();}});
		editor->scaleSelection();check(editor->texcoords!=moved,"UV scale dialog edits the selected UVs");model.undo();check(editor->texcoords==moved,"undo UV scaling");
		editor->getCoordSets();check(!editor->coordSetGroup->actions().isEmpty(),"coordinate-set menu lists actual NiMesh UV channels");
		QTemporaryDir directory;QImage checker(64,64,QImage::Format_RGB32);
		for(int y=0;y<64;++y)for(int x=0;x<64;++x)checker.setPixelColor(x,y,((x/8+y/8)&1)?QColor(90,100,110):QColor(180,190,200));
		editor->texfile=directory.filePath("checker.bmp");checker.save(editor->texfile);editor->texsource=QModelIndex();editor->makeCurrent();
		check(editor->bindTexture(editor->texfile),"UV editor can display a chosen background texture");editor->updateGL();editor->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/uv-editor-preview.png");
		delete editor;
		auto window=new NifSkope;check(window->findChild<QAction *>("aUVEditor")!=nullptr,"main window exposes a UV Editor button");delete window;
	}
};
