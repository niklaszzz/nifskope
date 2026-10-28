#include "glview.h"
#include "gl/glnode.h"
#include "gl/renderer.h"
#include "model/nifmodel.h"
#include <QLineF>
#include <QFont>
#include <cmath>

namespace {
void beginOverlay( int width, int height )
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); glDisable(GL_FOG);
	glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0,width,height,0,-1,1);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
}
void endOverlay()
{
	glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopAttrib();
}
void circle( const QPointF & center, double radius )
{
	glBegin(GL_LINE_LOOP);
	for(int i=0;i<32;++i) {double t=i*6.283185307179586/32;glVertex2d(center.x()+radius*cos(t),center.y()+radius*sin(t));}
	glEnd();
}
}

void GLView::setEditSelectionMode( Scene::SelMode mode )
{
	finishGizmo(true);
	vertexSelecting=false;
	if(mode==Scene::SelVertex && scene->selMode!=Scene::SelVertex) {
		editMesh=scene->currentBlock;
		selectedVertices.clear();
		refreshEditData();
	} else if(mode!=Scene::SelVertex) {
		editMesh=QModelIndex(); selectedVertices.clear();
	}
	scene->selMode=mode;
	gizmoVisible=false;
	vertexProjectionValid=false;
	update();
}

bool GLView::refreshEditData()
{
	if(!editData.read(model,editMesh,&vertexEditReason)) { selectedVertices.clear(); return false; }
	if(editData.isShared(model)) {
		vertexEditReason=tr("This mesh shares its vertex data with another mesh. Shared-data editing is not supported yet.");
		selectedVertices.clear(); return false;
	}
	for(auto it=selectedVertices.begin();it!=selectedVertices.end();) {
		if(*it<0 || *it>=editData.positions.size()) it=selectedVertices.erase(it); else ++it;
	}
	return true;
}

bool GLView::editSelectionPivot( Vector3 & pivot ) const
{
	if(selectedVertices.isEmpty()) return false;
	pivot=Vector3();
	for(int i:selectedVertices) {
		if(i<0 || i>=editData.positions.size()) return false;
		pivot+=editData.positions[i];
	}
	pivot=pivot/float(selectedVertices.size());
	return true;
}

void GLView::drawEditorOverlay()
{
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadMatrix(viewTrans);
	glGetDoublev(GL_MODELVIEW_MATRIX,gizmoModelview);
	glGetDoublev(GL_PROJECTION_MATRIX,gizmoProjection);
	glGetIntegerv(GL_VIEWPORT,gizmoViewport);
	glPopMatrix();
	lightMarkers.clear();
	vertexProjectionValid=false;
	if(!model) return;
	scene->renderer->stopProgram();
	beginOverlay(width(),height());
	if(scene->options & Scene::ShowCollision) {
		if(physxDirty) {
			QString error;PhysXCollision::collect(model,physxMeshes,error,false);physxDirty=false;
		}
		glLineWidth(1);
		for(const auto & mesh:physxMeshes) {
			bool selected=scene->currentBlock==mesh.source || scene->currentBlock==model->getBlock(mesh.data);
			if(selected)glColor3f(1,.65f,.05f);else glColor3f(0,.85f,1);
			glBegin(GL_LINES);
			for(int t=0;t<mesh.indices.size();t+=3)for(int a=0;a<3;++a) {
				auto first=mesh.vertices[mesh.indices[t+a]],second=mesh.vertices[mesh.indices[t+(a+1)%3]];
				auto v1=mesh.world.map(QVector3D(first[0],first[1],first[2])),v2=mesh.world.map(QVector3D(second[0],second[1],second[2]));
				QPointF p,q;if(projectGizmo(Vector3(v1.x(),v1.y(),v1.z()),p) && projectGizmo(Vector3(v2.x(),v2.y(),v2.z()),q)){glVertex2d(p.x(),p.y());glVertex2d(q.x(),q.y());}
			}
			glEnd();
		}
	}
	if(scene->options & Scene::ShowNodes) {
		for(auto node:scene->nodes.list()) {
			auto block=node->index();
			if(node->isHidden() || !model->inherits(block,"NiLight")) continue;
			QPointF p;
			if(!projectGizmo(node->worldTrans().translation,p)) continue;
			lightMarkers << qMakePair(QPersistentModelIndex(block),p);
			// A fixed-size sun marker remains visible even inside geometry.
			glLineWidth(5); glColor3f(.08f,.08f,.08f); circle(p,9);
			glLineWidth(2.5f);
			if(block==scene->currentBlock) glColor3f(1,.55f,.1f); else glColor3f(1,.95f,.25f);
			circle(p,9);
			glBegin(GL_LINES);
			for(int i=0;i<8;++i) {
				double t=i*6.283185307179586/8;
				glVertex2d(p.x()+12*cos(t),p.y()+12*sin(t)); glVertex2d(p.x()+17*cos(t),p.y()+17*sin(t));
			}
			glEnd();
			glPointSize(5); glBegin(GL_POINTS); glVertex2d(p.x(),p.y()); glEnd();
		}
	}
	if(scene->selMode==Scene::SelVertex && refreshEditData()) {
		auto node=scene->getNode(model,editMesh);
		if(node && !node->isHidden()) {
			vertexProjectionValid=true;
			auto world=node->worldTrans();
			for(int pass=0;pass<2;++pass) {
				glPointSize(pass?9:5); if(pass) glColor3f(1,.6f,.08f); else glColor3f(.25f,.85f,1);
				glBegin(GL_POINTS);
				for(int i=0;i<editData.positions.size();++i) {
					if(selectedVertices.contains(i)!=bool(pass)) continue;
					QPointF p; if(projectGizmo(world*editData.positions[i],p)) glVertex2d(p.x(),p.y());
				}
				glEnd();
			}
		} else vertexEditReason=tr("This mesh is hidden. Enable Show Hidden to edit it.");
	}
	if(vertexSelecting) {
		QRect r=QRect(vertexSelectionStart,vertexSelectionEnd).normalized();
		glLineWidth(1.5f); glColor3f(1,.8f,.2f); glBegin(GL_LINE_LOOP);
		glVertex2d(r.left(),r.top());glVertex2d(r.right(),r.top());glVertex2d(r.right(),r.bottom());glVertex2d(r.left(),r.bottom());glEnd();
	}
	if(snapTarget>=0 && snapTarget<snapTargets.size()) {
		QPointF p; if(projectGizmo(snapTargets[snapTarget],p)) {glColor3f(0,1,1);glLineWidth(3);circle(p,12);}
	}
	endOverlay();
	if(!physxMeshes.isEmpty() && (scene->options & Scene::ShowCollision)) {
		glColor3f(0,.85f,1);
		renderText(12,height()-16,tr("PhysX collision: stored mesh | Edit source meshes, then Rebuild Collision"),QFont("Sans Serif",9));
	}
	if(scene->selMode==Scene::SelVertex) {
		qglColor(QColor(255,225,150));
		QString text=vertexEditReason.isEmpty()
			? tr("Vertex Edit: %1 / %2 selected (X-ray)%3")
				.arg(selectedVertices.size()).arg(editData.positions.size()).arg(vertexSnap?tr(" | Snap on"):QString())
			: tr("Vertex Edit: %1").arg(vertexEditReason);
		renderText(12,22,text,QFont("Sans Serif",9));
		if(vertexEditReason.isEmpty()) renderText(12,40,tr("LMB box | Shift add | Ctrl toggle | Space+LMB pan | Alt+LMB orbit"),QFont("Sans Serif",9));
	}
}

void GLView::selectEditVertices( const QPoint & end )
{
	if(!vertexProjectionValid || !refreshEditData()) return;
	auto node=scene->getNode(model,editMesh); if(!node) return;
	auto world=node->worldTrans();
	QSet<int> hits;
	QRectF rectangle=QRect(vertexSelectionStart,end).normalized();
	bool click=(vertexSelectionStart-end).manhattanLength()<=4;
	double best=10; int closest=-1;
	for(int i=0;i<editData.positions.size();++i) {
		QPointF p; if(!projectGizmo(world*editData.positions[i],p)) continue;
		if(click) {double distance=QLineF(end,p).length();if(distance<best){best=distance;closest=i;}}
		else if(rectangle.contains(p)) hits.insert(i);
	}
	if(click && closest>=0) hits.insert(closest);
	if(!(vertexSelectionModifiers & (Qt::ControlModifier|Qt::ShiftModifier))) selectedVertices.clear();
	for(int i:hits) {
		if(vertexSelectionModifiers & Qt::ControlModifier) {if(selectedVertices.contains(i))selectedVertices.remove(i);else selectedVertices.insert(i);}
		else selectedVertices.insert(i);
	}
	gizmoVisible=false;
}

QModelIndex GLView::collisionAt(const QPoint & point) const
{
	if(!(scene->options & Scene::ShowCollision) || physxDirty)return QModelIndex();
	double best=5;QModelIndex hit;
	for(const auto & mesh:physxMeshes) {
		if(!mesh.source.isValid())continue;
		for(int t=0;t<mesh.indices.size();t+=3)for(int a=0;a<3;++a) {
			auto first=mesh.vertices[mesh.indices[t+a]],second=mesh.vertices[mesh.indices[t+(a+1)%3]];
			auto v1=mesh.world.map(QVector3D(first[0],first[1],first[2])),v2=mesh.world.map(QVector3D(second[0],second[1],second[2]));
			QPointF p,q;if(!projectGizmo(Vector3(v1.x(),v1.y(),v1.z()),p) || !projectGizmo(Vector3(v2.x(),v2.y(),v2.z()),q))continue;
			QPointF delta=q-p;double length=QPointF::dotProduct(delta,delta);
			double u=length>0?qBound(0.0,QPointF::dotProduct(QPointF(point)-p,delta)/length,1.0):0;
			double distance=QLineF(point,p+delta*u).length();if(distance<best){best=distance;hit=mesh.source;}
		}
	}
	return hit;
}

QModelIndex GLView::lightAt( const QPoint & point ) const
{
	if(!(scene->options & Scene::ShowNodes)) return QModelIndex();
	double best=20; QModelIndex result;
	for(const auto & light:lightMarkers) {
		double distance=QLineF(point,light.second).length();
		if(light.first.isValid() && distance<best) {best=distance;result=light.first;}
	}
	return result;
}

void GLView::buildSnapTargets()
{
	snapTargets.clear(); snapTarget=-1;
	double best=1e30;
	auto selectedNode=scene->getNode(model,editMesh);
	if(!selectedNode) return;
	if(!selectedVertices.isEmpty()) snapAnchor=selectedNode->worldTrans()*editData.positions[*selectedVertices.cbegin()];
	for(int i:selectedVertices) {
		auto world=selectedNode->worldTrans()*editData.positions[i];
		QPointF p; if(projectGizmo(world,p)) {
			double distance=QLineF(gizmoPress,p).length();if(distance<best){best=distance;snapAnchor=world;}
		}
	}
	if(!vertexSnap) return;
	for(auto node:scene->nodes.list()) {
		if(node->isHidden()) continue;
		VertexEditData data;
		if(!data.read(model,node->index())) continue;
		for(int i=0;i<data.positions.size();++i) {
			if(node->index()==editMesh && selectedVertices.contains(i)) continue;
			auto world=node->worldTrans()*data.positions[i];
			QPointF p; if(projectGizmo(world,p) && rect().contains(p.toPoint())) snapTargets<<world;
		}
	}
}

bool GLView::writeVertexTransform( const Transform & transform )
{
	QVector<Vector3> result=vertexDragPositions;
	for(int i:vertexDragSelection)
		result[i]=transform.translation + transform.rotation*(vertexDragPositions[i]-gizmoStart.translation)*transform.scale;
	return editData.write(model,result,vertexDragSelection);
}
