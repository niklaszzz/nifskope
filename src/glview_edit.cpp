#include "glview.h"
#include "gl/glnode.h"
#include "gl/renderer.h"
#include "model/nifmodel.h"
#include "model/roomedit.h"
#include <QLineF>
#include <QFont>
#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <cmath>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

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
	faceSelection=false; selectedFaces.clear();
	vertexSelecting=false;
	if(mode==Scene::SelVertex && scene->selMode!=Scene::SelVertex) {
		editMesh=scene->currentBlock;
		selectedVertices.clear();
		if(makeMeshGeometryUnique(model,editMesh,&vertexEditReason))refreshEditData();
	} else if(mode!=Scene::SelVertex) {
		editMesh=QModelIndex(); selectedVertices.clear();
	}
	scene->selMode=mode;
	gizmoVisible=false;
	vertexProjectionValid=false;
	emit editSelectionChanged();
	update();
}

bool GLView::refreshEditData()
{
	if(!editData.read(model,editMesh,&vertexEditReason)) { selectedVertices.clear(); return false; }
	if(editData.isShared(model)) {
		vertexEditReason=tr("This mesh shares its vertex data with another mesh. Shared-data editing is not supported yet.");
		selectedVertices.clear(); return false;
	}
	if(faceSelection && !faceData.read(model,editMesh,&vertexEditReason)) {selectedVertices.clear();selectedFaces.clear();return false;}
	if(faceSelection) {
        for(auto it=selectedFaces.begin();it!=selectedFaces.end();) {
            if(*it<0 || *it>=faceData.triangles.size())it=selectedFaces.erase(it);else ++it;
        }
        selectedVertices.clear();
        for(int f:selectedFaces)for(int a=0;a<3;++a)selectedVertices.insert(faceData.triangles[f][a]);
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

QVector<QPair<Vector3,Vector3>> GLView::lightIndicatorLines(
    const QModelIndex & block, const Transform & world, float length) const
{
    QVector<QPair<Vector3,Vector3>> lines;
    if (!model || !std::isfinite(length) || length <= 0)
        return lines;
    // Gamebryo lights shine along local +X. Rotation includes all scene parents;
    // scale affects the position, but not this fixed-screen-size direction marker.
    auto point = [&](const Vector3 & local) { return world.translation + world.rotation * local * length; };
    auto line = [&](const Vector3 & a, const Vector3 & b) { lines.append(qMakePair(point(a), point(b))); };
    const float tau = 6.28318530718f;
    auto ring = [&](int axis, float center, float radius) {
        for (int i = 0; i < 32; ++i) {
            Vector3 a, b;
            a[axis] = b[axis] = center;
            a[(axis+1)%3] = radius * cos(tau*i/32);
            a[(axis+2)%3] = radius * sin(tau*i/32);
            b[(axis+1)%3] = radius * cos(tau*(i+1)/32);
            b[(axis+2)%3] = radius * sin(tau*(i+1)/32);
            line(a,b);
        }
    };
    if (model->inherits(block,"NiDirectionalLight") || model->inherits(block,"NiSpotLight")) {
        line(Vector3(),Vector3(1,0,0));
        for (int axis = 1; axis <= 2; ++axis)
            for (float sign : {-1.f,1.f}) {
                Vector3 tail(.78f,0,0); tail[axis] = sign*.1f;
                line(Vector3(1,0,0),tail);
            }
        if (model->inherits(block,"NiSpotLight")) {
            auto cone = [&](float degrees, bool spokes) {
                if (!std::isfinite(degrees) || degrees <= 0) return;
                float angle = qBound(0.f,degrees,180.f)*tau/360;
                float center = cos(angle), radius = sin(angle);
                ring(0,center,radius);
                if (spokes) for (int i=0; i<4; ++i)
                    line(Vector3(),Vector3(center,radius*cos(tau*i/4),radius*sin(tau*i/4)));
            };
            float outer = model->get<float>(block,"Outer Spot Angle");
            cone(outer,true);
            float inner = model->get<float>(block,"Inner Spot Angle");
            if (inner < outer) cone(inner,false);
        }
    } else if (model->inherits(block,"NiPointLight")) {
        // A point light has no aim: a globe and six rays indicate omnidirectional emission.
        for (int axis=0; axis<3; ++axis) {
            ring(axis,0,.18f);
            for (float sign : {-1.f,1.f}) {
                Vector3 a,b; a[axis]=sign*.23f; b[axis]=sign*.36f; line(a,b);
            }
        }
    }
    return lines;
}

double GLView::lightAttenuationRadius(const QModelIndex & block, double fraction) const
{
    if (!model || !model->inherits(block,"NiPointLight") || !std::isfinite(fraction) || fraction <= 0)
        return -1;
    double constant=model->get<float>(block,"Constant Attenuation");
    double linear=model->get<float>(block,"Linear Attenuation");
    double quadratic=model->get<float>(block,"Quadratic Attenuation");
    if (!std::isfinite(constant) || !std::isfinite(linear) || !std::isfinite(quadratic)
        || constant < 0 || linear < 0 || quadratic < 0)
        return -1;
    // Solve C + L*d + Q*d*d = 1/fraction. This is a falloff contour, not a cutoff.
    double remaining=1/fraction-constant;
    if (remaining <= 0) return 0;
    double radius=-1;
    if (quadratic > 0)
        radius=2*remaining/(linear+std::sqrt(linear*linear+4*quadratic*remaining));
    else if (linear > 0)
        radius=remaining/linear;
    return std::isfinite(radius)?radius:-1;
}

QVector<QPair<Vector3,Vector3>> GLView::lightFalloffLines(
    const QModelIndex & block, const Transform & world, double fraction) const
{
    QVector<QPair<Vector3,Vector3>> lines;
    double radius=lightAttenuationRadius(block,fraction);
    if (radius <= 0) return lines;
    const double tau=6.283185307179586;
    // Attenuation uses world-space distance, independently of the light's transform scale.
    auto point=[&](const Vector3 & direction) {
        return world.translation+world.rotation*direction*float(radius);
    };
    auto line=[&](const Vector3 & a,const Vector3 & b) {lines.append(qMakePair(point(a),point(b)));};
    if (model->inherits(block,"NiSpotLight")) {
        double degrees=model->get<float>(block,"Outer Spot Angle");
        if (!std::isfinite(degrees) || degrees <= 0) return lines;
        double angle=qBound(0.0,degrees,180.0)*tau/360;
        for (int i=0;i<96;++i)
            line(Vector3(cos(angle),sin(angle)*cos(tau*i/96),sin(angle)*sin(tau*i/96)),
                Vector3(cos(angle),sin(angle)*cos(tau*(i+1)/96),sin(angle)*sin(tau*(i+1)/96)));
        for (int spoke=0;spoke<4;++spoke) {
            double y=cos(tau*spoke/4),z=sin(tau*spoke/4);
            line(Vector3(),Vector3(cos(angle),sin(angle)*y,sin(angle)*z));
            for (int i=0;i<32;++i) {
                double a=angle*i/32,b=angle*(i+1)/32;
                line(Vector3(cos(a),sin(a)*y,sin(a)*z),Vector3(cos(b),sin(b)*y,sin(b)*z));
            }
        }
    } else {
        for (int axis=0;axis<3;++axis) for (int i=0;i<128;++i) {
            Vector3 a,b;
            a[(axis+1)%3]=cos(tau*i/128);a[(axis+2)%3]=sin(tau*i/128);
            b[(axis+1)%3]=cos(tau*(i+1)/128);b[(axis+2)%3]=sin(tau*(i+1)/128);
            line(a,b);
        }
    }
    return lines;
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
	QString lightFalloffHint;
	 scene->renderer->stopProgram();
 editDepth.clear();
 if(!editXRay && scene->selMode==Scene::SelVertex) {
  editDepth.resize(gizmoViewport[2]*gizmoViewport[3]);
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(GL_PACK_ALIGNMENT,1);
  glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
  glReadPixels(gizmoViewport[0],gizmoViewport[1],gizmoViewport[2],gizmoViewport[3],GL_DEPTH_COMPONENT,GL_FLOAT,editDepth.data());
  glPopClientAttrib();
 }
	beginOverlay(width(),height());
	if(showRoomOverlay){
		RoomScene rooms(model);int selected=model->getBlockNumber(scene->currentBlock);auto assigned=rooms.memberships(selected);
		glLineWidth(1.5f);
		for(const auto & room:rooms.rooms){bool active=room.id==selected || assigned.contains(room.id);if(active)glColor3f(1,.65f,.1f);else glColor3f(.35f,.65f,.85f);auto edges=rooms.cage(room.id);glBegin(GL_LINES);for(auto edge:edges){QPointF a,b;if(projectGizmo(edge.first,a) && projectGizmo(edge.second,b)){glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());}}glEnd();
			for(int id:room.outgoing){auto portal=model->getBlock(id);if(model->getBlockName(portal)!="NiPortal")continue;Transform transform;if(!rooms.world(id,transform))continue;auto vertices=model->getArray<Vector3>(portal,"Vertices");if(vertices.size()<3)continue;glColor3f(.2f,1,.65f);glBegin(GL_LINES);for(int v=0;v<vertices.size();++v){QPointF a,b;if(projectGizmo(transform*vertices[v],a) && projectGizmo(transform*vertices[(v+1)%vertices.size()],b)){glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());}}glEnd();}
		}
	}
	if(highlightedPortal>=0){RoomScene rooms(model);Transform transform;auto block=model->getBlock(highlightedPortal);if(model->getBlockName(block)=="NiPortal" && rooms.world(highlightedPortal,transform)){auto points=model->getArray<Vector3>(block,"Vertices");glLineWidth(3);glColor3f(1,.8f,.1f);glBegin(GL_LINES);for(int i=0;i<points.size();++i){QPointF a,b;if(projectGizmo(transform*points[i],a) && projectGizmo(transform*points[(i+1)%points.size()],b)){glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());}}glEnd();}}
	if(!portalPreview.isEmpty()){glLineWidth(3);glColor3f(1,.25f,.85f);glBegin(GL_LINES);for(auto edge:portalPreview){QPointF a,b;if(projectGizmo(edge.first,a) && projectGizmo(edge.second,b)){glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());}}glEnd();}
	if(scene->options & Scene::ShowCollision) {
		if(physxDirty) {
			QString error;PhysXCollision::collect(model,physxMeshes,error,false);physxDirty=false;
		}
		glLineWidth(1);
		for(const auto & mesh:physxMeshes) {
			bool selected=scene->currentBlock==mesh.source || scene->currentBlock==model->getBlock(mesh.data);
			if(mesh.sourceMeshes.contains(scene->currentBlock))selected=true;
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
			// Convert 80 screen pixels to world units at the light's depth, in either projection.
            auto origin=node->worldTrans().translation;
            GLdouble x,y,z,ax,ay,az,bx,by,bz;
            float length=0;
            if(gluProject(origin[0],origin[1],origin[2],gizmoModelview,gizmoProjection,gizmoViewport,&x,&y,&z)
                && gluUnProject(x,y,z,gizmoModelview,gizmoProjection,gizmoViewport,&ax,&ay,&az)
                && gluUnProject(x+80,y,z,gizmoModelview,gizmoProjection,gizmoViewport,&bx,&by,&bz))
                length=Vector3(bx-ax,by-ay,bz-az).length();
            auto lines=lightIndicatorLines(block,node->worldTrans(),length);
            if(block==scene->currentBlock && model->inherits(block,"NiPointLight")) {
                double inner=lightAttenuationRadius(block,.1),outer=lightAttenuationRadius(block,.01);
                lightFalloffHint=inner<0 || outer<0
                    ? tr("Falloff radius unavailable for these attenuation values.")
                    : tr("Estimated falloff: 10% at %1 | 1% at %2 world units (no hard cutoff)")
                        .arg(inner,0,'f',1).arg(outer,0,'f',1);
                for(int contour=0;contour<2;++contour) {
                    auto edges=lightFalloffLines(block,node->worldTrans(),contour?.01:.1);
                    if(contour){glEnable(GL_LINE_STIPPLE);glLineStipple(1,0x0F0F);}
                    for(int pass=0;pass<2;++pass) {
                        glLineWidth(pass?1.2f:3.5f);
                        if(pass)glColor3f(1,.65f,.15f);else glColor3f(.08f,.08f,.08f);
                        glBegin(GL_LINES);
                        for(const auto & edge:edges) {
                            QPointF a,b;
                            if(projectGizmo(edge.first,a) && projectGizmo(edge.second,b)) {
                                glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());
                            }
                        }
                        glEnd();
                    }
                    glDisable(GL_LINE_STIPPLE);
                }
            }
            // A dark outline keeps the icons and arrows readable through geometry.
            for(int pass=0;pass<2;++pass) {
                glLineWidth(pass?2.f:5.f);
                if(!pass)glColor3f(.08f,.08f,.08f);
                else if(block==scene->currentBlock)glColor3f(1,.55f,.1f);
                else glColor3f(1,.95f,.25f);
                circle(p,9);
                if(model->inherits(block,"NiAmbientLight"))circle(p,5);
                glBegin(GL_LINES);
                for(const auto & edge:lines) {
                    QPointF a,b;
                    if(projectGizmo(edge.first,a) && projectGizmo(edge.second,b)) {
                        glVertex2d(a.x(),a.y());glVertex2d(b.x(),b.y());
                    }
                }
                if(model->inherits(block,"NiDirectionalLight"))for(int i=0;i<8;++i) {
                    double t=i*6.283185307179586/8;
                    glVertex2d(p.x()+12*cos(t),p.y()+12*sin(t));
                    glVertex2d(p.x()+17*cos(t),p.y()+17*sin(t));
                }
                glEnd();
            }
			glPointSize(5); glBegin(GL_POINTS); glVertex2d(p.x(),p.y()); glEnd();
		}
	}
	if(scene->selMode==Scene::SelVertex && refreshEditData()) {
		auto node=scene->getNode(model,editMesh);
		if(node && !node->isHidden()) {
			vertexProjectionValid=true;
			auto world=node->worldTrans();
            if(!editXRay){glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);}
            auto point=[&](const Vector3 & v){QPointF p;double depth;if(projectEditPoint(world*v,p,depth))glVertex3d(p.x(),p.y(),1-2*qMax(0.0,depth-0.000002));};
            FaceEditData wire;
            if(wire.read(model,editMesh)) {
                if(faceSelection) {
                    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,.55f,.05f,.3f);glBegin(GL_TRIANGLES);
                    for(int f:selectedFaces)if(f>=0 && f<wire.triangles.size()) {
                        auto t=wire.triangles[f];QPointF p;double depth;bool valid=true;
                        for(int a=0;a<3;++a)valid=projectEditPoint(world*editData.positions[t[a]],p,depth) && valid;
                        if(valid)for(int a=0;a<3;++a)point(editData.positions[t[a]]);
                    }
                    glEnd();glDisable(GL_BLEND);
                }
                glLineWidth(1);glBegin(GL_LINES);
                for(int f=0;f<wire.triangles.size();++f) {
                    auto t=wire.triangles[f];
                    if(faceSelection && selectedFaces.contains(f))glColor3f(1,.6f,.08f);else glColor3f(.2f,.8f,1);
                    for(int a=0;a<3;++a){QPointF p;double z;
                        if(projectEditPoint(world*editData.positions[t[a]],p,z) && projectEditPoint(world*editData.positions[t[(a+1)%3]],p,z)){
                            point(editData.positions[t[a]]);point(editData.positions[t[(a+1)%3]]);
                        }
                    }
                }
                glEnd();
            }
            for(int pass=0;pass<2 && !faceSelection;++pass) {
                glPointSize(pass?9:5);if(pass)glColor3f(1,.6f,.08f);else glColor3f(.25f,.85f,1);
                glBegin(GL_POINTS);
                for(int i=0;i<editData.positions.size();++i)if(selectedVertices.contains(i)==bool(pass))point(editData.positions[i]);
                glEnd();
            }
            glDisable(GL_DEPTH_TEST);
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
	if(!lightFalloffHint.isEmpty()) {
		qglColor(QColor(255,205,120));
		renderText(12,scene->selMode==Scene::SelVertex?58:22,lightFalloffHint,QFont("Sans Serif",9));
	}
	if(!physxMeshes.isEmpty() && (scene->options & Scene::ShowCollision)) {
		glColor3f(0,.85f,1);
		renderText(12,height()-16,tr("PhysX collision: stored mesh | Edit source meshes, then Rebuild Collision"),QFont("Sans Serif",9));
	}
	if(flyNavigating){qglColor(QColor(255,225,150));renderText(12,height()-36,tr("RMB fly | WASD move | Q/E down/up | Shift faster | Wheel speed: %1x").arg(flySpeedMultiplier,0,'f',2),QFont("Sans Serif",9));}
	if(scene->selMode==Scene::SelVertex) {
		qglColor(QColor(255,225,150));
		QString text=vertexEditReason.isEmpty()
			? tr("%4 Edit: %1 / %2 selected (%5)%3")
				.arg(faceSelection?selectedFaces.size():selectedVertices.size()).arg(faceSelection?faceData.triangles.size():editData.positions.size()).arg(vertexSnap?tr(" | Snap on"):QString()).arg(faceSelection?tr("Face"):tr("Vertex")).arg(editXRay?tr("X-ray"):tr("Visible only"))
			: tr("Vertex Edit: %1").arg(vertexEditReason);
		renderText(12,22,text,QFont("Sans Serif",9));
		if(vertexEditReason.isEmpty()) renderText(12,40,tr("LMB box | Ctrl+L linked | W/E/R tools | RMB fly | Space+LMB pan | Alt+LMB orbit"),QFont("Sans Serif",9));
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
	if(faceSelection) {
        double best=1e30;int closest=-1;
        auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
        for(int f=0;f<faceData.triangles.size();++f) {
            auto t=faceData.triangles[f];QPointF p[3];bool valid=true;
            for(int a=0;a<3;++a)valid=projectGizmo(world*editData.positions[t[a]],p[a]) && valid;
            if(!valid)continue;
            if(click) {
                QPointF q=end;double a=cross(p[1]-p[0],q-p[0]),b=cross(p[2]-p[1],q-p[1]),c=cross(p[0]-p[2],q-p[2]);
                if(std::abs(cross(p[1]-p[0],p[2]-p[0]))<.001)continue;
                if((a>=0 && b>=0 && c>=0)||(a<=0 && b<=0 && c<=0)) {
                    double area=cross(p[1]-p[0],p[2]-p[0]);
                    double weights[3]={cross(p[1]-q,p[2]-q)/area,cross(p[2]-q,p[0]-q)/area,cross(p[0]-q,p[1]-q)/area};
                    double depth=0;
                    for(int v=0;v<3;++v) {
                        double z=-(viewTrans*(world*editData.positions[t[v]]))[2];
                        depth+=weights[v]*((perspectiveMode || view==ViewWalk)?1.0/z:z);
                    }
                    if(perspectiveMode || view==ViewWalk)depth=1.0/depth;
                                        Vector3 hit;
                    for(int v=0;v<3;++v){double w=weights[v];if(perspectiveMode || view==ViewWalk)w*=depth/(-(viewTrans*(world*editData.positions[t[v]]))[2]);hit+=editData.positions[t[v]]*float(w);}
                    if(editPointVisible(world*hit) && depth<best){best=depth;closest=f;}
                }
            } else if(rectangle.contains((p[0]+p[1]+p[2])/3.0) && editPointVisible(world*((editData.positions[t[0]]+editData.positions[t[1]]+editData.positions[t[2]])/3.0f)))hits.insert(f);
        }
        if(click && closest>=0)hits.insert(closest);
        if(!(vertexSelectionModifiers & (Qt::ControlModifier|Qt::ShiftModifier)))selectedFaces.clear();
        for(int f:hits){if((vertexSelectionModifiers & Qt::ControlModifier) && selectedFaces.contains(f))selectedFaces.remove(f);else selectedFaces.insert(f);}
        selectedVertices.clear();for(int f:selectedFaces)if(f>=0 && f<faceData.triangles.size())for(int a=0;a<3;++a)selectedVertices.insert(faceData.triangles[f][a]);
        gizmoVisible=false;emit editSelectionChanged();return;
    }
    double best=10; int closest=-1;
	for(int i=0;i<editData.positions.size();++i) {
		QPointF p; if(!projectGizmo(world*editData.positions[i],p) || !editPointVisible(world*editData.positions[i])) continue;
		if(click) {double distance=QLineF(end,p).length();if(distance<best){best=distance;closest=i;}}
		else if(rectangle.contains(p)) hits.insert(i);
	}
	if(click && closest>=0) {
		const auto position=editData.positions[closest];
		for(int i=0;i<editData.positions.size();++i)
			if(editData.positions[i]==position)hits.insert(i);
	}
	if(!(vertexSelectionModifiers & (Qt::ControlModifier|Qt::ShiftModifier))) selectedVertices.clear();
	for(int i:hits) {
		if(vertexSelectionModifiers & Qt::ControlModifier) {if(selectedVertices.contains(i))selectedVertices.remove(i);else selectedVertices.insert(i);}
		else selectedVertices.insert(i);
	}
	gizmoVisible=false;emit editSelectionChanged();
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

void GLView::setFaceSelectionMode()
{
 setEditSelectionMode(Scene::SelVertex);faceSelection=true;selectedVertices.clear();selectedFaces.clear();refreshEditData();update();
}
void GLView::deleteSelectedFaces()
{
 if(!faceSelection || !refreshEditData() || selectedFaces.isEmpty())return;
 finishGizmo(true);model->beginUndoTransaction(tr("Delete faces"));
 faceData.remove(model,selectedFaces);model->endUndoTransaction();
 selectedFaces.clear();selectedVertices.clear();gizmoVisible=false;emit editSelectionChanged();update();
}

bool GLView::projectEditPoint(const Vector3 & world,QPointF & screen,double & depth) const
{
 GLdouble x,y,z;
 if(!gluProject(world[0],world[1],world[2],gizmoModelview,gizmoProjection,gizmoViewport,&x,&y,&z) || z<0 || z>1 || !std::isfinite(x) || !std::isfinite(y))return false;
 screen=QPointF(x,height()-y);depth=z;return true;
}

bool GLView::editPointVisible(const Vector3 & world) const
{
 if(editXRay)return true;
 QPointF screen;double depth;if(!projectEditPoint(world,screen,depth))return false;
 int x=int(screen.x())-gizmoViewport[0],y=int(height()-screen.y())-gizmoViewport[1];
 if(x<0 || y<0 || x>=gizmoViewport[2] || y>=gizmoViewport[3] || editDepth.size()!=gizmoViewport[2]*gizmoViewport[3])return false;
 return depth<=editDepth[y*gizmoViewport[2]+x]+0.000002;
}

void GLView::deleteSelectedVertices()
{
 if(scene->selMode!=Scene::SelVertex || faceSelection || !refreshEditData() || selectedVertices.isEmpty())return;
 finishGizmo(true);
 if(deleteMeshVertices(model,editMesh,selectedVertices,&vertexEditReason)){
  selectedVertices.clear();selectedFaces.clear();refreshEditData();
 }
 gizmoVisible=false;vertexProjectionValid=false;emit editSelectionChanged();update();
}

void GLView::cycleSelectionMode()
{
 if(flyNavigating || gizmoAxis>=0 || vertexSelecting)return;
 int mode;
 if(scene->selMode!=Scene::SelVertex){setEditSelectionMode(Scene::SelVertex);mode=1;}
 else if(!faceSelection){setFaceSelectionMode();mode=2;}
 else {setEditSelectionMode(Scene::SelObject);mode=0;}
 emit selectionModeCycled(mode);
}

void GLView::selectLinkedEditElements()
{
 if(flyNavigating || gizmoAxis>=0 || vertexSelecting || scene->selMode!=Scene::SelVertex || !refreshEditData())return;
 FaceEditData topology;if(!topology.read(model,editMesh,&vertexEditReason))return;
 QVector<int> parent(editData.positions.size());for(int i=0;i<parent.size();++i)parent[i]=i;
 auto root=[&](int i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
 for(auto triangle:topology.triangles) {
  int a=root(triangle[0]);for(int v=1;v<3;++v){int b=root(triangle[v]);if(a!=b)parent[b]=a;}
 }
 QSet<int> components;
 if(faceSelection){for(int f:selectedFaces)if(f>=0 && f<topology.triangles.size())components.insert(root(topology.triangles[f][0]));}
 else {for(int v:selectedVertices)components.insert(root(v));}
 if(components.isEmpty())return;
 if(faceSelection) {
  for(int f=0;f<topology.triangles.size();++f)if(components.contains(root(topology.triangles[f][0])))selectedFaces.insert(f);
  selectedVertices.clear();for(int f:selectedFaces)for(int a=0;a<3;++a)selectedVertices.insert(topology.triangles[f][a]);
 } else {
  for(int v=0;v<parent.size();++v)if(components.contains(root(v)))selectedVertices.insert(v);
 }
 gizmoVisible=false;emit editSelectionChanged();update();
}

void GLView::setIsolateSelection(bool enabled)
{
 auto block=scene->selMode==Scene::SelVertex?QModelIndex(editMesh):QModelIndex(scene->currentBlock);
 if(enabled && model && block.isValid() && scene->getNode(model,block))scene->isolatedRoot=block;
 else scene->isolatedRoot=QModelIndex();
 gizmoVisible=false;vertexProjectionValid=false;emit isolationChanged(scene->isolatedRoot.isValid());update();
}

bool GLView::applyNumericTransform(GizmoMode mode,const Vector3 & amount,bool worldSpace,QString * reason)
{
 auto fail=[&](const QString & text){if(reason)*reason=text;return false;};
 if(!model || gizmoAxis>=0 || flyNavigating || ((animState & AnimEnabled) && (animState & AnimPlay)))return fail(tr("Pause animation and finish the active interaction first."));
 for(int a=0;a<3;++a)if(!std::isfinite(amount[a]))return fail(tr("Enter finite transform values."));
 if(mode!=GizmoMove && mode!=GizmoRotate && mode!=GizmoScale)return fail(tr("Choose Move, Rotate or Scale."));
 if(mode==GizmoScale && amount[0]<=0)return fail(tr("The scale factor must be greater than zero."));
 bool vertices=scene->selMode==Scene::SelVertex;
 auto block=vertices?QModelIndex(editMesh):QModelIndex(scene->currentBlock);auto node=scene->getNode(model,block);
 if(!node || node->isHidden() || !Transform::canConstruct(model,block))return fail(tr("Select a visible editable object or mesh."));
 const auto world=node->worldTrans();const auto parent=vertices?world:(node->parentNode()?node->parentNode()->worldTrans():Transform());
 if(std::abs(parent.scale)<1e-8f)return fail(tr("Cannot transform beneath a zero-scale parent."));
 Vector3 pivot;
 if(vertices && (!refreshEditData() || !editSelectionPivot(pivot)))return fail(tr("Select vertices or faces first."));
 const float radians=3.141592653589793f/180.f;Matrix rotation=Matrix::euler(amount[0]*radians,amount[1]*radians,amount[2]*radians);
 if(vertices) {
  auto result=editData.positions;
  Vector3 delta=worldSpace?world.rotation.inverted()*amount/world.scale:amount;
  Matrix localRotation=worldSpace?world.rotation.inverted()*rotation*world.rotation:rotation;
  for(int i:selectedVertices) {
   if(mode==GizmoMove)result[i]+=delta;
   else if(mode==GizmoRotate)result[i]=pivot+localRotation*(result[i]-pivot);
   else result[i]=pivot+(result[i]-pivot)*amount[0];
  }
  model->beginUndoTransaction(tr("Numeric transform"));bool ok=editData.write(model,result,selectedVertices);if(ok)editData.updateBounds(model);model->endUndoTransaction();
  if(!ok)return fail(tr("The selected vertex storage cannot represent these values."));
 } else {
  Transform start(model,block),result=start;
  if(mode==GizmoMove){auto delta=worldSpace?amount:world.rotation*amount;result.translation+=parent.rotation.inverted()*delta/parent.scale;}
  if(mode==GizmoRotate)result.rotation=worldSpace?parent.rotation.inverted()*rotation*world.rotation:start.rotation*rotation;
  if(mode==GizmoScale)result.scale*=amount[0];
  if(meshCenterPivot && mode!=GizmoMove && meshPivot(block,pivot))result.translation=start.translation+start.rotation*pivot*start.scale-result.rotation*pivot*result.scale;
  if(!std::isfinite(result.scale))return fail(tr("Scale is outside the supported range."));
  for(int a=0;a<3;++a)if(!std::isfinite(result.translation[a]))return fail(tr("Translation is outside the supported range."));
  model->beginUndoTransaction(tr("Numeric transform"));result.writeBack(model,block);model->endUndoTransaction();
 }
 if(reason)reason->clear();gizmoVisible=false;vertexProjectionValid=false;update();return true;
}

void GLView::showNumericTransform()
{
 QDialog dialog(window());dialog.setObjectName("numericTransformDialog");dialog.setWindowTitle(tr("Numeric Transform"));auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
 auto operation=new QComboBox(&dialog);operation->setObjectName("numericOperation");operation->addItems({tr("Move by"),tr("Rotate by (degrees)"),tr("Scale by (uniform factor)")});operation->setCurrentIndex(gizmoMode==GizmoRotate?1:gizmoMode==GizmoScale?2:0);form->addRow(tr("Operation"),operation);
 auto space=new QComboBox(&dialog);space->setObjectName("numericSpace");space->addItems({tr("Local axes"),tr("World axes")});space->setCurrentIndex(worldAxes?1:0);form->addRow(tr("Space"),space);
 QDoubleSpinBox * fields[3];for(int a=0;a<3;++a){fields[a]=new QDoubleSpinBox(&dialog);fields[a]->setObjectName(QString("numeric%1").arg(a));fields[a]->setDecimals(6);fields[a]->setRange(-1000000,1000000);form->addRow(QString("XYZ").mid(a,1),fields[a]);}layout->addLayout(form);
 auto initialize=[&]{bool scale=operation->currentIndex()==2;for(int a=0;a<3;++a){fields[a]->setValue(scale?1:0);fields[a]->setEnabled(!scale || a==0);}fields[0]->setMinimum(scale?.000001:-1000000);};connect(operation,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,initialize);initialize();
 auto status=new QLabel(tr("Amounts are relative to the current selection. Scale is uniform."),&dialog);status->setWordWrap(true);layout->addWidget(status);
 auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);connect(buttons->button(QDialogButtonBox::Close),&QPushButton::clicked,&dialog,&QDialog::reject);
 connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,[&]{Vector3 values(fields[0]->value(),fields[1]->value(),fields[2]->value());QString error;if(applyNumericTransform(GizmoMode(operation->currentIndex()+1),values,space->currentIndex()==1,&error))status->setText(tr("Transform applied. Ctrl+Z in the main window undoes it."));else status->setText(error);});dialog.exec();
}
