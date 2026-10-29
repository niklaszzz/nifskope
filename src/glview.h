/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef GLVIEW
#define GLVIEW

#include "gl/glscene.h"
#include "model/vertexedit.h"
#include "physx/physxcollision.h"

#include <QGLWidget> // Inherited
#include <QGraphicsView>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QPointer>
#include <memory>

#include <math.h>


//! @file glview.h GLView, GLGraphicsView

class NifSkope;
class NifModel;
class GLGraphicsView;

class QGLFormat;
class QOpenGLContext;
class QOpenGLFunctions;
class QOpenGLFramebufferObject;
class QTimer;


//! The main [Viewport](@ref viewport_details) class
class GLView final : public QGLWidget
{
	Q_OBJECT

	friend class NifSkope;
	friend class GLGraphicsView;
	friend class GLViewGizmoTest;
	friend class VertexEditorTest;
	friend class PhysXCollisionTest;
	friend class WorkspaceTest;
	friend class ViewportResizeTest;
	friend class LightingPreviewTest;

private:
	GLView( const QGLFormat & format, QWidget * parent, const QGLWidget * shareWidget = 0 );
	~GLView();

public:
	//! Static instance
	static GLView * create( NifSkope * );

	QOpenGLContext * glContext;
	QOpenGLFunctions * glFuncs;

	float brightness = 1.0;
	float ambient = 0.375;
	float declination = 0;
	float planarAngle = 0;
	bool frontalLight = true;

	enum AnimationStates
	{
		AnimDisabled = 0x0,
		AnimEnabled = 0x1,
		AnimPlay = 0x2,
		AnimLoop = 0x4,
		AnimSwitch = 0x8
	};
	Q_DECLARE_FLAGS( AnimationState, AnimationStates );

	AnimationState animState;

	enum ViewState
	{
		ViewDefault,
		ViewTop,
		ViewBottom,
		ViewLeft,
		ViewRight,
		ViewFront,
		ViewBack,
		ViewWalk,
		ViewUser
	};

	enum DebugMode
	{
		DbgNone = 0,
		DbgColorPicker = 1,
		DbgBounds = 2
	};

	enum UpAxis
	{
		XAxis = 0,
		YAxis = 1,
		ZAxis = 2
	};

	void setNif( NifModel * );

	Scene * getScene();
	void updateShaders();
	void updateViewpoint();

	void flush();

	void center();
	void move( float, float, float );
	void rotate( float, float, float );
	void zoom( float );

	void setCenter();
	void setDistance( float );
	void setPosition( float, float, float );
	void setPosition( const Vector3 & );
	void setProjection( bool );
	void setRotation( float, float, float );
	void setZoom( float );

	void setOrientation( GLView::ViewState, bool recenter = true );
	void flipOrientation();

	void setDebugMode( DebugMode );

	enum GizmoMode { GizmoSelect, GizmoMove, GizmoRotate, GizmoScale };
	void setGizmoMode( GizmoMode mode );
	void setEditSelectionMode( Scene::SelMode mode );
	void setFaceSelectionMode();
	void deleteSelectedFaces();
	void deleteSelectedVertices();
	void cycleSelectionMode();
	void selectLinkedEditElements();
	void setEditXRay(bool enabled) { if(editXRay==enabled)return;editXRay=enabled;vertexProjectionValid=false;emit editXRayChanged(enabled);update(); }
	void setVertexSnap( bool enabled ) { vertexSnap=enabled; update(); }
	void setMeshCenterPivot(bool enabled);
	void setWorldAxes(bool enabled) { finishGizmo(true);worldAxes=enabled;gizmoVisible=false;update(); }
	void setIsolateSelection(bool enabled);
	void setRoomOverlay(bool enabled){showRoomOverlay=enabled;update();}
	void setPortalPreview(const QVector<QPair<Vector3,Vector3>> & edges){portalPreview=edges;update();}
	void setPortalHighlight(int id){highlightedPortal=id;update();}
	bool applyNumericTransform(GizmoMode mode,const Vector3 & amount,bool worldSpace,QString * reason=nullptr);
	void showNumericTransform();
	void setUnshaded(bool enabled);
	void setDetailBrightness(int percent);
	void setReferenceModels(const QList<NifModel *> & models);
	void setReferencesVisible(bool visible) { referencesVisible=visible;update(); }
	void setReferenceOpacity(int percent) { referenceOpacity=qBound(0,percent,100)/100.f;update(); }

	QColor clearColor() const;


	QModelIndex indexAt( const QPoint & p, int cycle = 0 );

	// UI

	QSize minimumSizeHint() const override final { return { 50, 50 }; }
	QSize sizeHint() const override final { return { 400, 400 }; }

public slots:
	void setCurrentIndex( const QModelIndex & );
	void setSceneTime( float );
	void setSceneSequence( const QString & );
	void saveUserView();
	void loadUserView();
	void setBrightness( int );
	void setAmbient( int );
	void setDeclination( int );
	void setPlanarAngle( int );
	void setFrontalLight( bool );
	void updateScene();
	void updateAnimationState( bool checked );
	void setVisMode( Scene::VisMode, bool checked = true );
	void updateSettings();

signals:
	void selectionModeCycled(int mode);
	void gizmoModeChanged(int mode);
	void viewportContextMenuRequested(const QPoint & point);
	void currentBlockChanged(const QModelIndex & block);
	void editSelectionChanged();
	void editXRayChanged(bool enabled);
	void isolationChanged(bool enabled);
	void clicked( const QModelIndex & );
	void paintUpdate();
	void sceneTimeChanged( float t, float mn, float mx );
	void viewpointChanged();

	void sequenceStopped();
	void sequenceChanged( const QString & );
	void sequencesUpdated();
	void sequencesDisabled( bool );

protected:
	//! Sets up the OpenGL rendering context, defines display lists, etc.
	void initializeGL() override final;
	//! Sets up the OpenGL viewport, projection, etc.
	void resizeGL( int width, int height ) override final;
	void resizeEvent( QResizeEvent * event ) override final;
#ifdef USE_GL_QPAINTER
	void paintEvent( QPaintEvent * ) override final;
#else
	//! Renders the OpenGL scene.
	void paintGL() override final;
#endif
	void glProjection( int x = -1, int y = -1 );

	// QWidget Event Handlers
	bool event(QEvent * event) override;

	void dragEnterEvent( QDragEnterEvent * ) override final;
	void dragLeaveEvent( QDragLeaveEvent * ) override final;
	void dragMoveEvent( QDragMoveEvent * ) override final;
	void dropEvent( QDropEvent * ) override final;
	void focusOutEvent( QFocusEvent * ) override final;
	void keyPressEvent( QKeyEvent * ) override final;
	void keyReleaseEvent( QKeyEvent * ) override final;
	void mouseDoubleClickEvent( QMouseEvent * ) override final;
	void mouseMoveEvent( QMouseEvent * ) override final;
	void mousePressEvent( QMouseEvent * ) override final;
	void mouseReleaseEvent( QMouseEvent * ) override final;
	void wheelEvent( QWheelEvent * ) override final;

protected slots:
	void saveImage();

private:
	struct ReferenceLayer;
	QVector<std::shared_ptr<ReferenceLayer>> referenceLayers;
	std::unique_ptr<QOpenGLFramebufferObject> referenceBuffer;
	bool referencesVisible = true;
	float referenceOpacity = .3f;
	void clearReferences();
	void drawReferences();
	NifModel * model;
	QVector<QMetaObject::Connection> activeModelConnections;
	Scene * scene;

	ViewState view;
	DebugMode debugMode;
	bool perspectiveMode;

	class TexCache * textures;

	float time;
	QTime lastTime;
	QTimer * timer;
	bool flyNavigating=false;
	QElapsedTimer flyClickTimer;
	QPoint flyPressPoint;
	bool flyGestureUsed=false;
	float flySpeedMultiplier=1.0f;
	void stopFlyNavigation();

	float Dist;
	Vector3 Pos;
	Vector3 Rot;
	GLdouble Zoom;
	GLdouble axis;
	GLdouble grid;
	Transform viewTrans;

	GizmoMode gizmoMode = GizmoSelect;
	int gizmoAxis = -1;
	bool gizmoVisible = false;
	bool gizmoReleasePending = false;
	QPersistentModelIndex gizmoBlock;
	Transform gizmoStart, gizmoParent;
	Vector3 gizmoOrigin, gizmoDirections[3];
	float gizmoLength = 1;
	float gizmoDragLength = 1;
	Vector3 gizmoDragOrigin, gizmoDragAxes[3];
	bool meshCenterPivot = false;
	bool worldAxes=false;
	int isolateRestoreBlock=-1;
	bool showRoomOverlay=false;
	int highlightedPortal=-1;
	QVector<QPair<Vector3,Vector3>> portalPreview;
	int selectionRestoreBlock=-1;
	Vector3 gizmoLocalPivot;
	bool meshPivot(const QModelIndex & block,Vector3 & pivot) const;
	GLdouble gizmoDragModelview[16], gizmoDragProjection[16];
	GLint gizmoDragViewport[4];
	double gizmoAngle = 0, gizmoLastAngle = 0;
	bool gizmoPlaneRotation = false;
	bool gizmoPlaneAngle( const QPoint & point, double & angle ) const;
	QPointF gizmoScreenOrigin, gizmoScreenAxes[3];
	QVector<QPointF> gizmoRings[3];
	QVector<QPointF> gizmoPlanes[3]; // IDs 3..5: YZ, XZ, XY
	Vector3 gizmoPlaneStart;
	bool gizmoPlanePoint(const QPoint & point,int normal,Vector3 & result) const;
	QPointF gizmoDragDirection;
	QPoint gizmoPress;
	GLdouble gizmoModelview[16], gizmoProjection[16];
	GLint gizmoViewport[4];
	void drawGizmo();
	bool projectGizmo( const Vector3 & point, QPointF & screen ) const;
	int pickGizmo( const QPoint & point ) const;
	bool beginGizmo( const QPoint & point );
	void dragGizmo( const QPoint & point );
	void finishGizmo( bool cancel = false );
	QPersistentModelIndex editMesh;
	VertexEditData editData;
	FaceEditData faceData;
	bool faceSelection=false;
	bool editXRay=true;
	QVector<float> editDepth;
	bool editPointVisible(const Vector3 & world) const;
	bool projectEditPoint(const Vector3 & world,QPointF & screen,double & depth) const;
	QSet<int> selectedFaces;
	QSet<int> selectedVertices, vertexDragSelection;
	QVector<Vector3> vertexDragPositions;
	bool vertexGizmo = false;
	bool vertexSnap = false;
	bool vertexSelecting = false;
	bool vertexProjectionValid = false;
	QPoint vertexSelectionStart, vertexSelectionEnd;
	Qt::KeyboardModifiers vertexSelectionModifiers;
	QString vertexEditReason;
	int editRestoreBlock = -1;
	QVector<Vector3> snapTargets;
	int snapTarget = -1;
	Vector3 snapAnchor;
	QVector<QPair<QPersistentModelIndex,QPointF>> lightMarkers;
	QVector<QPair<Vector3,Vector3>> lightIndicatorLines(const QModelIndex & block, const Transform & world, float length) const;
	double lightAttenuationRadius(const QModelIndex & block, double fraction) const;
	QVector<QPair<Vector3,Vector3>> lightFalloffLines(const QModelIndex & block, const Transform & world, double fraction) const;
	QVector<PhysXCollision::Mesh> physxMeshes;
	bool physxDirty = true;
	QModelIndex collisionAt(const QPoint & point) const;
	void drawEditorOverlay();
	void selectEditVertices( const QPoint & end );
	void buildSnapTargets();
	bool editSelectionPivot( Vector3 & pivot ) const;
	bool refreshEditData();
	bool writeVertexTransform( const Transform & transform );
	QModelIndex lightAt( const QPoint & point ) const;

	GLdouble aspect;
	
	QHash<int, bool> kbd;
	QPoint lastPos;
	QPoint pressPos;
	Vector3 mouseMov;
	Vector3 mouseRot;
	int cycleSelect;

	QPersistentModelIndex iDragTarget;
	QString fnDragTex, fnDragTexOrg;

	bool doCompile;
	bool doCenter;

	QTimer * lightVisTimer;
	int lightVisTimeout;

	struct Settings
	{
		QColor background;
		float fov = 45.0;
		float moveSpd = 350;
		float rotSpd = 45;

		UpAxis upAxis = ZAxis;
	} cfg;

private slots:
	void advanceGears();

	void dataChanged( const QModelIndex &, const QModelIndex & );
	void modelChanged();
	void modelLinked();
	void modelDestroyed();
};

Q_DECLARE_OPERATORS_FOR_FLAGS( GLView::AnimationState )


class GLGraphicsView : public QGraphicsView
{
	Q_OBJECT

public:
	GLGraphicsView( QWidget * parent );
	~GLGraphicsView();

protected slots:
	void setupViewport( QWidget * viewport ) override;

protected:
	bool eventFilter( QObject * o, QEvent * e ) override final;
	bool event(QEvent * event) override;
	void dragEnterEvent( QDragEnterEvent * ) override final;
	void dragLeaveEvent( QDragLeaveEvent * ) override final;
	void dragMoveEvent( QDragMoveEvent * ) override final;
	void dropEvent( QDropEvent * ) override final;
	void focusOutEvent( QFocusEvent * ) override final;
	void keyPressEvent( QKeyEvent * ) override final;
	void keyReleaseEvent( QKeyEvent * ) override final;
	void mouseDoubleClickEvent( QMouseEvent * ) override final;
	void mouseMoveEvent( QMouseEvent * ) override final;
	void mousePressEvent( QMouseEvent * ) override final;
	void mouseReleaseEvent( QMouseEvent * ) override final;
	void wheelEvent( QWheelEvent * ) override final;

	//void paintEvent( QPaintEvent * ) override final;
	void drawBackground( QPainter * painter, const QRectF & rect ) override final;
	void drawForeground( QPainter * painter, const QRectF & rect ) override final;

private:

	QStringList draggedNifs;

};

#endif
