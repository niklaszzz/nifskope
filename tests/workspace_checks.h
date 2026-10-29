#include "ui_nifskope.h"
#include <QTabBar>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QAbstractButton>
#include <QOpenGLFramebufferObject>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

class WorkspaceTest {
public:
	static void drops() {
		QTemporaryDir directory;QString a=directory.filePath("drop one.nif"),b=directory.filePath("drop two.NIF");
		NifModel source;VertexEditorTest::fixture(source);check(source.saveToFile(a) && source.saveToFile(b),"prepare drag-and-drop NIF files");
		auto window=new NifSkope;window->show();QCoreApplication::processEvents();window->ogl->timer->stop();
		auto viewport=window->graphicsView->viewport();
		check(window->graphicsView->parentWidget()!=window,"drop regression exercises the nested workspace layout");
		auto drop=[&](const QString & path) {
			QMimeData mime;mime.setUrls({QUrl::fromLocalFile(path)});
			QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&enter);
			check(enter.isAccepted(),"viewport accepts a local NIF drag");
			QDragMoveEvent move(QPoint(25,25),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);check(move.isAccepted(),"viewport accepts moving a NIF drag");
			QDropEvent event(QPointF(25,25),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&event);
			check(event.isAccepted(),"viewport accepts the NIF drop");QCoreApplication::processEvents();
			check(QFileInfo(window->getCurrentFile()).canonicalFilePath()==QFileInfo(path).canonicalFilePath(),"dropping a NIF loads it through the owning main window");
		};
		drop(a);check(window->nif->getBlockCount()==source.getBlockCount(),"dropping into an empty window actually loads the NIF blocks");
		QString error;check(window->addWorkspaceFile(b,error),"prepare a multi-NIF workspace for dropping files");
		window->activateDocument(1);auto second=window->nif;second->set<Vector3>(second->getBlock(1),"Translation",Vector3(9,8,7));second->flushUndoHistory();window->activateDocument(0);
		drop(b);check(window->nif==second && second->get<Vector3>(second->getBlock(1),"Translation")==Vector3(9,8,7) && window->documents.size()==2,"dropping an open workspace NIF switches tabs and preserves unsaved edits");
		second->resetUndoHistory();
		QMimeData mime;mime.setUrls({QUrl::fromLocalFile(a)});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&enter);
		QDragLeaveEvent leave;QApplication::sendEvent(viewport,&leave);
		QString folder=directory.filePath("folder.nif");QDir().mkpath(folder);QMimeData invalid;invalid.setUrls({QUrl::fromLocalFile(folder)});
		QDragEnterEvent invalidEnter(QPoint(20,20),Qt::CopyAction,&invalid,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&invalidEnter);
		check(!invalidEnter.isAccepted(),"directories named .nif are rejected by both file and texture drop handlers");
		check(window->nif==second,"cancelled and invalid drags do not load a stale NIF");
		QImage texture(2,2,QImage::Format_RGB32);texture.fill(Qt::white);QString texturePath=directory.filePath("texture.bmp");check(texture.save(texturePath),"prepare a texture drag");
		QMimeData textureMime;textureMime.setUrls({QUrl::fromLocalFile(texturePath)});QDragEnterEvent textureEnter(QPoint(20,20),Qt::CopyAction,&textureMime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&textureEnter);
		check(textureEnter.isAccepted(),"NIF drop handling still forwards texture drags to the viewport");QDragLeaveEvent textureLeave;QApplication::sendEvent(viewport,&textureLeave);
		delete window;
	}
	static QByteArray disk(const QString & path){QFile f(path);check(f.open(QIODevice::ReadOnly),"read saved workspace file");return f.readAll();}
	static void run() {
		QTemporaryDir directory;QString a=directory.filePath("world.nif"),b=directory.filePath("world_colp.nif");
		NifModel sourceA,sourceB;VertexEditorTest::fixture(sourceA);VertexEditorTest::fixture(sourceB);
		for(auto model:{&sourceA,&sourceB})for(int id:{0,1,3})model->set<uint>(model->getBlock(id),"Flags",0);
		sourceB.set<Vector3>(sourceB.getBlock(1),"Translation",Vector3(0,70,0));
		check(sourceA.saveToFile(a) && sourceB.saveToFile(b),"prepare two separate workspace NIFs");
		auto window=new NifSkope;window->show();window->loadFile(a);QCoreApplication::processEvents();
		auto first=window->nif;auto view=window->ogl;view->timer->stop();
		window->aSanitize->setChecked(false);
		if(!window->ui->aShowHidden->isChecked())window->ui->aShowHidden->trigger();
		window->resize(1200,800);QCoreApplication::processEvents();view->cfg.upAxis=GLView::ZAxis;view->cfg.fov=45;view->setZoom(1);view->doCenter=false;
		view->show();view->resizeGL(view->width(),view->height());
		view->scene->animate=false;view->animState=GLView::AnimDisabled;view->scene->options&=~(Scene::ShowNodes|Scene::ShowAxes|Scene::DoTexturing);
		view->setProjection(false);view->setRotation(0,0,0);view->setPosition(0,0,0);view->setDistance(180);view->view=GLView::ViewUser;
		window->select(first->getBlock(1));view->setGizmoMode(GLView::GizmoMove);view->updateGL();
		check(window->documents.size()==1 && !window->documentTabs->isVisible(),"single-file window keeps the normal editing layout");
		QString error;check(window->addWorkspaceFile(b,error),"add second NIF to the same workspace");
		check(window->documents.size()==2 && window->nif==first && window->documentTabs->isVisible(),"adding a reference retains the active document and displays file tabs");
		auto second=window->documents[1].model;auto referenceBytes=bytes(*second);
		view->setReferencesVisible(false);view->updateGL();auto plain=view->grabFrameBuffer();
		QPointF point;check(view->projectGizmo(Vector3(4,68,0),point),"project a point visible only in the reference document");
		view->setReferencesVisible(true);view->setReferenceOpacity(50);view->updateGL();auto onion=view->grabFrameBuffer();
		check(plain.pixel(point.toPoint())!=onion.pixel(point.toPoint()),"inactive document renders as a visible onion-skin layer");
		view->setReferenceOpacity(100);view->updateGL();auto opaque=view->grabFrameBuffer();
		check(opaque.pixel(point.toPoint())!=onion.pixel(point.toPoint()),"reference opacity changes the composited image");
		view->setReferenceOpacity(30);view->updateGL();view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/workspace-preview.png");
		check(!view->indexAt(point.toPoint()).isValid(),"reference geometry is click-through and cannot be selected in the active file");
		check(bytes(*second)==referenceBytes,"rendering references does not edit their document data");
		first->set<Vector3>(first->getBlock(1),"Translation",Vector3(-35,2,3));first->flushUndoHistory();
		view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0,1};
		auto camera=view->Pos;auto rotation=view->Rot;float distance=view->Dist;
		window->activateDocument(1);view->updateGL();
		check(window->nif==second && window->tree->model()==second && window->currentFile==b,"switching tabs routes the block editor and filename to the selected document");
		check(window->menuBar()->actions().contains(window->book->menuAction()),"Spells menu remains connected after switching documents");
		check(!window->undoAction->isEnabled() && view->scene->selMode==Scene::SelObject,"new active document has its own undo and object selection");
		check(view->Pos==camera && view->Rot==rotation && view->Dist==distance,"switching documents preserves the shared camera");
		second->set<Vector3>(second->getBlock(1),"Translation",Vector3(2,75,4));second->flushUndoHistory();
		window->undoAction->trigger();
		check(second->get<Vector3>(second->getBlock(1),"Translation")==Vector3(0,70,0)
			&& first->get<Vector3>(first->getBlock(1),"Translation")==Vector3(-35,2,3),"Undo affects only the active NIF");
		check(window->isWindowModified(),"inactive unsaved changes keep the workspace marked as modified");
		window->redoAction->trigger();window->activateDocument(0);view->updateGL();
		check(view->scene->selMode==Scene::SelVertex && view->editMesh==first->getBlock(1) && view->selectedVertices==QSet<int>({0,1}),"switching back restores the file's locked mesh and vertex selection");
		window->undoAction->trigger();check(first->get<Vector3>(first->getBlock(1),"Translation")==Vector3(-40,0,0)
			&& second->get<Vector3>(second->getBlock(1),"Translation")==Vector3(2,75,4),"each document retains an independent undo history across tab switches");
		window->redoAction->trigger();check(window->saveWorkspace(error),"Save All writes both documents separately");
		NifModel savedA,savedB;check(savedA.loadFromFile(a) && savedB.loadFromFile(b),"reload both saved NIFs independently");
		check(savedA.get<Vector3>(savedA.getBlock(1),"Translation")==Vector3(-35,2,3)
			&& savedB.get<Vector3>(savedB.getBlock(1),"Translation")==Vector3(2,75,4),"saved files contain their respective edits without mixing documents");
		check(first->undoStack->isClean() && second->undoStack->isClean() && !window->isWindowModified(),"successful Save All marks both files clean");
		auto savedBytes=disk(a);first->set<float>(first->getBlock(1),"Scale",2);second->set<float>(second->getBlock(1),"Scale",3);
		window->documents[1].path=directory.filePath("missing/fail.nif");
		check(!window->saveWorkspace(error) && disk(a)==savedBytes,"a staging failure leaves all existing workspace files untouched");
		check(!first->undoStack->isClean() && !second->undoStack->isClean(),"failed Save All retains both unsaved edit histories");
		window->documents[1].path=b;
		window->documents[1].path=a;
		check(!window->saveWorkspace(error) && disk(a)==savedBytes,"Save All refuses duplicate file destinations");
		window->documents[1].path=b;
		QTimer::singleShot(0,[](){auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Cancel)->click();});
		check(!window->confirmWorkspace(),"Cancel in workspace save confirmation protects unsaved files");
		check(window->saveWorkspace(error),"Save All succeeds after correcting the destination");
		check(window->addWorkspaceFile(b,error) && window->documents.size()==2 && window->activeDocument==1,"adding an already-open NIF switches to its tab instead of duplicating it");
		window->loadFile(a);check(window->nif==first && window->documents.size()==2,"normal Open switches to an existing workspace file without reloading it");
		window->activateDocument(1);
		check(!window->addWorkspaceFile(directory.filePath("absent.nif"),error) && window->documents.size()==2,"failed reference loading preserves the existing workspace");
		window->removeWorkspaceDocument(0);check(window->documents.size()==1 && window->nif==second && !window->documentTabs->isVisible(),"removing a reference returns to single-file editing");
		second->set<float>(second->getBlock(1),"Scale",4);second->flushUndoHistory();window->aSanitize->setChecked(false);window->save();
		check(second->undoStack->isClean() && savedB.loadFromFile(b) && savedB.get<float>(savedB.getBlock(1),"Scale")==4,"normal single-file Save continues to work after closing a workspace tab");
		delete window;
		referenceRefresh();
		microvolts();
	}
	static void referenceRefresh() {
		QTemporaryDir directory;QString worldPath=directory.filePath("world.nif"),collisionPath=directory.filePath("colp.nif");
		NifModel a,b;VertexEditorTest::fixture(a);VertexEditorTest::fixture(b);b.set<Vector3>(b.getBlock(1),"Translation",Vector3(0,70,0));
		check(a.saveToFile(worldPath) && b.saveToFile(collisionPath),"prepare reference-refresh fixtures");
		auto window=new NifSkope;window->resize(1200,800);window->show();window->loadFile(worldPath);QCoreApplication::processEvents();window->aSanitize->setChecked(false);
		auto world=window->nif;auto view=window->ogl;view->timer->stop();view->cfg.upAxis=GLView::ZAxis;view->setZoom(1);view->setProjection(false);view->setRotation(0,0,0);view->setPosition(0,0,0);view->setDistance(180);view->view=GLView::ViewUser;view->doCenter=false;view->animState=GLView::AnimDisabled;
		if(!window->ui->aShowHidden->isChecked())window->ui->aShowHidden->trigger();
		view->scene->options&=~(Scene::ShowGrid|Scene::ShowAxes|Scene::ShowNodes);view->setReferenceOpacity(100);
		QString error;check(window->addWorkspaceFile(collisionPath,error),"add reference-refresh document");
		window->activateDocument(1);view->updateGL(); // Warm the world reference cache.
		auto sample=[view](const QImage & image,const Vector3 & position){QPointF point;check(view->projectGizmo(position+Vector3(6,2,0),point),"project reference refresh sample");return image.pixel(point.toPoint());};
		Vector3 previous(-40,0,0);
		for(int cycle=0;cycle<3;++cycle) {
			window->activateDocument(0);Vector3 next(-20-cycle*30,-70,0);
			world->set<Vector3>(world->getBlock(1),"Translation",next);world->flushUndoHistory();
			if(cycle!=1)window->ui->aSave->trigger();
			check(world->get<Vector3>(world->getBlock(1),"Translation")==next,"world object keeps its edit across Save");
			window->activateDocument(1);view->updateGL();
			view->setReferencesVisible(false);view->updateGL();auto background=view->grabFrameBuffer();
			view->setReferencesVisible(true);view->updateGL();auto updated=view->grabFrameBuffer();
			check(sample(updated,next)!=sample(background,next) && sample(updated,previous)==sample(background,previous),"revisiting a cached world reference shows the latest object position with or without Save");
			if(cycle!=1){NifModel saved;check(saved.loadFromFile(worldPath) && saved.get<Vector3>(saved.getBlock(1),"Translation")==next,"ordinary Save writes the latest world position to disk");}
			world->undo();view->updateGL();auto undone=view->grabFrameBuffer();
			check(sample(undone,previous)!=sample(background,previous),"undo in an inactive document refreshes its reference preview");
			world->redo();view->updateGL();previous=next;
		}
		delete window;
	}
	static void microvolts() {
		QString visuals="build/qt-check/fixtures/Working.nif",collision="build/qt-check/fixtures/World_colp.nif";
		if(!QFile::exists(visuals) || !QFile::exists(collision))return;
		QTemporaryDir directory;QString worldPath=directory.filePath("world.nif"),collisionPath=directory.filePath("World_colp.nif");
		check(QFile::copy(visuals,worldPath) && QFile::copy(collision,collisionPath),"copy real MicroVolts files into a temporary workspace");
		auto window=new NifSkope;window->resize(1200,800);window->show();
		window->loadFile(worldPath);QCoreApplication::processEvents();window->aSanitize->setChecked(false);
		auto world=window->nif;auto view=window->ogl;view->timer->stop();view->show();view->resizeGL(view->width(),view->height());
		view->scene->options&=~Scene::DoTexturing;view->scene->animate=false;view->animState=GLView::AnimDisabled;
		view->setProjection(true);view->setOrientation(GLView::ViewFront);view->center();view->updateGL();
		QString error;check(window->addWorkspaceFile(collisionPath,error),"load actual player collision alongside world visuals");
		auto collider=window->documents[1].model;
		view->setReferencesVisible(false);view->updateGL();auto base=view->grabFrameBuffer();
		view->setReferencesVisible(true);view->setReferenceOpacity(50);view->updateGL();auto combined=view->grabFrameBuffer();
		int differences=0;for(int y=0;y<base.height();y+=3)for(int x=0;x<base.width();x+=3)if(base.pixel(x,y)!=combined.pixel(x,y))++differences;
		check(differences>100,"actual player collision appears over the MicroVolts visual scene");
		combined.save(QCoreApplication::applicationDirPath()+"/workspace-microvolts-preview.png");
		window->activateDocument(1);view->updateGL();
		check(window->findChild<QAction *>("aRebuildCollision")->isEnabled() && window->ui->aShowHidden->isChecked(),"active collision tab exposes collision rebuilding and its authoring meshes");
		QVector<PhysXCollision::Mesh> colliders;check(PhysXCollision::collect(collider,colliders,error),"find independent collision meshes inside the active file");
		QMap<int,QByteArray> cooked;for(auto mesh:colliders)cooked[collider->getBlockNumber(mesh.data)]=collider->get<QByteArray>(mesh.data);
		int proxy=collider->getBlockNumber(colliders.first().source);window->select(collider->getBlock(proxy));window->ui->aSelectVertex->trigger();view->updateGL();
		VertexEditData vertices;check(vertices.read(collider,collider->getBlock(proxy)) && view->vertexEditReason.isEmpty(),"MicroVolts collision authoring mesh remains vertex-editable in the workspace");
		auto edited=vertices.positions;edited[0][0]+=5;
		collider->beginUndoTransaction("Workspace collision vertex");check(vertices.write(collider,edited,{0}),"edit a collision source vertex in its own NIF");vertices.updateBounds(collider);collider->endUndoTransaction();
		window->activateDocument(0);view->updateGL();
		check(!window->findChild<QAction *>("aRebuildCollision")->isEnabled(),"switching to visuals disables collision rebuilding for that file");
		int mesh=-1;for(int b=0;b<world->getBlockCount();++b)if(world->inherits(world->getBlock(b),"NiMesh")){mesh=b;break;}
		auto moved=world->get<Vector3>(world->getBlock(mesh),"Translation")+Vector3(1,2,3);world->set<Vector3>(world->getBlock(mesh),"Translation",moved);world->flushUndoHistory();
		window->save();
		check(world->undoStack->isClean() && collider->undoStack->isClean(),"normal Save saves both real workspace files");
		NifModel savedWorld,savedCollision;check(savedWorld.loadFromFile(worldPath) && savedCollision.loadFromFile(collisionPath),"reload both MicroVolts workspace outputs");
		check(savedWorld.get<Vector3>(savedWorld.getBlock(mesh),"Translation")==moved && vertices.read(&savedCollision,savedCollision.getBlock(proxy)) && vertices.positions==edited,"visual-object and collision-vertex edits persist in their separate NIFs");
		bool untouched=true;for(auto i=cooked.cbegin();i!=cooked.cend();++i)untouched=untouched && savedCollision.get<QByteArray>(savedCollision.getBlock(i.key()),"Mesh Data")==i.value();
		check(untouched,"Save All never rebuilds or alters cooked PhysX collision implicitly");delete window;
	}
};
