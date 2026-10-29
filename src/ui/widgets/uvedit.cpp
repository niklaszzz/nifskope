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

#include "uvedit.h"

#include "message.h"
#include "nifskope.h"
#include "gl/gltex.h"
#include "gl/gltools.h"
#include "model/nifmodel.h"
#include "model/uveditdata.h"
#include "model/vertexedit.h"
#include <QMessageBox>
#include <QApplication>
#include <QFileDialog>
#include <QFocusEvent>
#include "ui/settingsdialog.h"

#include "lib/nvtristripwrapper.h"

#include <QUndoStack> // QUndoCommand Inherited
#include <QActionGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QLineF>
#include <QTimer>
#include <cmath>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QSettings>
#include <QComboBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QFileInfo>

// TODO: Determine the necessity of this
// Appears to be used solely for gluErrorString
// There may be some Qt alternative
#ifdef __APPLE__
	#include <OpenGL/glu.h>
#else
	#include <GL/glu.h>
#endif

#define BASESIZE 512.0
#define GRIDSIZE 16.0
#define GRIDSEGS 4
#define ZOOMUNIT -64.0
#define MINZOOM 0.1
#define MAXZOOM 20.0
#define MAXSCALE 10.0
#define MAXTRANS 10.0


UVWidget * UVWidget::createEditor( NifModel * nif, const QModelIndex & idx, QWidget * owner )
{
	if(!owner)for(auto widget:QApplication::topLevelWidgets())if(auto main=qobject_cast<NifSkope *>(widget))if(main->findChildren<NifModel *>().contains(nif)){owner=main;break;}
	auto main=qobject_cast<NifSkope *>(owner);
	if(owner)for(auto editor:owner->findChildren<UVWidget *>())if(editor->document()==nif && editor->followMesh(nif,idx)){
		if(main)main->synchronizeUVEditor(editor);editor->window()->show();editor->window()->raise();editor->window()->activateWindow();return editor;
	}
	auto shell=new QWidget(owner,Qt::Tool);shell->setObjectName("uvEditorWindow");shell->setWindowTitle(tr("UV Editor - MicroSkope 1.0"));shell->setAttribute(Qt::WA_DeleteOnClose);shell->resize(760,760);
	auto layout=new QVBoxLayout(shell);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
	UVWidget * uvw = new UVWidget(shell);
	uvw->mapControls->setParent(shell);uvw->mapControls->setAutoFillBackground(true);layout->addWidget(uvw->mapControls);layout->addWidget(uvw,1);
	connect(uvw,&QObject::destroyed,shell,&QObject::deleteLater);

	if ( !uvw->setNifData( nif, idx ) ) {
		QString reason=tr("Select a mesh with UV coordinates first.");
		if(nif && nif->inherits(idx,"NiMesh")){UVEditData data;data.read(nif,idx,0,&reason);}
		QMessageBox::warning(nullptr,tr("UV Editor"),reason);
		delete shell;
		return nullptr;
	}

	if(main)main->synchronizeUVEditor(uvw);
	shell->show();
	uvw->frameUVs();
	shell->raise();shell->activateWindow();
	return uvw;
}

static GLshort vertArray[4][2] = {
	{ 0, 0 }, { 1, 0 },
	{ 1, 1 }, { 0, 1 }
};

static GLshort texArray[4][2] = {
	{ 0, 0 }, { 1, 0 },
	{ 1, 1 }, { 0, 1 }
};

static GLdouble glUnit  = ( 1.0 / BASESIZE );
static GLdouble glGridD = GRIDSIZE * glUnit;

QStringList UVWidget::texnames = {
	"Base Texture", "Dark Texture", "Detail Texture",
	"Gloss Texture", "Glow Texture", "Bump Map Texture",
	"Normal Texture", "Parallax Texture",
	"Decal 0 Texture", "Decal 1 Texture", "Decal 2 Texture",
	"Decal 3 Texture"
};


UVWidget::UVWidget( QWidget * parent )
	: QGLWidget( QGLFormat( QGL::SampleBuffers ), parent, 0, parent?Qt::Widget:Qt::Tool ), undoStack( new QUndoStack( this ) )
{
	setWindowTitle( tr( "UV Editor" ) );
	resize(700,700);
	setFocusPolicy( Qt::StrongFocus );

	textures = new TexCache( this );
	mapControls=new QWidget(this);mapControls->setObjectName("uvMapControls");
	auto mapLayout=new QGridLayout(mapControls);mapLayout->setContentsMargins(8,6,8,6);
	textureChoice=new QComboBox(mapControls);textureChoice->setObjectName("uvTextureChoice");textureChoice->setMinimumContentsLength(18);textureChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	uvChoice=new QComboBox(mapControls);uvChoice->setObjectName("uvCoordinateChoice");
	mapStatus=new QLabel(mapControls);mapStatus->setObjectName("uvMapStatus");
	mapLayout->addWidget(new QLabel(tr("Texture"),mapControls),0,0);mapLayout->addWidget(textureChoice,0,1,1,3);mapLayout->addWidget(uvChoice,1,0);mapLayout->addWidget(mapStatus,1,2,1,2);mapLayout->setColumnStretch(3,1);
	auto unwrap=new QPushButton(tr("Cube Unwrap"),mapControls);unwrap->setObjectName("uvCubeUnwrap");unwrap->setToolTip(tr("Project selected faces, or the whole mesh if no complete faces are selected, into six padded cube islands on the active UV channel. Adds seam vertices while preserving other vertex attributes."));mapLayout->addWidget(unwrap,1,1);connect(unwrap,&QPushButton::clicked,this,&UVWidget::cubeUnwrap);
	connect(textureChoice,QOverload<int>::of(&QComboBox::activated),this,&UVWidget::chooseTexture);
	connect(uvChoice,QOverload<int>::of(&QComboBox::activated),this,[this](int index){changeCoordSet(uvChoice->itemData(index).toInt());});

	zoom = 1.2;

	pos = QPoint( 0, 0 );

	mousePos = QPoint( -1000, -1000 );

	setCursor( QCursor( Qt::CrossCursor ) );
	setMouseTracking( true );

	setContextMenuPolicy( Qt::ActionsContextMenu );

	QAction * aUndo = documentUndo = new QAction(tr("Undo"),this);
	QAction * aRedo = documentRedo = new QAction(tr("Redo"),this);
	connect(aUndo,&QAction::triggered,this,[this](){if(nif && !uvDragging)nif->undo();});
	connect(aRedo,&QAction::triggered,this,[this](){if(nif && !uvDragging)nif->redo();});

	aUndo->setShortcut( QKeySequence::Undo );
	aRedo->setShortcut( QKeySequence::Redo );

	addAction( aUndo );
	addAction( aRedo );
	auto frame=new QAction(tr("Frame UVs"),this);frame->setShortcut(QKeySequence("F"));addAction(frame);
	connect(frame,&QAction::triggered,this,&UVWidget::frameUVs);
	auto background=new QAction(tr("Load Background Texture…"),this);addAction(background);
	connect(background,&QAction::triggered,this,[this](){
		QString path=QFileDialog::getOpenFileName(this,tr("UV Background Texture"),nif?nif->getFolder():QString(),tr("Textures (*.dds *.tga *.bmp);;All files (*)"));
		if(!path.isEmpty()){texfile=path;texsource=QModelIndex();updateGL();}
	});

	QAction * aSep = new QAction( this );
	aSep->setSeparator( true );
	addAction( aSep );

	QAction * aSelectAll = new QAction( tr( "Select &All" ), this );
	aSelectAll->setShortcut( QKeySequence::SelectAll );
	connect( aSelectAll, &QAction::triggered, this, &UVWidget::selectAll );
	addAction( aSelectAll );

	QAction * aSelectNone = new QAction( tr( "Select &None" ), this );
	connect( aSelectNone, &QAction::triggered, this, &UVWidget::selectNone );
	addAction( aSelectNone );
	auto cubeAction=new QAction(tr("Cube Unwrap"),this);cubeAction->setObjectName("aUVCubeUnwrap");connect(cubeAction,&QAction::triggered,this,&UVWidget::cubeUnwrap);addAction(cubeAction);
	auto invertSelection=new QAction(tr("Invert Selection"),this);invertSelection->setObjectName("uvInvertSelection");invertSelection->setShortcut(QKeySequence("Ctrl+I"));invertSelection->setAutoRepeat(false);
	connect(invertSelection,&QAction::triggered,this,&UVWidget::invertSelection);addAction(invertSelection);

	QAction * aSelectFaces = new QAction( tr( "Select &Faces" ), this );
	connect( aSelectFaces, &QAction::triggered, this, &UVWidget::selectFaces );
	addAction( aSelectFaces );

	QAction * aSelectConnected = new QAction( tr( "Select &Connected" ), this );
	aSelectConnected->setShortcut(QKeySequence("Ctrl+L"));
	connect( aSelectConnected, &QAction::triggered, this, &UVWidget::selectConnected );
	addAction( aSelectConnected );

	QAction * aScaleSelection = new QAction( tr( "&Scale and Translate Selected" ), this );
	aScaleSelection->setShortcut( QKeySequence( "Alt+S" ) );
	connect( aScaleSelection, &QAction::triggered, this, &UVWidget::scaleSelection );
	addAction( aScaleSelection );

	QAction * aRotateSelection = new QAction( tr( "&Rotate Selected" ), this );
	aRotateSelection->setShortcut( QKeySequence( "Alt+R" ) );
	connect( aRotateSelection, &QAction::triggered, this, &UVWidget::rotateSelection );
	addAction( aRotateSelection );

	aSep = new QAction( this );
	aSep->setSeparator( true );
	addAction( aSep );

	aTextureBlend = new QAction( tr( "Texture Alpha Blending" ), this );
	aTextureBlend->setCheckable( true );
	aTextureBlend->setChecked( true );
	connect( aTextureBlend, &QAction::toggled, this, &UVWidget::updateGL );
	addAction( aTextureBlend );

	updateSettings();

	connect( NifSkope::getOptions(), &SettingsDialog::saveSettings, this, &UVWidget::updateSettings );
	connect( NifSkope::getOptions(), &SettingsDialog::update3D, this, &UVWidget::updateGL );
}

UVWidget::~UVWidget()
{
	finishCageDrag(true);
	delete textures;
	nif = nullptr;
}

void UVWidget::updateSettings()
{
	QSettings settings;
	settings.beginGroup( "Settings/Render/Colors/" );

	cfg.background = settings.value( "Background",QColor(32,32,32) ).value<QColor>();
	cfg.highlight = settings.value( "Highlight",QColor(255,180,40) ).value<QColor>();
	cfg.wireframe = settings.value( "Wireframe",QColor(235,235,235) ).value<QColor>();

	settings.endGroup();
}

void UVWidget::initializeGL()
{
	glMatrixMode( GL_MODELVIEW );

	initializeTextureUnits( context()->contextHandle() );

	glShadeModel( GL_SMOOTH );
	//glShadeModel( GL_LINE_SMOOTH );

	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glEnable( GL_BLEND );

	glDepthFunc( GL_LEQUAL );
	glEnable( GL_DEPTH_TEST );

	glEnable( GL_MULTISAMPLE );
	glDisable( GL_LIGHTING );

	qglClearColor( cfg.background );

	if ( !texfile.isEmpty() )
		bindTexture( texfile );
	else
		bindTexture( texsource );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 2, GL_SHORT, 0, vertArray );

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_SHORT, 0, texArray );

	// check for errors
	GLenum err;

	while ( ( err = glGetError() ) != GL_NO_ERROR ) {
		qDebug() << "GL ERROR (init) : " << (const char *)gluErrorString( err );
	}
}

void UVWidget::resizeGL( int width, int height )
{
	if(mapControls && mapControls->parentWidget()==this)mapControls->setGeometry(8,8,qMax(100,width-16),mapControls->sizeHint().height());
	updateViewRect( width, height );
}

void UVWidget::paintGL()
{
	glPushAttrib( GL_ALL_ATTRIB_BITS );

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();

	setupViewport( width(), height() );

	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();

	qglClearColor( cfg.background );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );

	// draw texture

	glPushMatrix();
	glLoadIdentity();

	glEnable( GL_TEXTURE_2D );

	if ( aTextureBlend->isChecked() )
		glEnable( GL_BLEND );
	else
		glDisable( GL_BLEND );

	if ( !texfile.isEmpty() )
		bindTexture( texfile );
	else
		bindTexture( texsource );

	glTranslatef( -0.5f, -0.5f, 0.0f );

	glTranslatef( -1.0f, -1.0f, 0.0f );

	for ( int i = 0; i < 3; i++ ) {
		for ( int j = 0; j < 3; j++ ) {
			if ( i == 1 && j == 1 ) {
				glColor4f( 0.75f, 0.75f, 0.75f, 1.0f );
			} else {
				glColor4f( 0.5f, 0.5f, 0.5f, 1.0f );
			}

			glDrawArrays( GL_QUADS, 0, 4 );

			glTranslatef( 1.0f, 0.0f, 0.0f );
		}

		glTranslatef( -3.0f, 1.0f, 0.0f );
	}

	glTranslatef( 1.0f, -2.0f, 0.0f );

	glDisable( GL_TEXTURE_2D );

	glPopMatrix();

	// draw grid
	glPushMatrix();
	glLoadIdentity();

	glEnable( GL_BLEND );

	glLineWidth( 0.8f );
	glBegin( GL_LINES );
	int glGridMinX = qRound( qMin( glViewRect[0], glViewRect[1] ) / glGridD );
	int glGridMaxX = qRound( qMax( glViewRect[0], glViewRect[1] ) / glGridD );
	int glGridMinY = qRound( qMin( glViewRect[2], glViewRect[3] ) / glGridD );
	int glGridMaxY = qRound( qMax( glViewRect[2], glViewRect[3] ) / glGridD );

	for ( int i = glGridMinX; i < glGridMaxX; i++ ) {
		GLdouble glGridPos = glGridD * i;

		if ( ( i % ( GRIDSEGS * GRIDSEGS ) ) == 0 ) {
			glLineWidth( 1.4f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.4f );
		} else if ( zoom > ( GRIDSEGS * GRIDSEGS / 2.0 ) ) {
			continue;
		} else if ( ( i % GRIDSEGS ) == 0 ) {
			glLineWidth( 1.2f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.2f );
		} else if ( zoom > ( GRIDSEGS / 2.0 ) ) {
			continue;
		} else {
			glLineWidth( 0.8f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.1f );
		}

		glVertex2d( glGridPos, glViewRect[2] );
		glVertex2d( glGridPos, glViewRect[3] );
	}

	for ( int i = glGridMinY; i < glGridMaxY; i++ ) {
		GLdouble glGridPos = glGridD * i;

		if ( ( i % ( GRIDSEGS * GRIDSEGS ) ) == 0 ) {
			glLineWidth( 1.4f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.4f );
		} else if ( zoom > ( GRIDSEGS * GRIDSEGS / 2.0 ) ) {
			continue;
		} else if ( ( i % GRIDSEGS ) == 0 ) {
			glLineWidth( 1.2f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.2f );
		} else if ( zoom > ( GRIDSEGS / 2.0 ) ) {
			continue;
		} else {
			glLineWidth( 0.8f );
			glColor4f( 1.0f, 1.0f, 1.0f, 0.1f );
		}

		glVertex2d( glViewRect[0], glGridPos );
		glVertex2d( glViewRect[1], glGridPos );
	}

	glEnd();

	glPopMatrix();

	drawTexCoords();
	drawSelectionCage();
	const QStringList hints={tr("Box-select empty space | Drag cage to move | Square handles scale"),
		tr("Round handle rotates | Shift: uniform / 15 deg | Esc: cancel | MMB pan"),
		tr("Shift adds | Ctrl toggles | Wheel zoom | F frame | Ctrl+Z undo")};
	for(int line=0;line<hints.size();++line){
		qglColor(QColor(20,20,20));renderText(13,21+line*18,hints[line],QFont("Sans Serif",9));
		qglColor(QColor(255,225,150));renderText(12,20+line*18,hints[line],QFont("Sans Serif",9));
	}
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );

	if ( !selectRect.isNull() ) {
		glLoadIdentity();
		glColor( Color4( cfg.highlight ) );
		glBegin( GL_LINE_LOOP );
		glVertex( mapToContents( selectRect.topLeft() ) );
		glVertex( mapToContents( selectRect.topRight() ) );
		glVertex( mapToContents( selectRect.bottomRight() ) );
		glVertex( mapToContents( selectRect.bottomLeft() ) );
		glEnd();
	}

	if ( !selectPoly.isEmpty() ) {
		glLoadIdentity();
		glColor( Color4( cfg.highlight ) );
		glBegin( GL_LINE_LOOP );
		for ( const QPoint& p : selectPoly ) {
			glVertex( mapToContents( p ) );
		}
		glEnd();
	}

	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();

	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	glPopAttrib();
}

void UVWidget::drawTexCoords()
{
	glMatrixMode( GL_MODELVIEW );

	glPushMatrix();
	glLoadIdentity();

	glScalef( 1.0f, 1.0f, 1.0f );
	glTranslatef( -0.5f, -0.5f, 0.0f );

	Color4 nlColor( cfg.wireframe );
	nlColor.setAlpha( 0.5f );
	Color4 hlColor( cfg.highlight );
	hlColor.setAlpha( 0.5f );

	glLineWidth( 1.0f );
	glPointSize( 3.5f );

	glEnable( GL_BLEND );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	if(viewportFaceSelection){
		glColor4f(hlColor[0],hlColor[1],hlColor[2],.18f);glBegin(GL_TRIANGLES);
		for(const auto & face:faces)if(viewportFaces.contains(face.index))for(int a=0;a<3;++a)glVertex(Vector3(texcoords[face.tc[a]],.5f));
		glEnd();
	}

	float z;

	// draw triangle edges
	for ( int i = 0; i < faces.size(); i++ ) {
		glBegin( GL_LINE_LOOP );

		for ( int j = 0; j < 3; j++ ) {
			int x = faces[i].tc[j];

			if ( selection.contains( x ) ) {
				glColor( Color3( hlColor ) );
				z = 1.0f;
			} else {
				glColor( Color3( nlColor ) );
				z = 0.0f;
			}

			glVertex( Vector3( texcoords[x], z ) );
		}

		glEnd();
	}

	// draw points

	glBegin( GL_POINTS );

	for ( int i = 0; i < texcoords.size(); i++ ) {
		if ( selection.contains( i ) ) {
			glColor( Color3( hlColor ) );
			z = 1.0f;
		} else {
			glColor( Color3( nlColor ) );
			z = 0.0f;
		}

		glVertex( Vector3( texcoords[i], z ) );
	}

	glEnd();

	glPopMatrix();
}

void UVWidget::setupViewport( int width, int height )
{
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	glViewport( 0, 0, width, height );

	glOrtho( glViewRect[0], glViewRect[1], glViewRect[2], glViewRect[3], -10.0, +10.0 );
}

void UVWidget::updateViewRect( int width, int height )
{
	GLdouble glOffX = glUnit * zoom * 0.5 * width;
	GLdouble glOffY = glUnit * zoom * 0.5 * height;
	GLdouble glPosX = glUnit * pos.x();
	GLdouble glPosY = glUnit * pos.y();

	glViewRect[0] = -glOffX - glPosX;
	glViewRect[1] = +glOffX - glPosX;
	glViewRect[2] = +glOffY + glPosY;
	glViewRect[3] = -glOffY + glPosY;
}

QPoint UVWidget::mapFromContents( const Vector2 & v ) const
{
	float x = ( ( v[0] - 0.5 ) - glViewRect[ 0 ] ) / ( glViewRect[ 1 ] - glViewRect[ 0 ] ) * width();
	float y = ( ( v[1] - 0.5 ) - glViewRect[ 2 ] ) / ( glViewRect[ 3 ] - glViewRect[ 2 ] ) * height() * ( -1 ) + height();

	return QPointF( x, y ).toPoint();
}

Vector2 UVWidget::mapToContents( const QPoint & p ) const
{
	float x = ( (float)p.x() / (float)width() ) * ( glViewRect[ 1 ] - glViewRect[ 0 ] ) + glViewRect[ 0 ];
	float y = ( (float)p.y() / (float)height() ) * ( glViewRect[ 2 ] - glViewRect[ 3 ] ) + glViewRect[ 3 ];
	return Vector2( x, y );
}

QVector<int> UVWidget::indices( const QPoint & p ) const
{
	return indices( QRect( p - QPoint( 2, 2 ), QSize( 5, 5 ) ) );
}

QVector<int> UVWidget::indices( const QRegion & region ) const
{
	QList<int> hits;

	for ( int i = 0; i < texcoords.count(); i++ ) {
		if ( region.contains( mapFromContents( texcoords[ i ] ) ) )
			hits << i;
	}

	return hits.toVector();
}

bool UVWidget::bindTexture( const QString & filename )
{
	GLuint mipmaps = 0;
	mipmaps = textures->bind( filename );

	if ( mipmaps ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		// TODO: Add support for non-square textures

		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();

		glMatrixMode( GL_MODELVIEW );
		return true;
	}

	return false;
}

bool UVWidget::bindTexture( const QModelIndex & iSource )
{
	GLuint mipmaps = 0;
	mipmaps = textures->bind( iSource );

	if ( mipmaps ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		// TODO: Add support for non-square textures

		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();

		glMatrixMode( GL_MODELVIEW );
		return true;
	}

	return false;
}


QSize UVWidget::sizeHint() const
{
	if ( sHint.isValid() ) {
		return sHint;
	}

	return QSizeF( BASESIZE, BASESIZE ).toSize();
}

void UVWidget::setSizeHint( const QSize & s )
{
	sHint = s;
}

void UVWidget::frameUVs()
{
	if(cageHandle>=0)return;
	if(texcoords.isEmpty())return;
	Vector2 low=texcoords.first(),high=low;
	for(auto uv:texcoords)for(int a=0;a<2;++a){low[a]=qMin(low[a],uv[a]);high[a]=qMax(high[a],uv[a]);}
	auto center=(low+high)*.5f;
	pos=QPointF(-(center[0]-.5)*BASESIZE,(center[1]-.5)*BASESIZE);
	zoom=qMax(.2,qMax(double(high[0]-low[0])*BASESIZE/qMax(1,width()),double(high[1]-low[1])*BASESIZE/qMax(1,height()-140))*1.2);
	updateViewRect(width(),height());updateGL();
}

QSize UVWidget::minimumSizeHint() const
{
	return QSizeF( BASESIZE, BASESIZE ).toSize();
}

int UVWidget::heightForWidth( int width ) const
{
	if ( width < minimumSizeHint().height() )
		return minimumSizeHint().height();

	return width;
}

void UVWidget::mousePressEvent(QMouseEvent * e)
{
 QPoint previous=mousePos;mousePos=e->pos();
 if(e->button()!=Qt::LeftButton)return;
 if(!nif || uvDragging)return;
 bool modify=e->modifiers() & (Qt::ShiftModifier|Qt::ControlModifier);
 if(!(e->modifiers() & (Qt::ControlModifier|Qt::AltModifier))) {
  int handle=pickCage(e->pos());if(handle>0 || (handle==0 && !modify)){beginCageDrag(handle,e->pos());updateGL();return;}
 }
 auto hits=indices(e->pos());
 if(!hits.isEmpty() && !(e->modifiers() & Qt::AltModifier)) {
  if((previous-e->pos()).manhattanLength()>4)selectCycle=0;else ++selectCycle;
  int hit=hits[selectCycle%hits.size()];
  if(e->modifiers() & Qt::ControlModifier)select(hit,!isSelected(hit));
  else if(e->modifiers() & Qt::ShiftModifier)select(hit);
  else {if(!isSelected(hit))selectNone();select(hit);beginCageDrag(0,e->pos());}
 } else {
  selectionModifiers=e->modifiers();boxSelecting=true;
  if(!modify)selectNone();
  if(e->modifiers() & Qt::AltModifier)selectPoly<<e->pos();else selectRect=QRect(e->pos(),e->pos());
 }
 updateCageCursor(e->pos());updateGL();
}

void UVWidget::mouseMoveEvent(QMouseEvent * e)
{
 QPoint delta=e->pos()-mousePos;
 if(e->buttons()==Qt::LeftButton) {
  if(cageHandle>=0)updateCageDrag(e->pos(),e->modifiers());
  else if(boxSelecting){if(!selectPoly.isEmpty())selectPoly<<e->pos();else selectRect.setBottomRight(e->pos());}
 } else if(e->buttons()==Qt::MiddleButton && cageHandle<0) {
  pos+=zoom*QPointF(delta.x(),-delta.y());updateViewRect(width(),height());setCursor(Qt::ClosedHandCursor);
 } else if(e->buttons()==Qt::RightButton && cageHandle<0) {
  zoom=qBound(MINZOOM,zoom*(1.0+delta.y()/ZOOMUNIT),MAXZOOM);updateViewRect(width(),height());setCursor(Qt::SizeVerCursor);
 } else if(e->buttons()==Qt::NoButton)updateCageCursor(e->pos());
 mousePos=e->pos();updateGL();
}

void UVWidget::mouseReleaseEvent(QMouseEvent * e)
{
 if(e->button()==Qt::LeftButton) {
  if(cageHandle>=0){updateCageDrag(e->pos(),e->modifiers());finishCageDrag();}
  else if(boxSelecting) {
   viewportFaceSelection=false;
   QRegion region;
   if(selectPoly.size()>2)region=QRegion(QPolygon(selectPoly.toVector()));
   else if(selectPoly.isEmpty())region=QRegion(selectRect.normalized());
   for(int hit:indices(region)) {
    if((selectionModifiers & Qt::ControlModifier) && selection.contains(hit))selection.removeAll(hit);
    else if(!selection.contains(hit))selection<<hit;
   }
  }
  boxSelecting=false;selectRect=QRect();selectPoly.clear();emit selectionChanged();
 }
 updateCageCursor(e->pos());updateGL();
}
void UVWidget::wheelEvent( QWheelEvent * e )
{
	if(cageHandle>=0)return;
	switch ( e->modifiers() ) {
	case Qt::NoModifier:
		zoom *= 1.0 + ( e->delta() / 8.0 ) / ZOOMUNIT;

		if ( zoom < MINZOOM ) {
			zoom = MINZOOM;
		} else if ( zoom > MAXZOOM ) {
			zoom = MAXZOOM;
		}

		updateViewRect( width(), height() );

		break;
	}

	updateGL();
}

void UVWidget::focusOutEvent(QFocusEvent * event)
{
	finishCageDrag(true);boxSelecting=false;selectRect=QRect();selectPoly.clear();kbd.clear();
	QGLWidget::focusOutEvent(event);
}

void UVWidget::keyPressEvent( QKeyEvent * e )
{
 if(e->key()==Qt::Key_Escape && (cageHandle>=0 || boxSelecting)) {
  finishCageDrag(true);boxSelecting=false;selectRect=QRect();selectPoly.clear();updateGL();e->accept();return;
 }	switch ( e->key() ) {
	case Qt::Key_X:
	case Qt::Key_Y:
		kbd[e->key()] = true;
		break;
	default:
		e->ignore();
		break;
	}
}

void UVWidget::keyReleaseEvent( QKeyEvent * e )
{
	switch ( e->key() ) {
	case Qt::Key_X:
	case Qt::Key_Y:
		kbd[e->key()] = false;
		break;
	default:
		e->ignore();
		break;
	}
}

bool UVWidget::setNifData( NifModel * nifModel, const QModelIndex & nifIndex )
{
 finishCageDrag(true);boxSelecting=false;selectRect=QRect();selectPoly.clear();
	if ( nif ) {
		// disconnect( nif ) may not work with new Qt5 syntax...
		// it says the calls need to remain symmetric to the connect() ones.
		// Otherwise, use QMetaObject::Connection
		//disconnect( nif );
		QObject::disconnect(nif,nullptr,this,nullptr);
		QObject::disconnect(nif->undoStack,nullptr,this,nullptr);
	}

	undoStack->clear();

	nif = nifModel;
	iShape = nifIndex;
	isDataOnSkin = false;
	texcoords.clear();faces.clear();texcoords2faces.clear();selection.clear();texsource=QModelIndex();texfile.clear();
	currentCoordSet=0;currentTexSlot=0;
	for(auto menu:findChildren<QMenu *>(QString(),Qt::FindDirectChildrenOnly))delete menu;
	for(auto group:findChildren<QActionGroup *>(QString(),Qt::FindDirectChildrenOnly))delete group;
	menuTexSelect=nullptr;texSlotGroup=nullptr;mapStatus->clear();

	// Version dependent actions
	if ( nif && nif->getVersionNumber() != 0x14020007 ) {
		coordSetGroup = new QActionGroup( this );
		connect( coordSetGroup, &QActionGroup::triggered, this, &UVWidget::selectCoordSet );

		coordSetSelect = new QMenu( tr( "Select Coordinate Set" ),this );
		addAction( coordSetSelect->menuAction() );
		connect( coordSetSelect, &QMenu::aboutToShow, this, &UVWidget::getCoordSets );

		texSlotGroup = new QActionGroup( this );
		connect( texSlotGroup, &QActionGroup::triggered, this, &UVWidget::selectTexSlot );

		menuTexSelect = new QMenu( tr( "Select Texture Slot" ),this );
		addAction( menuTexSelect->menuAction() );
		connect( menuTexSelect, &QMenu::aboutToShow, this, &UVWidget::getTexSlots );
	}

	if ( nif ) {
		connect(nif,&NifModel::modelAboutToBeReset,this,[this]{if(!restoringUndo)restoreShape=nif->getBlockNumber(iShape);});
connect(nif,&NifModel::modelReset,this,[this](){if(!restoringUndo)QTimer::singleShot(0,this,[this]{if(nif && !restoringUndo){if(!iShape.isValid())setNifData(nif,nif->getBlock(restoreShape));else setTexCoords();updateGL();}});});
		connect(nif,&NifModel::undoAboutToRestore,this,[this](){restoreShape=nif->getBlockNumber(iShape);restoreBlockCount=nif->getBlockCount();restoringUndo=true;});
		connect(nif,&NifModel::undoRestored,this,[this](){
			auto model=nif;auto selected=selection;int coordinateSet=currentCoordSet;int textureSlot=currentTexSlot;auto savedFaces=viewportFaces;bool savedFaceMode=viewportFaceSelection;
			if(!setNifData(model,model->getBlock(restoreShape))){restoringUndo=false;mapStatus->setText(tr("Select a mesh with editable UVs."));return;}
			restoringUndo=false;
			int restoredTexture=textureChoice->findData(textureSlot);if(restoredTexture>=0)chooseTexture(restoredTexture);changeCoordSet(coordinateSet);
			for(int i:selected)if(i>=0 && i<texcoords.size())selection<<i;if(savedFaceMode)setViewportSelection({},savedFaces,true);
			updateGL();
		});
		connect(nif->undoStack,&QUndoStack::canUndoChanged,this,[this](bool value){documentUndo->setEnabled(value);});
		connect(nif->undoStack,&QUndoStack::canRedoChanged,this,[this](bool value){documentRedo->setEnabled(value);});
		documentUndo->setEnabled(nif->undoStack->canUndo());documentRedo->setEnabled(nif->undoStack->canRedo());
		connect(nif,&NifModel::destroyed,this,[this](){nif=nullptr;uvDragging=false;window()->close();});
		connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
		connect( nif, &NifModel::rowsRemoved, this, &UVWidget::nifDataChanged );
		connect(nif,&NifModel::linksChanged,this,[this]{QTimer::singleShot(0,this,[this]{if(nif && iShape.isValid() && !restoringUndo)followMesh(nif,iShape);});});
	}

	if ( !nif )
		return false;

	textures->setNifFolder( nif->getFolder() );

	iShapeData = nif->getBlock( nif->getLink( iShape, "Data" ) );
	if ( nif->getVersionNumber() == 0x14020007 && nif->getUserVersion2() >= 100 ) {
		iShapeData = nif->getIndex( iShape, "Vertex Data" );

		auto vf = nif->get<BSVertexDesc>( iShape, "Vertex Desc" );
		if ( (vf & VertexFlags::VF_SKINNED) && nif->getUserVersion2() == 100 ) {
			// Skinned SSE
			auto skinID = nif->getLink( nif->getIndex( iShape, "Skin" ) );
			auto partID = nif->getLink( nif->getBlock( skinID, "NiSkinInstance" ), "Skin Partition" );
			iPartBlock = nif->getBlock( partID, "NiSkinPartition" );
			if ( !iPartBlock.isValid() )
				return false;

			isDataOnSkin = true;

			iShapeData = nif->getIndex( iPartBlock, "Vertex Data" );
		}
	}

	if(nif->inherits(iShape,"NiMesh")) {
		iShapeData=iShape;iTexCoords=iShape;
		auto sets=UVEditData::coordinateSets(nif,iShape);if(sets.isEmpty())return false;currentCoordSet=sets.first();
		if(!setTexCoords())return false;
	} else if ( nif->inherits( iShapeData, "NiTriBasedGeomData" ) ) {
		iTexCoords = nif->getIndex( iShapeData, "UV Sets" ).child( 0, 0 );

		if ( !iTexCoords.isValid() || !nif->rowCount( iTexCoords ) ) {
			return false;
		}

		if ( !setTexCoords() ) {
			return false;
		}
	} else if ( nif->inherits( iShape, "BSTriShape" ) ) {
		int numVerts = 0;
		if ( !isDataOnSkin )
			numVerts = nif->get<int>( iShape, "Num Vertices" );
		else
			numVerts = nif->get<uint>( iPartBlock, "Data Size" ) / nif->get<uint>( iPartBlock, "Vertex Size" );

		for ( int i = 0; i < numVerts; i++ ) {
			texcoords << nif->get<Vector2>( nif->index( i, 0, iShapeData ), "UV" );
		}

		// Fake index so that isValid() checks do not fail
		iTexCoords = iShape;

		if ( !setTexCoords() )
			return false;
	}

	if(texcoords.isEmpty() || faces.isEmpty())return false;
	getTexSlots();
	if(textureChoice->isEnabled()){for(int i=0;i<textureChoice->count();++i)if(qobject_cast<QStandardItemModel *>(textureChoice->model())->item(i)->isEnabled()){chooseTexture(i);return true;}}
	auto props = nif->getLinkArray( iShape, "Properties" );
	props << nif->getLink( iShape, "Shader Property" );
	for ( const auto l : props )
	{
		QModelIndex iTexProp = nif->getBlock( l, "NiTexturingProperty" );

		if ( iTexProp.isValid() ) {
			mapStatus->setText(tr("No matching UV set"));return true;
		} else {
			iTexProp = nif->getBlock( l, "NiTextureProperty" );

			if ( iTexProp.isValid() ) {
				QModelIndex iTexSource = nif->getBlock( nif->getLink( iTexProp, "Image" ) );

				if ( iTexSource.isValid() ) {
					//texfile = TexCache::find( nif->get<QString>( iTexSource, "File Name" ) , nif->getFolder() );
					texsource = iTexSource;
					return true;
				}
			} else {
				// TODO: use the BSShaderTextureSet
				iTexProp = nif->getBlock( l, "BSShaderPPLightingProperty" );

				if ( !iTexProp.isValid() )
					iTexProp = nif->getBlock( l, "BSLightingShaderProperty" );

				if ( iTexProp.isValid() ) {
					QModelIndex iTexSource = nif->getBlock( nif->getLink( iTexProp, "Texture Set" ) );

					if ( iTexSource.isValid() ) {
						// Assume that a FO3 mesh never has embedded textures...
						//texsource = iTexSource;
						//return true;
						QModelIndex iTextures = nif->getIndex( iTexSource, "Textures" );

						if ( iTextures.isValid() ) {
							texfile = TexCache::find( nif->get<QString>( iTextures.child( 0, 0 ) ), nif->getFolder() );
							return true;
						}
					}
				} else {
					iTexProp = nif->getBlock( l, "BSEffectShaderProperty" );

					if ( iTexProp.isValid() ) {
						QString texture = nif->get<QString>( iTexProp, "Source Texture" );

						if ( !texture.isEmpty() ) {
							texfile = TexCache::find( texture, nif->getFolder() );
							return true;
						}
					}
				}
			}
		}
	}

	return true;
}

bool UVWidget::setTexCoords()
{
	faces.clear();texcoords2faces.clear();
	if(nif->inherits(iShape,"NiMesh")) {
		UVEditData data;if(!data.read(nif,iShape,currentCoordSet))return false;texcoords=data.coordinates;
		for(int i=0;i<data.indices.size();i+=3){int f=faces.size();faces<<face(f,data.indices[i],data.indices[i+1],data.indices[i+2]);for(int j=0;j<3;++j)texcoords2faces.insertMulti(data.indices[i+j],f);}
		return true;
	}
	if ( nif->inherits( iShape, "NiTriBasedGeom" ) )
		texcoords = nif->getArray<Vector2>( iTexCoords );

	QVector<Triangle> tris;

	if ( nif->isNiBlock( iShapeData, "NiTriShapeData" ) ) {
		tris = nif->getArray<Triangle>( iShapeData, "Triangles" );
	} else if ( nif->isNiBlock( iShapeData, "NiTriStripsData" ) ) {
		QModelIndex iPoints = nif->getIndex( iShapeData, "Points" );

		if ( !iPoints.isValid() )
			return false;

		for ( int r = 0; r < nif->rowCount( iPoints ); r++ ) {
			tris += triangulate( nif->getArray<quint16>( iPoints.child( r, 0 ) ) );
		}
	} else if ( nif->inherits( iShape, "BSTriShape" ) ) {
		if ( !isDataOnSkin ) {
			tris = nif->getArray<Triangle>( iShape, "Triangles" );
		} else {
			auto partIdx = nif->getIndex( iPartBlock, "Partition" );
			for ( int i = 0; i < nif->rowCount( partIdx ); i++ ) {
				tris << nif->getArray<Triangle>( nif->index( i, 0, partIdx ), "Triangles" );
			}
		}
		
	}

	if ( tris.isEmpty() )
		return false;

	QVectorIterator<Triangle> itri( tris );

	while ( itri.hasNext() ) {
		const Triangle & t = itri.next();

		int fIdx = faces.size();
		faces.append( face( fIdx, t[0], t[1], t[2] ) );

		for ( int i = 0; i < 3; i++ ) {
			if(t[i]>=texcoords.size()){faces.clear();return false;}
			texcoords2faces.insertMulti( t[i], fIdx );
		}
	}

	return true;
}

void UVWidget::updateNif()
{
	if ( nif && iTexCoords.isValid() ) {
		disconnect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
		nif->setState( BaseModel::Processing );

		if(nif->inherits(iShape,"NiMesh")) {
			UVEditData data;
			if(!data.read(nif,iShape,currentCoordSet) || !data.write(nif,texcoords))setTexCoords();
		} else if ( nif->inherits( iShapeData, "NiTriBasedGeomData" ) ) {
			nif->setArray<Vector2>( iTexCoords, texcoords );
		} else if ( nif->inherits( iShape, "BSTriShape" ) ) {
			int numVerts = 0;
			if ( !isDataOnSkin )
				numVerts = nif->get<int>( iShape, "Num Vertices" );
			else
				numVerts = nif->get<uint>( iPartBlock, "Data Size" ) / nif->get<uint>( iPartBlock, "Vertex Size" );

			for ( int i = 0; i < numVerts; i++ ) {
				nif->set<HalfVector2>( nif->index( i, 0, iShapeData ), "UV", HalfVector2( texcoords.value( i ) ) );
			}

			nif->dataChanged( iShape, iShape );
		}
		
		nif->restoreState();
		emit nif->dataChanged(iShape,iShape);
		connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
	}
}

void UVWidget::nifDataChanged( const QModelIndex & idx )
{
	if ( !nif || !iShape.isValid() || !iShapeData.isValid() || !iTexCoords.isValid() ) {
		mapStatus->setText(tr("Select a mesh with editable UVs."));
		texcoords.clear();faces.clear();selection.clear();updateGL();
		return;
	}

	if ( nif->getBlock( idx ) == iShapeData ) {
		if(uvRefreshQueued)return;
		uvRefreshQueued=true;
		QTimer::singleShot(0,this,[this]{
			uvRefreshQueued=false;
			if(!nif || !iShape.isValid() || !iTexCoords.isValid() || !setTexCoords()){mapStatus->setText(tr("UV data unavailable; select another mesh."));texcoords.clear();faces.clear();selection.clear();updateGL();return;}
			for(auto it=selection.begin();it!=selection.end();)if(*it<0 || *it>=texcoords.size())it=selection.erase(it);else ++it;
			updateGL();
		});
	}
}

bool UVWidget::isSelected( int index )
{
	return selection.contains( index );
}

class UVWSelectCommand final : public QUndoCommand
{
public:
	UVWSelectCommand( UVWidget * w, const QList<int> & nSel ) : QUndoCommand(), uvw( w ), newSelection( nSel )
	{
		setText( "Select" );
	}

	int id() const override final
	{
		return 0;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			newSelection = static_cast<const UVWSelectCommand *>( cmd )->newSelection;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		oldSelection = uvw->selection;
		uvw->selection = newSelection;uvw->viewportFaceSelection=false;
		emit uvw->selectionChanged();
		uvw->updateGL();
	}

	void undo() override final
	{
		uvw->selection = oldSelection;uvw->viewportFaceSelection=false;
		emit uvw->selectionChanged();
		uvw->updateGL();
	}

protected:
	UVWidget * uvw;
	QList<int> oldSelection, newSelection;
};

void UVWidget::select( int index, bool yes )
{
	QList<int> sel = this->selection;

	if ( yes ) {
		if ( !sel.contains( index ) )
			sel.append( index );
	} else {
		sel.removeAll( index );
	}

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::select( const QRegion & r, bool add )
{
	QList<int> sel( add ? this->selection : QList<int>() );
	for ( const auto s : indices( r ) ) {
		if ( !sel.contains( s ) )
			sel.append( s );
	}
	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectNone()
{
	undoStack->push( new UVWSelectCommand( this, QList<int>() ) );
}

void UVWidget::invertSelection()
{
 if(uvDragging || cageHandle>=0 || boxSelecting)return;
 QList<int> inverted;for(int i=0;i<texcoords.size();++i)if(!selection.contains(i))inverted<<i;
 undoStack->push(new UVWSelectCommand(this,inverted));
}

void UVWidget::cubeUnwrap()
{
 if(!nif || uvDragging || cageHandle>=0)return;
 auto triangles=selectedFaces();QString error;
 if(!cubeUnwrapMesh(nif,iShape,currentCoordSet,triangles,&error)){QMessageBox::warning(this,tr("Cube Unwrap"),error);return;}
 undoStack->clear();if(!setTexCoords())return;
 if(triangles.isEmpty())for(const auto & face:faces)triangles.insert(face.index);
 setViewportSelection({},triangles,true);emit selectionChanged();frameUVs();updateGL();
}

void UVWidget::selectAll()
{
	QList<int> sel;

	for ( int s = 0; s < texcoords.count(); s++ )
		sel << s;

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectFaces()
{
	QList<int> sel = this->selection;
	for ( const auto s : QList<int>( sel ) ) {
		for ( const auto f : texcoords2faces.values( s ) ) {
			for ( int i = 0; i < 3; i++ ) {
				if ( !sel.contains( faces[f].tc[i] ) )
					sel.append( faces[f].tc[i] );
			}
		}
	}
	undoStack->push( new UVWSelectCommand( this, sel ) );
}

void UVWidget::selectConnected()
{
	QList<int> sel = this->selection;
	bool more = true;

	while ( more ) {
		more = false;
		for ( const auto s : QList<int>( sel ) ) {
			for ( const auto f :texcoords2faces.values( s ) ) {
				for ( int i = 0; i < 3; i++ ) {
					if ( !sel.contains( faces[f].tc[i] ) ) {
						sel.append( faces[f].tc[i] );
						more = true;
					}
				}
			}
		}
	}

	undoStack->push( new UVWSelectCommand( this, sel ) );
}

class UVWMoveCommand final : public QUndoCommand
{
public:
	UVWMoveCommand( UVWidget * w, double dx, double dy ) : QUndoCommand(), uvw( w ), move( dx, dy )
	{
		setText( "Move" );
	}

	int id() const override final
	{
		return 1;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			move += static_cast<const UVWMoveCommand *>( cmd )->move;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		for ( const auto tc : uvw->selection ) {
			uvw->texcoords[tc] += move;
		}
		uvw->updateNif();
		uvw->updateGL();
	}

	void undo() override final
	{
		for ( const auto tc : uvw->selection ) {
			uvw->texcoords[tc] -= move;
		}
		uvw->updateNif();
		uvw->updateGL();
	}

protected:
	UVWidget * uvw;
	Vector2 move;
};

void UVWidget::moveSelection( double moveX, double moveY )
{
	if(selection.isEmpty())return;
	nif->beginUndoTransaction(tr("Move UVs"));
	nif->applyCommand(new UVWMoveCommand(this,moveX,moveY));
	nif->endUndoTransaction();
}

// For mouse-driven scaling: insert state/flag for scaling mode
// get difference in mouse coords, scale everything around centre

//! A class to perform scaling of UV coordinates
class UVWScaleCommand final : public QUndoCommand
{
public:
	UVWScaleCommand( UVWidget * w, float sX, float sY ) : QUndoCommand(), uvw( w ), scaleX( sX ), scaleY( sY )
	{
		setText( "Scale" );
	}

	int id() const override final
	{
		return 2;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			scaleX *= static_cast<const UVWScaleCommand *>( cmd )->scaleX;
			scaleY *= static_cast<const UVWScaleCommand *>( cmd )->scaleY;
			return true;
		}

		return false;
	}

	void redo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		for ( const auto i : uvw->selection ) {
			Vector2 temp( uvw->texcoords[i] );
			uvw->texcoords[i] = Vector2( temp[0] * scaleX, temp[1] * scaleY );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->updateGL();
	}

	void undo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		for ( const auto i : uvw->selection ) {
			Vector2 temp( uvw->texcoords[i] );
			uvw->texcoords[i] = Vector2( temp[0] / scaleX, temp[1] / scaleY );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->updateGL();
	}

protected:
	UVWidget * uvw;
	float scaleX, scaleY;
};

void UVWidget::scaleSelection()
{
	if(selection.isEmpty())return;
	ScalingDialog * scaleDialog = new ScalingDialog( this );

	if ( scaleDialog->exec() == QDialog::Accepted ) {
		nif->beginUndoTransaction(tr("Scale and translate UVs"));
		// order does not matter here, since we scale around the center
		// don't perform identity transforms
		if ( !( scaleDialog->getXScale() == 1.0 && scaleDialog->getYScale() == 1.0 ) ) {
			nif->applyCommand( new UVWScaleCommand( this, scaleDialog->getXScale(), scaleDialog->getYScale() ) );
		}

		if ( !( scaleDialog->getXMove() == 0.0 && scaleDialog->getYMove() == 0.0 ) ) {
			nif->applyCommand( new UVWMoveCommand( this, scaleDialog->getXMove(), scaleDialog->getYMove() ) );
		}
		nif->endUndoTransaction();
	}
	delete scaleDialog;
}

ScalingDialog::ScalingDialog( QWidget * parent ) : QDialog( parent )
{
	grid = new QGridLayout;
	setLayout( grid );
	int currentRow = 0;

	grid->addWidget( new QLabel( tr( "Enter scaling factors" ) ), currentRow, 0, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( "X: " ), currentRow, 0, 1, 1 );
	spinXScale = new QDoubleSpinBox;
	spinXScale->setValue( 1.0 );
	spinXScale->setRange( -MAXSCALE, MAXSCALE );
	grid->addWidget( spinXScale, currentRow, 1, 1, 1 );

	grid->addWidget( new QLabel( "Y: " ), currentRow, 2, 1, 1 );
	spinYScale = new QDoubleSpinBox;
	spinYScale->setValue( 1.0 );
	spinYScale->setRange( -MAXSCALE, MAXSCALE );
	grid->addWidget( spinYScale, currentRow, 3, 1, 1 );
	currentRow++;

	uniform = new QCheckBox;
	connect( uniform, &QCheckBox::toggled, this, &ScalingDialog::setUniform );
	uniform->setChecked( true );
	grid->addWidget( uniform, currentRow, 0, 1, 1 );
	grid->addWidget( new QLabel( tr( "Uniform scaling" ) ), currentRow, 1, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( tr( "Enter translation amounts" ) ), currentRow, 0, 1, -1 );
	currentRow++;

	grid->addWidget( new QLabel( "X: " ), currentRow, 0, 1, 1 );
	spinXMove = new QDoubleSpinBox;
	spinXMove->setValue( 0.0 );
	spinXMove->setRange( -MAXTRANS, MAXTRANS );
	grid->addWidget( spinXMove, currentRow, 1, 1, 1 );

	grid->addWidget( new QLabel( "Y: " ), currentRow, 2, 1, 1 );
	spinYMove = new QDoubleSpinBox;
	spinYMove->setValue( 0.0 );
	spinYMove->setRange( -MAXTRANS, MAXTRANS );
	grid->addWidget( spinYMove, currentRow, 3, 1, 1 );
	currentRow++;

	QPushButton * ok = new QPushButton( tr( "OK" ) );
	grid->addWidget( ok, currentRow, 0, 1, 2 );
	connect( ok, &QPushButton::clicked, this, &ScalingDialog::accept );

	QPushButton * cancel = new QPushButton( tr( "Cancel" ) );
	grid->addWidget( cancel, currentRow, 2, 1, 2 );
	connect( cancel, &QPushButton::clicked, this, &ScalingDialog::reject );
}

float ScalingDialog::getXScale()
{
	return spinXScale->value();
}

float ScalingDialog::getYScale()
{
	return spinYScale->value();
}

void ScalingDialog::setUniform( bool status )
{
	// Cast QDoubleSpinBox slot
	auto dsbValueChanged = static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);

	if ( status == true ) {
		connect( spinXScale, dsbValueChanged, spinYScale, &QDoubleSpinBox::setValue );
		spinYScale->setEnabled( false );
		spinYScale->setValue( spinXScale->value() );
	} else {
		disconnect( spinXScale, dsbValueChanged, spinYScale, &QDoubleSpinBox::setValue );
		spinYScale->setEnabled( true );
	}
}

// 1 unit corresponds to 2 grid squares
float ScalingDialog::getXMove()
{
	return spinXMove->value() / 2.0;
}

float ScalingDialog::getYMove()
{
	return spinYMove->value() / 2.0;
}

//! A class to perform rotation of UV coordinates
class UVWRotateCommand final : public QUndoCommand
{
public:
	UVWRotateCommand( UVWidget * w, float r ) : QUndoCommand(), uvw( w ), rotation( r )
	{
		setText( "Rotation" );
	}

	int id() const override final
	{
		return 3;
	}

	bool mergeWith( const QUndoCommand * cmd ) override final
	{
		if ( cmd->id() == id() ) {
			rotation += static_cast<const UVWRotateCommand *>( cmd )->rotation;
			rotation -= 360.0 * (int)( rotation / 360.0 );
			return true;
		}

		return false;
	}

	void redo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		Matrix rotMatrix;
		rotMatrix.fromEuler( 0, 0, ( rotation * PI / 180.0 ) );

		for ( const auto i : uvw->selection ) {
			Vector3 temp( uvw->texcoords[i], 0 );
			temp = rotMatrix * temp;
			uvw->texcoords[i] = Vector2( temp[0], temp[1] );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->updateGL();
	}

	void undo() override final
	{
		Vector2 centre;
		for ( const auto i : uvw->selection ) {
			centre += uvw->texcoords[i];
		}
		centre /= uvw->selection.size();

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] -= centre;
		}

		Matrix rotMatrix;
		rotMatrix.fromEuler( 0, 0, -( rotation * PI / 180.0 ) );

		for ( const auto i : uvw->selection ) {
			Vector3 temp( uvw->texcoords[i], 0 );
			temp = rotMatrix * temp;
			uvw->texcoords[i] = Vector2( temp[0], temp[1] );
		}

		for ( const auto i : uvw->selection ) {
			uvw->texcoords[i] += centre;
		}

		uvw->updateNif();
		uvw->updateGL();
	}

protected:
	UVWidget * uvw;
	float rotation;
};

void UVWidget::rotateSelection()
{
	if(selection.isEmpty())return;
	bool ok;
	float rotateFactor = QInputDialog::getDouble( this, "NifSkope", tr( "Enter rotation angle" ), 0.0, -360.0, 360.0, 2, &ok );

	if ( ok ) {
		nif->beginUndoTransaction(tr("Rotate UVs"));
		nif->applyCommand( new UVWRotateCommand( this, rotateFactor ) );
		nif->endUndoTransaction();
	}
}

QModelIndex UVWidget::textureProperty() const
{
 if(!nif)return QModelIndex();QModelIndex block=iShape;QSet<int> visited;
 while(block.isValid() && !visited.contains(nif->getBlockNumber(block))){visited.insert(nif->getBlockNumber(block));for(int id:nif->getLinkArray(block,"Properties")){auto property=nif->getBlock(id);if(nif->inherits(property,"NiTexturingProperty"))return property;}block=nif->getBlock(nif->getParent(block));}return QModelIndex();
}

int UVWidget::textureCoordinateSet(const QModelIndex & descriptor) const
{
 auto flags=nif->getIndex(descriptor,"Flags");return flags.isValid()?(nif->get<quint16>(flags)&0xff):nif->get<uint>(descriptor,"UV Set");
}

void UVWidget::refreshUVChoices()
{
 QSignalBlocker blocker(uvChoice);uvChoice->clear();auto sets=UVEditData::coordinateSets(nif,iShape);
 if(!nif->inherits(iShape,"NiMesh")){sets.clear();for(int i=0;i<nif->rowCount(nif->getIndex(iShapeData,"UV Sets"));++i)sets<<i;if(sets.isEmpty())sets<<0;}
 for(int set:sets)uvChoice->addItem(tr("UV %1").arg(set+1),set);uvChoice->setCurrentIndex(uvChoice->findData(currentCoordSet));
}

void UVWidget::getTexSlots()
{
 QSignalBlocker blocker(textureChoice);textureChoice->clear();validTexs.clear();if(menuTexSelect)menuTexSelect->clear();
 auto property=textureProperty();refreshUVChoices();
 for(int slot=0;slot<texnames.size();++slot){auto descriptor=nif->getIndex(property,texnames[slot]);if(!descriptor.isValid())continue;
  auto source=nif->getBlock(nif->getLink(descriptor,"Source"));if(!source.isValid())continue;
  int set=textureCoordinateSet(descriptor);QString path=nif->get<QString>(source,"File Name");QString label=tr("%1 — UV %2 — %3").arg(texnames[slot]).arg(set+1).arg(path.isEmpty()?tr("Embedded texture"):QFileInfo(path).fileName());
  textureChoice->addItem(label,slot);int row=textureChoice->count()-1;textureChoice->setItemData(row,path,Qt::ToolTipRole);
  bool available=uvChoice->findData(set)>=0;if(!available)qobject_cast<QStandardItemModel *>(textureChoice->model())->item(row)->setEnabled(false);
  validTexs<<texnames[slot];if(menuTexSelect){auto action=menuTexSelect->addAction(label);action->setData(slot);action->setCheckable(true);action->setChecked(slot==currentTexSlot);action->setEnabled(available);texSlotGroup->addAction(action);}
 }
 textureChoice->setEnabled(textureChoice->count()>0);textureChoice->setCurrentIndex(textureChoice->findData(currentTexSlot));
 if(textureChoice->count()==0){textureChoice->addItem(tr("Current texture / no material maps"),-1);textureChoice->setCurrentIndex(0);}
 if(mapControls->parentWidget()==this)mapControls->setGeometry(8,8,qMax(100,width()-16),mapControls->sizeHint().height());
}

void UVWidget::chooseTexture(int index)
{
 if(index<0 || uvDragging || cageHandle>=0)return;int slot=textureChoice->itemData(index).toInt();if(slot<0 || slot>=texnames.size())return;
 auto descriptor=nif->getIndex(textureProperty(),texnames[slot]);auto source=nif->getBlock(nif->getLink(descriptor,"Source"));if(!source.isValid())return;
 int set=textureCoordinateSet(descriptor);if(uvChoice->findData(set)<0){mapStatus->setText(tr("UV set unavailable"));return;}
 int old=currentCoordSet;auto oldCoords=iTexCoords;currentCoordSet=set;if(!nif->inherits(iShape,"NiMesh") && nif->inherits(iShapeData,"NiTriBasedGeomData"))iTexCoords=nif->getIndex(iShapeData,"UV Sets").child(set,0);
 if(!setTexCoords()){currentCoordSet=old;iTexCoords=oldCoords;setTexCoords();mapStatus->setText(tr("UV set unavailable"));return;}
 currentTexSlot=slot;iTex=descriptor;texsource=source;texfile.clear();undoStack->clear();mapStatus->clear();refreshUVChoices();{QSignalBlocker blocker(textureChoice);textureChoice->setCurrentIndex(index);}frameUVs();updateGL();
}

void UVWidget::selectTexSlot()
{
 if(auto action=texSlotGroup->checkedAction())chooseTexture(textureChoice->findData(action->data().toInt()));
}

void UVWidget::getCoordSets()
{
	coordSetSelect->clear();

	// TODO: Broken for newer nif.xml where NiGeometryData has been corrected
	auto sets=UVEditData::coordinateSets(nif,iShape);
	if(!nif->inherits(iShape,"NiMesh"))for(int i=0;i<nif->rowCount(nif->getIndex(iShapeData,"UV Sets"));++i)sets<<i;

	for ( int i : sets ) {
		QAction * temp;
		coordSetSelect->addAction( temp = new QAction( QString( "%1" ).arg( i ), coordSetSelect ) );
		coordSetGroup->addAction( temp );
		temp->setCheckable( true );

		if ( i == currentCoordSet ) {
			temp->setChecked( true );
		}
	}

	if(nif->inherits(iShape,"NiMesh"))return;
	coordSetSelect->addSeparator();
	aDuplicateCoords = new QAction( tr( "Duplicate current" ), this );
	coordSetSelect->addAction( aDuplicateCoords );
	connect( aDuplicateCoords, &QAction::triggered, this, &UVWidget::duplicateCoordSet );
}

void UVWidget::selectCoordSet()
{
	QString selected = coordSetGroup->checkedAction()->text();
	bool ok;
	quint8 setToUse = selected.toInt( &ok );

	if ( !ok )
		return;


	// change coordinate set
	changeCoordSet( setToUse );
}

void UVWidget::changeCoordSet( int setToUse )
{
 if(uvDragging || cageHandle>=0)return;
 int old=currentCoordSet;auto oldCoords=iTexCoords;currentCoordSet=setToUse;
 if(!nif->inherits(iShape,"NiMesh") && nif->inherits(iShapeData,"NiTriBasedGeomData"))iTexCoords=nif->getIndex(iShapeData,"UV Sets").child(setToUse,0);
 if(!setTexCoords()){currentCoordSet=old;iTexCoords=oldCoords;setTexCoords();return;}
 undoStack->clear();refreshUVChoices();mapStatus->setText(iTex.isValid() && textureCoordinateSet(iTex)!=currentCoordSet?tr("UV override"):QString());frameUVs();updateGL();
}

void UVWidget::duplicateCoordSet()
{
	// this signal close the UVWidget
	disconnect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
	// expand the UV Sets array and duplicate the current coordinates
	quint8 numUvSets = nif->get<quint8>( iShapeData, "Num UV Sets" );
	nif->set<quint8>( iShapeData, "Num UV Sets", numUvSets + 1 );
	QModelIndex uvSets = nif->getIndex( iShapeData, "UV Sets" );
	nif->updateArray( uvSets );
	nif->setArray<Vector2>( uvSets.child( numUvSets, 0 ), nif->getArray<Vector2>( uvSets.child( currentCoordSet, 0 ) ) );
	// switch to that coordinate set
	changeCoordSet( numUvSets );
	// reconnect data changed signal
	connect( nif, &NifModel::dataChanged, this, &UVWidget::nifDataChanged );
}



QRectF UVWidget::selectionCage() const
{
 bool any=false;QPointF low,high;
 for(int i:selection)if(i>=0 && i<texcoords.size()) {
  auto p=QPointF(mapFromContents(texcoords[i]));
  if(!any){low=high=p;any=true;}else {low.setX(qMin(low.x(),p.x()));low.setY(qMin(low.y(),p.y()));high.setX(qMax(high.x(),p.x()));high.setY(qMax(high.y(),p.y()));}
 }
 if(!any)return QRectF();
 QRectF cage=QRectF(low,high).adjusted(-10,-10,10,10);
 if(cage.width()<28)cage.adjust(-(28-cage.width())/2,0,(28-cage.width())/2,0);
 if(cage.height()<28)cage.adjust(0,-(28-cage.height())/2,0,(28-cage.height())/2);
 return cage;
}

QPointF UVWidget::cageHandlePoint(int handle,const QRectF & cage) const
{
 switch(handle) {
 case 1:return cage.topLeft();case 2:return QPointF(cage.center().x(),cage.top());
 case 3:return cage.topRight();case 4:return QPointF(cage.right(),cage.center().y());
 case 5:return cage.bottomRight();case 6:return QPointF(cage.center().x(),cage.bottom());
 case 7:return cage.bottomLeft();case 8:return QPointF(cage.left(),cage.center().y());
 case 9:return QPointF(cage.center().x(),cage.top()-28);
 default:return cage.center();
 }
}

int UVWidget::pickCage(const QPoint & point) const
{
 auto cage=selectionCage();if(cage.isNull())return -1;
 for(int h=9;h>0;--h)if(QLineF(point,cageHandlePoint(h,cage)).length()<=8)return h;
 return cage.contains(point)?0:-1;
}

void UVWidget::drawSelectionCage()
{
 auto cage=selectionCage();if(cage.isNull())return;
 glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width(),height(),0,-1,1);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 auto vertex=[](QPointF p){glVertex2d(p.x(),p.y());};
 for(int pass=0;pass<2;++pass) {
  glLineWidth(pass?1.5f:3.5f);if(pass)glColor3f(1,.75f,.15f);else glColor3f(.08f,.08f,.08f);
  glBegin(GL_LINE_LOOP);vertex(cage.topLeft());vertex(cage.topRight());vertex(cage.bottomRight());vertex(cage.bottomLeft());glEnd();
  glBegin(GL_LINES);vertex(cageHandlePoint(2,cage));vertex(cageHandlePoint(9,cage));glEnd();
 }
 for(int h=1;h<=9;++h) {
  QPointF center=cageHandlePoint(h,cage);glColor3f(.08f,.08f,.08f);
  glBegin(h==9?GL_POLYGON:GL_QUADS);
  if(h==9){for(int a=0;a<24;++a)vertex(center+QPointF(6*cos(a*2*3.141592653589793/24),6*sin(a*2*3.141592653589793/24)));}
  else {vertex(center+QPointF(-4,-4));vertex(center+QPointF(4,-4));vertex(center+QPointF(4,4));vertex(center+QPointF(-4,4));}
  glEnd();glColor3f(1,.75f,.15f);glLineWidth(1.5f);glBegin(GL_LINE_LOOP);
  if(h==9){for(int a=0;a<24;++a)vertex(center+QPointF(6*cos(a*2*3.141592653589793/24),6*sin(a*2*3.141592653589793/24)));}
  else {vertex(center+QPointF(-4,-4));vertex(center+QPointF(4,-4));vertex(center+QPointF(4,4));vertex(center+QPointF(-4,4));}
  glEnd();
 }
 glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopAttrib();
}

void UVWidget::updateCageCursor(const QPoint & point)
{
 int handle=pickCage(point);
 switch(handle) {
 case 0:setCursor(Qt::SizeAllCursor);break;
 case 1:case 5:setCursor(Qt::SizeFDiagCursor);break;
 case 3:case 7:setCursor(Qt::SizeBDiagCursor);break;
 case 2:case 6:setCursor(Qt::SizeVerCursor);break;
 case 4:case 8:setCursor(Qt::SizeHorCursor);break;
 case 9:setCursor(Qt::OpenHandCursor);break;
 default:setCursor(indices(point).isEmpty()?Qt::CrossCursor:Qt::PointingHandCursor);break;
 }
}

void UVWidget::beginCageDrag(int handle,const QPoint & point)
{
 if(!nif || selection.isEmpty())return;
 cageHandle=handle;cagePress=point;cageStartCoords=texcoords;cageSelection=selection;
 auto low=texcoords[selection.first()],high=low;
 for(int i:selection)for(int a=0;a<2;++a){low[a]=qMin(low[a],texcoords[i][a]);high[a]=qMax(high[a],texcoords[i][a]);}
 cagePivot=(low+high)*.5f;
 if(handle>0 && handle<9) {
  if(handle==1 || handle==7 || handle==8)cagePivot[0]=high[0];
  if(handle==3 || handle==4 || handle==5)cagePivot[0]=low[0];
  if(handle==1 || handle==2 || handle==3)cagePivot[1]=high[1];
  if(handle==5 || handle==6 || handle==7)cagePivot[1]=low[1];
 }
 updateCageCursor(point);
}

void UVWidget::updateCageDrag(const QPoint & point,Qt::KeyboardModifiers modifiers)
{
 if(cageHandle<0 || !nif || !iShape.isValid() || cageStartCoords.size()!=texcoords.size())return;
 if(!uvDragging && (point-cagePress).manhattanLength()<3)return;
 auto start=mapToContents(cagePress)+Vector2(.5f,.5f),current=mapToContents(point)+Vector2(.5f,.5f);
 auto delta=current-start;float sx=1,sy=1,angle=0;
 if(cageHandle==0){if(kbd[Qt::Key_X])delta[1]=0;if(kbd[Qt::Key_Y])delta[0]=0;}
 else if(cageHandle==9) {
  auto a=start-cagePivot,b=current-cagePivot;
  angle=std::atan2(b[1],b[0])-std::atan2(a[1],a[0]);
  if(modifiers & Qt::ShiftModifier){const float step=3.141592653589793f/12;angle=std::round(angle/step)*step;}
 } else {
  auto a=start-cagePivot,b=current-cagePivot;
  bool scaleX=cageHandle!=2 && cageHandle!=6,scaleY=cageHandle!=4 && cageHandle!=8;
  if(scaleX && std::abs(a[0])>1e-8f)sx=b[0]/a[0];
  if(scaleY && std::abs(a[1])>1e-8f)sy=b[1]/a[1];
  if(scaleX && scaleY && (modifiers & Qt::ShiftModifier)){float length=a[0]*a[0]+a[1]*a[1];if(length>1e-12f)sx=sy=(a[0]*b[0]+a[1]*b[1])/length;}
 }
 auto values=cageStartCoords;
 for(int i:cageSelection) {
  if(i<0 || i>=values.size())return;
  if(cageHandle==0)values[i]+=delta;
  else {auto d=cageStartCoords[i]-cagePivot;
   if(cageHandle==9)values[i]=cagePivot+Vector2(d[0]*cos(angle)-d[1]*sin(angle),d[0]*sin(angle)+d[1]*cos(angle));
   else {values[i]=cagePivot+Vector2(d[0]*sx,d[1]*sy);if(cageHandle==2 || cageHandle==6)values[i][0]=cageStartCoords[i][0];if(cageHandle==4 || cageHandle==8)values[i][1]=cageStartCoords[i][1];}
  }
  if(!std::isfinite(values[i][0]) || !std::isfinite(values[i][1]))return;
 }
 if(values==texcoords)return;
 if(!uvDragging){nif->beginUndoTransaction(cageHandle==0?tr("Move UVs"):cageHandle==9?tr("Rotate UVs"):tr("Scale UVs"));uvDragging=true;}
 texcoords=values;updateNif();
}

void UVWidget::finishCageDrag(bool cancel)
{
 if(uvDragging && nif) {
  if(cancel && cageStartCoords.size()==texcoords.size()){texcoords=cageStartCoords;updateNif();}
  uvDragging=false;nif->endUndoTransaction();
 }
 cageHandle=-1;cageStartCoords.clear();cageSelection.clear();
}

QSet<int> UVWidget::selectedVertices() const
{
 QSet<int> result;for(int i:selection)if(i>=0 && i<texcoords.size())result.insert(i);return result;
}
QSet<int> UVWidget::selectedFaces() const
{
 if(viewportFaceSelection)return viewportFaces;
 auto vertices=selectedVertices();QSet<int> result;
 for(const auto & face:faces)if(vertices.contains(face.tc[0]) && vertices.contains(face.tc[1]) && vertices.contains(face.tc[2]))result.insert(face.index);
 return result;
}
void UVWidget::setViewportSelection(const QSet<int> & vertices,const QSet<int> & triangles,bool faceMode)
{
 if(uvDragging || cageHandle>=0)return;QSet<int> chosen;
 viewportFaceSelection=faceMode;viewportFaces=triangles;
 if(faceMode){for(const auto & face:faces)if(triangles.contains(face.index))for(int a=0;a<3;++a)chosen.insert(face.tc[a]);}
 else chosen=vertices;
 selection.clear();for(int i:chosen)if(i>=0 && i<texcoords.size())selection<<i;updateGL();
}

bool UVWidget::followMesh(NifModel * document,const QModelIndex & target)
{
 if(uvDragging || cageHandle>=0 || !document || !target.isValid())return false;
 auto block=document->getBlock(target);bool same=document==nif && block==iShape;
 if(same && (document->inherits(block,"NiMesh") || document->getBlock(document->getLink(block,"Data"))==iShapeData)){if(setTexCoords())return true;return false;}
 bool supported=false;
 if(document->inherits(block,"NiMesh")){UVEditData data;for(int set:UVEditData::coordinateSets(document,block))if(data.read(document,block,set)){supported=true;break;}}
 else {auto data=document->getBlock(document->getLink(block,"Data"));supported=document->inherits(block,"NiTriShape") && document->rowCount(document->getIndex(data,"UV Sets"))>0;}
 if(!supported)return false;
 auto previous=nif;auto previousMesh=iShape;int previousSlot=currentTexSlot,previousSet=currentCoordSet;
 if(!setNifData(document,block)){if(previous && previousMesh.isValid())setNifData(previous,previousMesh);return false;}
 if(same){int slot=textureChoice->findData(previousSlot);if(slot>=0)chooseTexture(slot);if(uvChoice->findData(previousSet)>=0)changeCoordSet(previousSet);}
 viewportFaces.clear();viewportFaceSelection=false;frameUVs();updateGL();return true;
}
