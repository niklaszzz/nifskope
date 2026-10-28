#include "model/vertexedit.h"
#include <QMouseEvent>
#include <QTimer>

class VertexEditorTest {
public:
	static void fixture( NifModel & model )
	{
		model.insertNiBlock("NiNode");
		for(int m=0;m<2;++m) {
			auto mesh=model.insertNiBlock("NiTriShape");
			auto data=model.insertNiBlock("NiTriShapeData");
			model.setLink(mesh,"Data",model.getBlockNumber(data));
			model.set<quint16>(data,"Num Vertices",4);
			model.set<bool>(data,"Has Vertices",true);
			model.updateArray(model.getIndex(data,"Vertices"));
			const QVector<Vector3> points={Vector3(-20,-20,0),Vector3(20,-20,0),Vector3(-20,20,0),Vector3(20,20,0)};
			for(int i=0;i<4;++i) model.set<Vector3>(model.getIndex(data,"Vertices").child(i,0),points[i]);
			model.set<quint16>(data,"Num Triangles",2);
			model.set<uint>(data,"Num Triangle Points",6);
			model.set<bool>(data,"Has Triangles",true);
			model.updateArray(model.getIndex(data,"Triangles"));
			model.set<Triangle>(model.getIndex(data,"Triangles").child(0,0),Triangle(0,1,2));
			model.set<Triangle>(model.getIndex(data,"Triangles").child(1,0),Triangle(1,3,2));
			model.set<Vector3>(mesh,"Translation",Vector3(m*110-40,0,0));
		}
		model.insertNiBlock("NiPointLight");
		model.set<Vector3>(model.getBlock(5),"Translation",Vector3(0,80,0));
		model.set<uint>(model.getBlock(0),"Num Children",3);
		model.updateArray(model.getIndex(model.getBlock(0),"Children"));
		for(int i=0;i<3;++i) model.setLink(model.getIndex(model.getBlock(0),"Children").child(i,0),1+i*2);
		model.resetUndoHistory();
	}
	static void storage()
	{
		NifModel model; fixture(model);
		VertexEditData data;
		check(data.read(&model,model.getBlock(1)) && data.positions.size()==4,"read classic mesh vertices");
		auto original=data.positions, changed=original;
		changed[0]=Vector3(-30,-40,5);
		model.beginUndoTransaction("Vertex test");
		check(data.write(&model,changed,QSet<int>{0}),"write a selected vertex");
		data.updateBounds(&model);
		model.endUndoTransaction();
		check(data.read(&model,model.getBlock(1)) && data.positions==changed,"vertex edit reaches document data");
		check(model.get<float>(model.getBlock(2),"Radius")>0,"vertex edit updates stored bounds");
		model.undo(); data.read(&model,model.getBlock(1));
		check(data.positions==original,"undo restores vertex storage");
		model.redo(); data.read(&model,model.getBlock(1));
		check(data.positions==changed,"redo restores vertex edit");
		QByteArray saved;QBuffer output(&saved);output.open(QIODevice::WriteOnly);
		check(model.save(output),"serialize edited classic mesh");
		NifModel loaded;QBuffer input(&saved);input.open(QIODevice::ReadOnly);
		check(loaded.load(input) && data.read(&loaded,loaded.getBlock(1)) && data.positions==changed,"edited classic vertices survive save and reload");
		check(!data.write(&model,changed,QSet<int>{0}),"reject vertex destinations from another document");
		auto shared=model.insertNiBlock("NiTriShape");model.setLink(shared,"Data",2);model.resetUndoHistory();
		data.read(&model,model.getBlock(1));
		check(data.isShared(&model) && !data.write(&model,original,QSet<int>{0}),"shared geometry cannot accidentally edit another mesh");
	}
	static void microvolts( const QString & path )
	{
		if(!QFile::exists(path)) return;
		NifModel model;check(model.loadFromFile(path),"load MicroVolts vertex-edit fixture");
		VertexEditData data;int block=-1,editable=0,meshes=0;
		for(int b=0;b<model.getBlockCount();++b) if(model.inherits(model.getBlock(b),"NiMesh")) {
			++meshes;VertexEditData candidate;
			if(candidate.read(&model,model.getBlock(b))) {++editable;if(block<0)block=b;}
		}
		std::printf("Editable MicroVolts meshes: %d / %d\n",editable,meshes);
		check(block>=0 && data.read(&model,model.getBlock(block)),"read MicroVolts position stream");
		auto original=data.positions,changed=original;
		QSet<int> selection={0,original.size()-1};
		for(int i:selection) changed[i]+=Vector3(1,2,3);
		QMap<int,QByteArray> originalBuffers;
		QMap<int,QSet<int>> allowed;
		auto refs=model.getIndex(model.getBlock(block),"Datastreams");
		for(int s=0;s<model.rowCount(refs);++s) {
			auto ref=refs.child(s,0);int stream=model.getLink(ref,"Stream");auto source=model.getBlock(stream);
			originalBuffers[stream]=model.get<QByteArray>(model.getIndex(source,"Data").child(0,0));
			auto formats=model.getArray<uint>(source,"Component Formats");auto semantics=model.getIndex(ref,"Component Semantics");
			int stride=0,offset=0,positionSize=0;
			for(int c=0;c<formats.size();++c) {
				int size=((formats[c]>>16)&15)*((formats[c]>>8)&15);
				auto semantic=model.get<QString>(semantics.child(c,0),"Name");
				if(semantic=="POSITION" || semantic=="POSITION_BP"){offset=stride;positionSize=size;}
				stride+=size;
			}
			if(positionSize) for(int i:selection) for(int byte=0;byte<positionSize;++byte) allowed[stream].insert(i*stride+offset+byte);
		}
		model.resetUndoHistory();model.beginUndoTransaction("Edit MicroVolts vertices");
		check(data.write(&model,changed,selection),"write selected MicroVolts vertices");data.updateBounds(&model);model.endUndoTransaction();
		bool untouched=true;
		for(auto it=originalBuffers.cbegin();it!=originalBuffers.cend();++it) {
			auto after=model.get<QByteArray>(model.getIndex(model.getBlock(it.key()),"Data").child(0,0));
			if(after.size()!=it.value().size()) {untouched=false;continue;}
			for(int i=0;i<after.size();++i) if(after[i]!=it.value()[i] && !allowed[it.key()].contains(i)) untouched=false;
		}
		check(untouched,"MicroVolts edit preserves all bytes outside selected positions");
		check(data.read(&model,model.getBlock(block)) && data.positions==changed,"MicroVolts selected positions updated");
		QByteArray saved;QBuffer output(&saved);output.open(QIODevice::WriteOnly);check(model.save(output),"serialize edited MicroVolts mesh");
		NifModel loaded;QBuffer input(&saved);input.open(QIODevice::ReadOnly);VertexEditData reloaded;
		check(loaded.load(input) && reloaded.read(&loaded,loaded.getBlock(block)) && reloaded.positions==changed,"MicroVolts vertex edits survive save and reload");
		model.undo();data.read(&model,model.getBlock(block));
		check(data.positions==original,"undo MicroVolts vertex edit");
		for(auto it=originalBuffers.cbegin();it!=originalBuffers.cend();++it)
			check(model.get<QByteArray>(model.getIndex(model.getBlock(it.key()),"Data").child(0,0))==it.value(),"undo restores original datastream bytes");
	}
	static void mouse( GLView * view, QEvent::Type type, QPoint point, Qt::KeyboardModifiers mods=Qt::NoModifier )
	{
		QMouseEvent event(type,point,Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,mods);
		QApplication::sendEvent(view,&event);
	}
	static QPoint vertex( GLView * view, int index )
	{
		QPointF point;
		auto node=view->scene->getNode(view->model,view->editMesh);
		check(view->projectGizmo(node->worldTrans()*view->editData.positions[index],point),"project editable vertex");
		return point.toPoint();
	}
	static void run()
	{
		NifModel model; fixture(model);
		QGLFormat format;format.setVersion(2,1);
		auto view=new GLView(format,nullptr);
		view->timer->stop(); view->cfg.fov=45; view->cfg.upAxis=GLView::ZAxis;
		view->cfg.background=QColor(30,30,30);
		view->setNif(&model);view->setPosition(0,0,0);view->setRotation(0,0,0);view->setDistance(180);view->setProjection(false);
		view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;
		view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);
		view->setCurrentIndex(model.getBlock(1));view->setEditSelectionMode(Scene::SelVertex);view->updateGL();
		if(!view->vertexProjectionValid) std::fprintf(stderr,"Vertex edit reason: %s; positions=%d; lock=%d; nodes=%d\n",qPrintable(view->vertexEditReason),view->editData.positions.size(),model.getBlockNumber(view->editMesh),view->scene->nodes.list().size());
		check(view->editMesh==model.getBlock(1) && view->vertexProjectionValid,"vertex mode locks the selected mesh");
		QPoint a=vertex(view,0),b=vertex(view,1);
		QPoint start(qMin(a.x(),b.x())-5,qMin(a.y(),b.y())-5),end(qMax(a.x(),b.x())+5,qMax(a.y(),b.y())+5);
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		check(view->selectedVertices==QSet<int>({0,1}),"LMB drag box selects multiple vertices");
		check(model.undoStack->count()==0,"vertex selection does not dirty the document");
		QPoint p=vertex(view,3);
		mouse(view,QEvent::MouseButtonPress,p,Qt::ShiftModifier);mouse(view,QEvent::MouseButtonRelease,p,Qt::ShiftModifier);
		check(view->selectedVertices.contains(3),"Shift adds to vertex selection");
		p=vertex(view,0);mouse(view,QEvent::MouseButtonPress,p,Qt::ControlModifier);mouse(view,QEvent::MouseButtonRelease,p,Qt::ControlModifier);
		check(!view->selectedVertices.contains(0),"Ctrl toggles vertex selection");
		view->setCurrentIndex(model.getBlock(3));
		check(view->scene->currentBlock==model.getBlock(1) && view->editMesh==model.getBlock(1),"vertex mode cannot switch to another mesh");
		view->selectedVertices=QSet<int>({0,1});view->setGizmoMode(GLView::GizmoMove);view->updateGL();
		check(view->gizmoVisible,"selected vertices have a transform gizmo");
		auto original=view->editData.positions;
		auto objectTranslation=model.get<Vector3>(model.getBlock(1),"Translation");
		start=(view->gizmoScreenOrigin*.3+view->gizmoScreenAxes[0]*.7).toPoint();
		mouse(view,QEvent::MouseButtonPress,start);check(view->vertexGizmo,"vertex handle starts an edit drag");
		end=start+(view->gizmoDragDirection*.2).toPoint();
		mouse(view,QEvent::MouseMove,end);view->updateGL();mouse(view,QEvent::MouseButtonRelease,end);
		VertexEditData data;data.read(&model,model.getBlock(1));
		check(data.positions[0][0]>original[0][0] && data.positions[1][0]>original[1][0]
			&& data.positions[2]==original[2] && data.positions[3]==original[3],"Move changes only selected vertices");
		check(model.get<Vector3>(model.getBlock(1),"Translation")==objectTranslation,"vertex editing does not move the object transform");
		check(model.undoStack->count()==1,"vertex drag and bounds update form one undo step");
		model.undo();view->updateGL();data.read(&model,model.getBlock(1));
		check(data.positions==original && view->editMesh==model.getBlock(1) && view->selectedVertices.size()==2,"undo preserves vertex edit mode and selection");

		view->setGizmoMode(GLView::GizmoRotate);view->updateGL();
		int sample=-1;for(int i=4;i<70;++i) if(view->pickGizmo(view->gizmoRings[2][i].toPoint())==2){sample=i;break;}
		check(sample>=0,"pick vertex rotation ring");
		start=view->gizmoRings[2][sample].toPoint();end=view->gizmoRings[2][sample+12].toPoint();
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		data.read(&model,model.getBlock(1));
		check(!(data.positions[0]==original[0]) && std::abs((data.positions[1]-data.positions[0]).length()-40)<.01f
			&& data.positions[2]==original[2],"Rotate pivots the selected vertices together");
		model.undo();view->updateGL();view->setGizmoMode(GLView::GizmoScale);view->updateGL();
		start=view->gizmoScreenOrigin.toPoint();end=start+QPoint(60,-60);
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		data.read(&model,model.getBlock(1));
		check(std::abs((data.positions[1]-data.positions[0]).length()-40*std::exp(1.f))<.01f && data.positions[2]==original[2],"Scale resizes selected vertices around their center");
		model.undo();view->updateGL();
		int count=model.undoStack->count();
		start=view->gizmoScreenOrigin.toPoint();mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,start+QPoint(30,-30));
		QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);
		mouse(view,QEvent::MouseButtonRelease,start+QPoint(30,-30));data.read(&model,model.getBlock(1));
		check(data.positions==original && model.undoStack->count()==count,"Escape cancels a vertex drag without history");

		view->selectedVertices=QSet<int>({0});view->setVertexSnap(true);view->setGizmoMode(GLView::GizmoMove);view->updateGL();
		start=(view->gizmoScreenOrigin*.3+view->gizmoScreenAxes[0]*.7).toPoint();end=vertex(view,1);
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);view->updateGL();
		check(view->snapTarget>=0,"snap detects another vertex near the cursor");
		mouse(view,QEvent::MouseButtonRelease,end);data.read(&model,model.getBlock(1));
		check((data.positions[0]-original[1]).length()<.001f && data.positions[1]==original[1],"Move snaps the anchor vertex exactly onto its target");
		view->updateGL();view->updateGL();view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/vertex-edit-preview.png");
		model.undo();view->updateGL();
		VertexEditData other;other.read(&model,model.getBlock(3));
		auto targetWorld=view->scene->getNode(&model,model.getBlock(3))->worldTrans()*other.positions[0];QPointF targetScreen;
		check(view->projectGizmo(targetWorld,targetScreen),"project snapping vertex on another mesh");
		start=(view->gizmoScreenOrigin*.3+view->gizmoScreenAxes[0]*.7).toPoint();end=targetScreen.toPoint();
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		data.read(&model,model.getBlock(1));
		check((view->scene->getNode(&model,model.getBlock(1))->worldTrans()*data.positions[0]-targetWorld).length()<.001f,"snap to a vertex on a different mesh in world space");
		model.undo();view->setEditSelectionMode(Scene::SelObject);view->setGizmoMode(GLView::GizmoSelect);
		view->scene->options|=Scene::ShowNodes;view->updateGL();
		check(view->lightMarkers.size()==1,"Show Nodes displays a light marker");
		p=view->lightMarkers.first().second.toPoint();
		check(view->indexAt(p)==model.getBlock(5),"light icon is independently pickable");
		mouse(view,QEvent::MouseButtonPress,p);mouse(view,QEvent::MouseButtonRelease,p);
		check(view->scene->currentBlock==model.getBlock(5),"clicking the light icon selects the light");
		view->setGizmoMode(GLView::GizmoMove);view->updateGL();
		auto lightStart=model.get<Vector3>(model.getBlock(5),"Translation");
		start=(view->gizmoScreenOrigin*.3+view->gizmoScreenAxes[0]*.7).toPoint();
		mouse(view,QEvent::MouseButtonPress,start);end=start+(view->gizmoDragDirection*.2).toPoint();
		mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		check(!(model.get<Vector3>(model.getBlock(5),"Translation")==lightStart),"move selected light with gizmo");
		model.undo();check(model.get<Vector3>(model.getBlock(5),"Translation")==lightStart,"undo light movement");
		view->updateGL();view->updateGL();view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/light-marker-preview.png");
		view->scene->options&=~Scene::ShowNodes;view->updateGL();
		check(view->lightMarkers.isEmpty() && !view->lightAt(p).isValid(),"Show Nodes off hides and disables light markers");
		delete view;
		auto window=new NifSkope;
		auto document=window->findChild<NifModel *>();fixture(*document);
		window->select(document->getBlock(1));
		auto viewport=window->findChild<GLView *>("OGL1");
		window->findChild<QAction *>("aSelectVertex")->trigger();
		check(viewport->editMesh==document->getBlock(1),"main window Vertex Select locks the selected mesh");
		window->findChild<QAction *>("aGizmo1")->trigger();
		check(viewport->scene->selMode==Scene::SelVertex,"main window Move stays in vertex mode");
		viewport->selectedVertices=QSet<int>({0,1});
		VertexEditData uiData;uiData.read(document,document->getBlock(1));auto uiPositions=uiData.positions;
		uiPositions[0]+=Vector3(1,0,0);
		document->beginUndoTransaction("Vertex UI test");uiData.write(document,uiPositions,QSet<int>{0});document->endUndoTransaction();
		window->findChild<QAction *>("aUndo")->trigger();
		check(viewport->editMesh==document->getBlock(1) && viewport->selectedVertices.size()==2
			&& viewport->scene->selMode==Scene::SelVertex,"main window Undo retains locked mesh and vertex selection");
		delete window;
	}
};
