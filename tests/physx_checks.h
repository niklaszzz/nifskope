#include "physx/physxcollision.h"
#include "model/vertexedit.h"
#include <QTemporaryDir>
#include <cmath>

class PhysXCollisionTest {
public:
	static void run() {
		QString path="build/qt-check/fixtures/World_colp.nif";
		if(!QFileInfo::exists(path))return;
		auto window=new NifSkope;
		auto model=window->findChild<NifModel *>();
		auto action=window->findChild<QAction *>("aRebuildCollision");
		check(action && !action->isEnabled(),"Rebuild Collision disabled in a document without PhysX");
		check(model->loadFromFile(path),"load collision fixture in main window");model->resetUndoHistory();
		check(action->isEnabled(),"Rebuild Collision enabled for PhysX document");
		QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();
		view->cfg.fov=45;view->cfg.upAxis=GLView::ZAxis;view->cfg.background=QColor(30,30,30);
		view->setNif(model);view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);
		view->setProjection(true);view->view=GLView::ViewUser;view->setRotation(35,0,-20);
		view->scene->options|=Scene::ShowCollision;view->updateGL();
		check(view->physxMeshes.size()==9,"Show Collision decodes stored PhysX meshes for rendering");
		const auto mesh=view->physxMeshes[3];QVector3D center;
		for(auto p:mesh.vertices)center+=mesh.world.map(QVector3D(p[0],p[1],p[2]));center/=mesh.vertices.size();
		view->setPosition(-center.x(),-center.y(),-center.z());view->setDistance(500);view->setCurrentIndex(mesh.source);view->updateGL();view->updateGL();
		bool hit=false;
		for(int i=0;i<mesh.indices.size() && !hit;i+=3) {
			auto a=mesh.vertices[mesh.indices[i]],b=mesh.vertices[mesh.indices[i+1]];auto p=mesh.world.map(QVector3D((a[0]+b[0])*.5f,(a[1]+b[1])*.5f,(a[2]+b[2])*.5f));QPointF screen;
			if(view->projectGizmo(Vector3(p.x(),p.y(),p.z()),screen) && view->rect().contains(screen.toPoint()))hit=view->collisionAt(screen.toPoint()).isValid();
		}
		check(hit,"stored collision wireframe is clickable and selects its editable source");
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/physx-collision-preview.png");
		view->scene->options&=~Scene::ShowCollision;view->updateGL();check(!view->collisionAt(QPoint(400,300)).isValid(),"Show Collision off disables collision picking");
		view->scene->options|=Scene::ShowHidden;
		view->setEditSelectionMode(Scene::SelVertex);view->updateGL();check(view->editMesh==mesh.source && !view->editData.positions.isEmpty() && view->vertexEditReason.isEmpty(),"collision source can enter the existing vertex editor");
		delete view;
		check(model->loadFromFile("build/qt-check/fixtures/Working.nif"),"switch main window to ordinary visual file");
		check(!action->isEnabled(),"switching away from collision disables Rebuild Collision");
		delete window;
	}
};

static void checkPhysx(const QString & path,const QString & dll)
{
	NifModel model;check(model.loadFromFile(path),"load original PhysX collision sample");
	model.resetUndoHistory();
	QVector<PhysXCollision::Mesh> meshes;QString error;
	bool collected=PhysXCollision::collect(&model,meshes,error);
	if(!collected)qWarning("%s",qPrintable(error));check(collected,"match all source meshes to cooked collision");
	check(meshes.size()==9,"sample contains nine static collision meshes");
	QVector<QByteArray> originals;
	for(const auto & mesh:meshes) {
		originals<<model.get<QByteArray>(mesh.data);
		std::printf("Collision %s: %d vertices, %d triangles\n",qPrintable(mesh.name),mesh.vertices.size(),mesh.indices.size()/3);
		VertexEditData source;check(source.read(&model,mesh.source),"read matched collision proxy");
		Transform sourceWorld(&model,mesh.source);int parent=model.getParent(mesh.source);QSet<int> parents;
		while(parent>=0 && !parents.contains(parent)){parents.insert(parent);auto block=model.getBlock(parent);if(model.inherits(block,"NiAVObject"))sourceWorld=Transform(&model,block)*sourceWorld;parent=model.getParent(block);}
		float maximum=0;
		for(auto p:source.positions) {
			auto world=sourceWorld*p;auto local=mesh.world.inverted().map(QVector3D(world[0],world[1],world[2]));
			float nearest=1e30f;for(auto cooked:mesh.vertices)nearest=qMin(nearest,(QVector3D(cooked[0],cooked[1],cooked[2])-local).lengthSquared());
			maximum=qMax(maximum,std::sqrt(nearest));
		}
		std::printf("Original proxy/collision maximum distance: %g\n",double(maximum));
		check(maximum<.1f,"original collider and proxy agree in world space");
	}
	QByteArray damaged=originals.first().left(60);QVector<Vector3> v;QVector<quint32> t;
	check(!PhysXCollision::decode(damaged,v,t),"reject truncated cooked geometry");
	VertexEditData edit;check(edit.read(&model,meshes.first().source),"read editable collision source");
	auto changed=edit.positions;changed[0][0]+=12.5f;
	model.beginUndoTransaction("Edit collision source");check(edit.write(&model,changed,{0}),"edit one collision source vertex");edit.updateBounds(&model);model.endUndoTransaction();
	check(model.get<QByteArray>(meshes.first().data)==originals.first(),"source edits leave cooked collision untouched until explicit rebuild");
	QByteArray before=bytes(model);int history=model.undoStack->count();
	QString helper=QCoreApplication::applicationDirPath()+"/PhysxCooker.exe";
	check(!PhysXCollision::rebuild(&model,dll,helper+".missing",error),"missing helper aborts safely");
	check(bytes(model)==before && model.undoStack->count()==history,"failed rebuild changes neither document nor undo history");
	int count=0;bool rebuilt=PhysXCollision::rebuild(&model,dll,helper,error,&count);
	if(!rebuilt)qWarning("%s",qPrintable(error));check(rebuilt && count==9,"cook and runtime-validate all nine collision meshes using game SDK");
	check(model.undoStack->count()==history+1,"rebuild creates exactly one undo step");
	check(model.get<QByteArray>(meshes.first().data)!=originals.first(),"rebuild updates the actual PhysX mesh bytes");
	QByteArray after=bytes(model);
	NifModel expected;QBuffer beforeBuffer(&before);beforeBuffer.open(QIODevice::ReadOnly);check(expected.load(beforeBuffer),"load pre-rebuild comparison document");
	for(int i=0;i<meshes.size();++i) {
		int block=model.getBlockNumber(meshes[i].data);
		expected.set<QByteArray>(expected.getBlock(block),"Mesh Data",model.get<QByteArray>(meshes[i].data));
	}
	check(bytes(expected)==after,"rebuild preserves every field outside cooked mesh data");
	QVector<PhysXCollision::Mesh> updated;check(PhysXCollision::collect(&model,updated,error),"decode rebuilt geometry");
	for(int n=1;n<meshes.size();++n) {
		const auto & old=meshes[n];const auto & current=updated[n];QVector<int> mapping;bool positionsMatch=true;
		for(auto p:current.vertices) {
			float nearest=1e30f;int match=-1;
			for(int i=0;i<old.vertices.size();++i){float d=(p-old.vertices[i]).length();if(d<nearest){nearest=d;match=i;}}
			positionsMatch=positionsMatch && nearest<.02f;mapping<<match;
		}
		check(positionsMatch,"unchanged collision vertices retain their physical positions");
		auto triangleKey=[](int a,int b,int c){if(b<a && b<c){int tmp=a;a=b;b=c;c=tmp;}else if(c<a && c<b){int tmp=a;a=c;c=b;b=tmp;}return QString("%1,%2,%3").arg(a).arg(b).arg(c);};
		QMap<QString,int> originalFaces,currentFaces;
		for(int i=0;i<old.indices.size();i+=3)++originalFaces[triangleKey(old.indices[i],old.indices[i+1],old.indices[i+2])];
		for(int i=0;i<current.indices.size();i+=3)++currentFaces[triangleKey(mapping[current.indices[i]],mapping[current.indices[i+1]],mapping[current.indices[i+2]])];
		check(originalFaces==currentFaces,"unchanged collision triangles retain topology and winding");
	}
	// The edited point must exist in the newly cooked collider; cooking may weld/reorder vertices.
	auto worldPoint=Transform(&model,updated.first().source)*changed[0];
	QVector3D wanted=updated.first().world.inverted().map(QVector3D(worldPoint[0],worldPoint[1],worldPoint[2]));
	float nearest=1e30f;for(auto p:updated.first().vertices)nearest=qMin(nearest,(QVector3D(p[0],p[1],p[2])-wanted).length());
	std::printf("Edited vertex collision distance: %g\n",double(nearest));
	check(nearest<.05f,"edited vertex reaches physical collision in the correct coordinate space");
	QBuffer saved(&after);saved.open(QIODevice::ReadOnly);NifModel loaded;check(loaded.load(saved),"reload rebuilt collision NIF");
	check(PhysXCollision::collect(&loaded,updated,error) && updated.size()==9,"rebuilt collision survives save and reload");
	model.undo();check(bytes(model)==before,"undo restores all original collision bytes");
	model.redo();check(bytes(model)==after,"redo restores rebuilt collision exactly");
	NifModel plain;check(plain.loadFromFile("build/qt-check/fixtures/Working.nif"),"load ordinary visual NIF");
	auto plainBytes=bytes(plain);check(!PhysXCollision::hasCollision(&plain),"ordinary visual NIF has no collision action");
	check(!PhysXCollision::rebuild(&plain,dll,helper,error),"rebuild refuses non-collision NIF");
	check(bytes(plain)==plainBytes,"non-collision NIF remains byte-identical");
}
