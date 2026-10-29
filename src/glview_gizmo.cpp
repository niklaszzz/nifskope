#include "glview.h"
#include "gl/glnode.h"
#include "gl/renderer.h"
#include "model/nifmodel.h"

#include <QLineF>
#include <QPolygonF>
#include <QUndoStack>
#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int ringSegments = 96;

double segmentDistance( const QPointF & p, const QPointF & a, const QPointF & b )
{
	QPointF d = b - a;
	double length2 = QPointF::dotProduct( d, d );
	double t = length2 > 0 ? qBound( 0.0, QPointF::dotProduct( p - a, d ) / length2, 1.0 ) : 0;
	return QLineF( p, a + d * t ).length();
}

}

void GLView::setGizmoMode( GizmoMode mode )
{
	finishGizmo( true );
	gizmoMode = mode;
	gizmoVisible = false;
	emit gizmoModeChanged(int(mode));
	update();
}

void GLView::setMeshCenterPivot(bool enabled)
{
	finishGizmo(true);
	meshCenterPivot=enabled;gizmoVisible=false;update();
}

bool GLView::meshPivot(const QModelIndex & block,Vector3 & pivot) const
{
	VertexEditData data;
	if(!data.read(model,block) || data.positions.isEmpty())return false;
	Vector3 low=data.positions.first(),high=low;
	for(auto v:data.positions)for(int a=0;a<3;++a) {
		if(!std::isfinite(v[a]))return false;
		low[a]=qMin(low[a],v[a]);high[a]=qMax(high[a],v[a]);
	}
	pivot=(low+high)*.5f;return true;
}

bool GLView::projectGizmo( const Vector3 & point, QPointF & screen ) const
{
	GLdouble x, y, z;
	if ( !gluProject( point[0], point[1], point[2], gizmoModelview, gizmoProjection,
		gizmoViewport, &x, &y, &z ) || z <= 0 || z >= 1 )
		return false;
	screen = QPointF( x, height() - y );
	return std::isfinite( x ) && std::isfinite( y );
}

void GLView::drawGizmo()
{
	// The same projected geometry is used for drawing and picking.
	{
		gizmoVisible = false;
		if ( !model || gizmoMode == GizmoSelect
			 || ((animState & AnimEnabled) && (animState & AnimPlay)) )
			return;
		auto block = scene->selMode == Scene::SelVertex ? editMesh : scene->currentBlock;
		if(model->inherits(block,"NiLight") && !(scene->options & Scene::ShowNodes)) return;
		if ( !block.isValid() || !model->inherits( block, "NiAVObject" )
			 || !Transform::canConstruct( model, block ) )
			return;
		auto node = scene->getNode( model, block );
		if ( !node || node->isHidden() )
			return;
		const auto world = node->worldTrans();
		gizmoOrigin = world.translation;
		if(meshCenterPivot && scene->selMode!=Scene::SelVertex) {
			Vector3 pivot;if(meshPivot(block,pivot))gizmoOrigin=world*pivot;
		}
		if(scene->selMode==Scene::SelVertex) {
			Vector3 pivot;
			if(!vertexProjectionValid || !editSelectionPivot(pivot)) return;
			gizmoOrigin=world*pivot;
		}
		for ( int a = 0; a < 3; ++a ) {
			Vector3 unit;
			unit[a] = 1;
			gizmoDirections[a] = worldAxes?unit:world.rotation * unit;
		}
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadMatrix( viewTrans );
		glGetDoublev( GL_MODELVIEW_MATRIX, gizmoModelview );
		glGetDoublev( GL_PROJECTION_MATRIX, gizmoProjection );
		glGetIntegerv( GL_VIEWPORT, gizmoViewport );
		glPopMatrix();
		if ( !projectGizmo( gizmoOrigin, gizmoScreenOrigin ) )
			return;
		// Convert a fixed screen distance to world units at the pivot depth.
		Vector3 camera = viewTrans * gizmoOrigin;
		if ( perspectiveMode || view==ViewWalk )
			gizmoLength = float( -camera[2] * 2 * tan( cfg.fov / Zoom * pi / 360 ) * 85 / qMax( 1, height() ) );
		else {
			// Unproject at the pivot depth; this also accounts for orthographic zoom.
			GLdouble x, y, z, ax, ay, az, bx, by, bz;
			gluProject( gizmoOrigin[0], gizmoOrigin[1], gizmoOrigin[2], gizmoModelview, gizmoProjection, gizmoViewport, &x, &y, &z );
			gluUnProject( x, y, z, gizmoModelview, gizmoProjection, gizmoViewport, &ax, &ay, &az );
			gluUnProject( x + 85, y, z, gizmoModelview, gizmoProjection, gizmoViewport, &bx, &by, &bz );
			gizmoLength = Vector3( bx - ax, by - ay, bz - az ).length();
		}
		if ( !std::isfinite( gizmoLength ) || gizmoLength <= 0 )
			return;
		for ( int a = 0; a < 3; ++a ) {
			if ( !projectGizmo( gizmoOrigin + gizmoDirections[a] * gizmoLength, gizmoScreenAxes[a] ) )
				return;
			gizmoRings[a].clear();
			gizmoPlanes[a].clear();
			if(gizmoMode==GizmoMove) {
				const float offsets[4][2]={{.22f,.22f},{.46f,.22f},{.46f,.46f},{.22f,.46f}};
				for(auto & offset:offsets) {
					QPointF p;
					auto corner=gizmoOrigin+(gizmoDirections[(a+1)%3]*offset[0]+gizmoDirections[(a+2)%3]*offset[1])*gizmoLength;
					if(!projectGizmo(corner,p)){gizmoPlanes[a].clear();break;}gizmoPlanes[a]<<p;
				}
				double area=0;for(int i=0;i<gizmoPlanes[a].size();++i){auto p=gizmoPlanes[a][i],q=gizmoPlanes[a][(i+1)%4];area+=p.x()*q.y()-q.x()*p.y();}
				if(std::abs(area)<64)gizmoPlanes[a].clear();
			}
			if(gizmoMode!=GizmoRotate)continue;
			for ( int s = 0; s <= ringSegments; ++s ) {
				double t = 2 * pi * s / ringSegments;
				Vector3 p = gizmoOrigin + (gizmoDirections[(a + 1) % 3] * cos(t)
					+ gizmoDirections[(a + 2) % 3] * sin(t)) * gizmoLength;
				QPointF screen;
				if ( !projectGizmo( p, screen ) )
					return;
				gizmoRings[a] << screen;
			}
		}
		gizmoVisible = true;
	}
	if ( !gizmoVisible )
		return;

	glPushAttrib( GL_ALL_ATTRIB_BITS );
	scene->renderer->stopProgram();
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glDisable( GL_LIGHTING );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_FOG );
	glDisable( GL_CULL_FACE );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, width(), height(), 0, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();
	glLineWidth( 3 );
	const float colors[3][3] = { {1, .25f, .2f}, {.25f, 1, .3f}, {.3f, .55f, 1} };
	if(gizmoMode==GizmoMove) {
		glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		for(int a=0;a<3;++a) {
			if(gizmoPlanes[a].size()!=4)continue;
			if(gizmoAxis==a+3)glColor4f(1,.9f,.15f,.65f);else glColor4f(colors[a][0],colors[a][1],colors[a][2],.25f);
			glBegin(GL_QUADS);for(auto p:gizmoPlanes[a])glVertex2d(p.x(),p.y());glEnd();
			if(gizmoAxis==a+3)glColor3f(1,.9f,.15f);else glColor3fv(colors[a]);
			glLineWidth(1.5f);glBegin(GL_LINE_LOOP);for(auto p:gizmoPlanes[a])glVertex2d(p.x(),p.y());glEnd();
		}
		glDisable(GL_BLEND);glLineWidth(3);
	}
	for ( int a = 0; a < 3; ++a ) {
		if ( gizmoAxis == a ) glColor3f( 1, .9f, .15f );
		else glColor3fv( colors[a] );
		if ( gizmoMode == GizmoRotate ) {
			glBegin( GL_LINE_STRIP );
			for ( const auto & p : gizmoRings[a] ) glVertex2d( p.x(), p.y() );
			glEnd();
		} else {
			QPointF p = gizmoScreenAxes[a], o = gizmoScreenOrigin;
			glBegin( GL_LINES );
			glVertex2d( o.x(), o.y() ); glVertex2d( p.x(), p.y() );
			glEnd();
			QPointF d = p - o;
			double len = QLineF(o, p).length();
			if ( len > 15 && gizmoMode == GizmoMove ) {
				d /= len;
				QPointF side( -d.y(), d.x() );
				QPointF b = p - d * 12 + side * 5, c = p - d * 12 - side * 5;
				glBegin( GL_TRIANGLES );
				glVertex2d( p.x(), p.y() ); glVertex2d( b.x(), b.y() ); glVertex2d( c.x(), c.y() );
				glEnd();
			}
		}
	}
	if ( gizmoMode == GizmoScale ) {
		glColor3f( 1, .9f, .15f );
		double x = gizmoScreenOrigin.x(), y = gizmoScreenOrigin.y();
		glBegin( GL_QUADS );
		glVertex2d(x-7,y-7); glVertex2d(x+7,y-7); glVertex2d(x+7,y+7); glVertex2d(x-7,y+7);
		glEnd();
	}
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopAttrib();
}

int GLView::pickGizmo( const QPoint & point ) const
{
	if ( !gizmoVisible ) return -1;
	if ( gizmoMode == GizmoScale )
		return QLineF( point, gizmoScreenOrigin ).length() <= 12 ? 3 : -1;
	if(gizmoMode==GizmoMove) {
		for(int a=0;a<3;++a)if(gizmoPlanes[a].size()==4 && QPolygonF(gizmoPlanes[a]).containsPoint(point,Qt::OddEvenFill))return a+3;
	}
	double best = 9;
	int axis = -1;
	for ( int a = 0; a < 3; ++a ) {
		if ( gizmoMode == GizmoMove ) {
			if ( QLineF( gizmoScreenOrigin, gizmoScreenAxes[a] ).length() < 15 ) continue;
			double d = segmentDistance( point, gizmoScreenOrigin * .8 + gizmoScreenAxes[a] * .2, gizmoScreenAxes[a] );
			if ( d < best ) { best = d; axis = a; }
		} else {
			for ( int s = 1; s < gizmoRings[a].size(); ++s ) {
				double d = segmentDistance( point, gizmoRings[a][s-1], gizmoRings[a][s] );
				if ( d < best ) { best = d; axis = a; }
			}
		}
	}
	return axis;
}

bool GLView::beginGizmo( const QPoint & point )
{
	if ( !model || ((animState & AnimEnabled) && (animState & AnimPlay)) ) return false;
	int a = pickGizmo( point );
	if ( a < 0 ) return false;
	vertexGizmo = scene->selMode == Scene::SelVertex;
	gizmoBlock = vertexGizmo ? editMesh : scene->currentBlock;
	if ( !gizmoBlock.isValid() ) return false;
	auto node = scene->getNode( model, gizmoBlock );
	if ( !node ) return false;
	gizmoParent = node->parentNode() ? node->parentNode()->worldTrans() : Transform();
	if(vertexGizmo) gizmoParent=node->worldTrans();
	if ( std::abs( gizmoParent.scale ) < 1e-8 ) return false;
	gizmoStart = Transform( model, gizmoBlock );
	gizmoLocalPivot=Vector3();
	if(!vertexGizmo && meshCenterPivot)meshPivot(gizmoBlock,gizmoLocalPivot);
	if(vertexGizmo) {
		if(!refreshEditData()) return false;
		gizmoStart=Transform();
		if(!editSelectionPivot(gizmoStart.translation)) return false;
		vertexDragPositions=editData.positions;
		vertexDragSelection=selectedVertices;
	}
	gizmoDragLength = gizmoLength;
	gizmoDragOrigin = gizmoOrigin;
	for ( int i = 0; i < 3; ++i ) gizmoDragAxes[i] = gizmoDirections[i];
	std::copy( gizmoModelview, gizmoModelview + 16, gizmoDragModelview );
	std::copy( gizmoProjection, gizmoProjection + 16, gizmoDragProjection );
	std::copy( gizmoViewport, gizmoViewport + 4, gizmoDragViewport );
	gizmoPress = point;
	gizmoDragDirection = QPointF();
	if ( gizmoMode == GizmoMove ) {
		if(a<3)gizmoDragDirection = gizmoScreenAxes[a] - gizmoScreenOrigin;
		else if(!gizmoPlanePoint(point,a-3,gizmoPlaneStart))return false;
	} else if ( gizmoMode == GizmoRotate ) {
		double best = 1e30;
		for ( int s = 1; s < gizmoRings[a].size(); ++s ) {
			double d = segmentDistance( point, gizmoRings[a][s-1], gizmoRings[a][s] );
			if ( d < best ) {
				best = d;
				gizmoDragDirection = (gizmoRings[a][s] - gizmoRings[a][s-1]) * (ringSegments / (2 * pi));
			}
		}
		if ( QLineF( QPointF(), gizmoDragDirection ).length() < 8 ) return false;
	}
	if(vertexGizmo) buildSnapTargets();
	model->beginUndoTransaction( vertexGizmo ? tr("Transform vertices") : tr( "Transform with gizmo" ) );
	gizmoAxis = a;
	gizmoReleasePending = true;
	gizmoAngle = 0;
	gizmoPlaneRotation = gizmoMode == GizmoRotate && gizmoPlaneAngle( point, gizmoLastAngle );
	kbd.clear();
	mouseMov = mouseRot = Vector3();
	setCursor( Qt::ClosedHandCursor );
	return true;
}

bool GLView::gizmoPlanePoint(const QPoint & point,int normalAxis,Vector3 & result) const
{
	GLdouble ax, ay, az, bx, by, bz;
	if ( !gluUnProject( point.x(), height() - point.y(), 0, gizmoDragModelview,
		gizmoDragProjection, gizmoDragViewport, &ax, &ay, &az )
		|| !gluUnProject( point.x(), height() - point.y(), 1, gizmoDragModelview,
		gizmoDragProjection, gizmoDragViewport, &bx, &by, &bz ) ) return false;
	Vector3 origin(ax, ay, az), ray(bx-ax, by-ay, bz-az);
	ray.normalize();
	Vector3 normal = gizmoDragAxes[normalAxis];
	double denominator = Vector3::dotproduct( ray, normal );
	if ( std::abs(denominator) < .08 ) return false;
	double t = Vector3::dotproduct( gizmoDragOrigin - origin, normal ) / denominator;
	if ( t <= 0 ) return false;
	result=origin+ray*t;
	return std::isfinite(result[0]) && std::isfinite(result[1]) && std::isfinite(result[2]);
}

bool GLView::gizmoPlaneAngle( const QPoint & point, double & angle ) const
{
	Vector3 pointOnPlane;if(!gizmoPlanePoint(point,gizmoAxis,pointOnPlane))return false;
	Vector3 offset = pointOnPlane - gizmoDragOrigin;
	if ( offset.length() < gizmoDragLength * .05 ) return false;
	angle = std::atan2( Vector3::dotproduct(offset, gizmoDragAxes[(gizmoAxis+2)%3]),
		Vector3::dotproduct(offset, gizmoDragAxes[(gizmoAxis+1)%3]) );
	return std::isfinite(angle);
}

void GLView::dragGizmo( const QPoint & point )
{
	if ( !model || !gizmoBlock.isValid() ) { finishGizmo( true ); return; }
	Transform result = gizmoStart;
	QPointF delta = point - gizmoPress;
	double length2 = QPointF::dotProduct( gizmoDragDirection, gizmoDragDirection );
	double amount = length2 > 0 ? QPointF::dotProduct( delta, gizmoDragDirection ) / length2 : 0;
	if ( gizmoMode == GizmoMove ) {
		Vector3 worldDelta;
		if(gizmoAxis<3)worldDelta=gizmoDragAxes[gizmoAxis]*(amount*gizmoDragLength);
		else {
			Vector3 position;if(!gizmoPlanePoint(point,gizmoAxis-3,position))return;
			worldDelta=position-gizmoPlaneStart;
			const auto normal=gizmoDragAxes[gizmoAxis-3];worldDelta-=normal*Vector3::dotproduct(worldDelta,normal);
		}
		snapTarget=-1;
		if(vertexGizmo && vertexSnap) {
			double best=14;
			for(int i=0;i<snapTargets.size();++i) {
				QPointF p; if(!projectGizmo(snapTargets[i],p)) continue;
				double distance=QLineF(point,p).length();
				if(distance<best){best=distance;snapTarget=i;}
			}
			if(snapTarget>=0) {
				auto targetDelta=snapTargets[snapTarget]-snapAnchor;
				if(gizmoAxis<3) {
					auto direction=gizmoDragAxes[gizmoAxis];
					worldDelta=direction*Vector3::dotproduct(targetDelta,direction);
				} else {
					auto normal=gizmoDragAxes[gizmoAxis-3];
					worldDelta=targetDelta-normal*Vector3::dotproduct(targetDelta,normal);
				}
			}
		}
		result.translation += gizmoParent.rotation.inverted() * worldDelta / gizmoParent.scale;
	} else if ( gizmoMode == GizmoRotate ) {
		if ( gizmoPlaneRotation ) {
			double angle;
			if ( !gizmoPlaneAngle( point, angle ) ) return;
			double step = angle - gizmoLastAngle;
			if ( step > pi ) step -= 2*pi;
			if ( step < -pi ) step += 2*pi;
			gizmoAngle += step;
			gizmoLastAngle = angle;
			amount = gizmoAngle;
		}
		Vector3 angles;
		angles[gizmoAxis] = amount;
		result.rotation = gizmoStart.rotation * Matrix::euler( angles[0], angles[1], angles[2] );
		if(worldAxes)result.rotation=gizmoParent.rotation.inverted()*Matrix::euler(angles[0],angles[1],angles[2])*gizmoParent.rotation*gizmoStart.rotation;
	} else if ( gizmoMode == GizmoScale ) {
		result.scale *= std::exp( qBound( -9.0, (delta.x() - delta.y()) / 120.0, 9.0 ) );
	}
	if(!vertexGizmo && meshCenterPivot && gizmoMode!=GizmoMove) {
		// Keep the temporary pivot fixed in parent space as the object rotates
		// or scales. Geometry and the stored local origin are not rebased.
		result.translation=gizmoStart.translation+gizmoStart.rotation*gizmoLocalPivot*gizmoStart.scale
			-result.rotation*gizmoLocalPivot*result.scale;
	}
	if ( !std::isfinite(result.scale) ) return;
	for ( int a = 0; a < 3; ++a ) {
		if ( !std::isfinite(result.translation[a]) ) return;
		for ( int b = 0; b < 3; ++b )
			if ( !std::isfinite(result.rotation(a,b)) ) return;
	}
	if(vertexGizmo) writeVertexTransform(result);
	else result.writeBack( model, gizmoBlock );
	update();
}

void GLView::finishGizmo( bool cancel )
{
	if ( gizmoAxis < 0 ) return;
	gizmoAxis = -1;
	if ( model ) {
		if ( gizmoBlock.isValid() ) {
			if(vertexGizmo) {
				if(cancel) editData.write(model,vertexDragPositions,vertexDragSelection);
				else {
					VertexEditData current;
					if(current.read(model,gizmoBlock) && current.positions!=vertexDragPositions) current.updateBounds(model);
				}
			} else if(cancel) gizmoStart.writeBack( model, gizmoBlock );
		}
		model->endUndoTransaction();
	}
	vertexGizmo=false;
	snapTargets.clear(); snapTarget=-1;
	gizmoBlock = QModelIndex();
	gizmoVisible = false;
	lastPos = gizmoPress;
	mouseMov = mouseRot = Vector3();
	unsetCursor();
	update();
}
