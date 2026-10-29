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

#include "nifskope.h"
#include "ui_nifskope.h"

#include "glview.h"
#include "message.h"
#include "spellbook.h"
#include "version.h"
#include "gl/glscene.h"
#include "model/kfmmodel.h"
#include "model/nifmodel.h"
#include "physx/physxcollision.h"
#include "model/nifproxymodel.h"
#include "ui/widgets/fileselect.h"
#include "ui/widgets/floatslider.h"
#include "ui/widgets/floatedit.h"
#include "ui/widgets/lightingwidget.h"
#include "ui/widgets/materialpanel.h"
#include "ui/widgets/roompanel.h"
#include "ui/widgets/nifview.h"
#include "ui/widgets/refrbrowser.h"
#include "ui/widgets/inspect.h"
#include "ui/widgets/uvedit.h"
#include "ui/widgets/xmlcheck.h"
#include "ui/about_dialog.h"
#include "ui/settingsdialog.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QFontDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QProgressBar>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include <QProcess>
#include <QStyleFactory>
#include <QRegularExpression>

QString nstypes::operator""_uip( const char * str, size_t )
{
	QString u;

#ifndef QT_NO_DEBUG
	u = "UI/Debug/";
#else
	u = "UI/";
#endif

	return u + QString( str );
}

using namespace nstypes;
using namespace nstheme;


QColor NifSkope::defaultsDark[6] = {
	QColor( 60, 60, 60 ),    /// nstheme::Base
	QColor( 50, 50, 50 ),    /// nstheme::BaseAlt
	Qt::white,               /// nstheme::Text
	QColor( 204, 204, 204 ), /// nstheme::Highlight
	Qt::black,               /// nstheme::HighlightText
	QColor( 255, 66, 58 )    /// nstheme::BrightText
};

QColor NifSkope::defaultsLight[6] = {
	QColor( 245, 245, 245 ), /// nstheme::Base
	QColor( 255, 255, 255 ), /// nstheme::BaseAlt
	Qt::black,               /// nstheme::Text
	QColor( 42, 130, 218 ),  /// nstheme::Highlight
	Qt::white,               /// nstheme::HighlightText
	Qt::red                  /// nstheme::BrightText
};



//! @file nifskope_ui.cpp UI logic for %NifSkope's main window.

NifSkope * NifSkope::createWindow( const QString & fname )
{
	NifSkope * skope = new NifSkope;
	skope->setAttribute( Qt::WA_DeleteOnClose );
	skope->restoreUi();
	skope->loadTheme();
	skope->show();
	skope->raise();

	if ( !fname.isEmpty() ) {
		skope->loadFile( fname );
	}

	return skope;
}

void NifSkope::initActions()
{
	aSanitize = ui->aSanitize;
	aList = ui->aList;
	aHierarchy = ui->aHierarchy;
	aCondition = ui->aCondition;
	aRCondition = ui->aRCondition;
	aSelectFont = ui->aSelectFont;

	// Build all actions list
	allActions = QSet<QAction *>::fromList( 
		ui->tFile->actions() 
		<< ui->mRender->actions()
		<< ui->tRender->actions()
		<< ui->tAnim->actions()
	);

	// Undo/Redo
	undoAction = new QAction(tr("&Undo"),this);
	connect(undoAction,&QAction::triggered,this,[this](){nif->undo();});
	undoAction->setShortcut( QKeySequence::Undo );
	undoAction->setObjectName( "aUndo" );
	undoAction->setIcon( QIcon( ":btn/undo" ) );
	allActions << undoAction;
	redoAction = new QAction(tr("&Redo"),this);
	connect(redoAction,&QAction::triggered,this,[this](){nif->redo();});
	redoAction->setShortcut( QKeySequence::Redo );
	redoAction->setObjectName( "aRedo" );
	redoAction->setIcon( QIcon( ":btn/redo" ) );
	allActions << redoAction;

	// TODO: Back/Forward button in Block List
	//idxForwardAction = indexStack->createRedoAction( this );
	//idxBackAction = indexStack->createUndoAction( this );

	ui->tFile->addAction( undoAction );
	ui->tFile->addAction( redoAction );

	connect( undoAction, &QAction::triggered, [this]( bool ) {
		ogl->update();
	} );

	connect( redoAction, &QAction::triggered, [this]( bool ) {
		ogl->update();
	} );

	ui->aSave->setShortcut( QKeySequence::Save );
	ui->aSaveAs->setShortcut( { "Ctrl+Alt+S" } );
	ui->aWindow->setShortcut( QKeySequence::New );

	connect( ui->aBrowseArchive, &QAction::triggered, this, &NifSkope::archiveDlg );
	connect( ui->aOpen, &QAction::triggered, this, &NifSkope::openDlg );
	connect( ui->aSave, &QAction::triggered, this, &NifSkope::save );  
	connect( ui->aSaveAs, &QAction::triggered, this, &NifSkope::saveAsDlg );

	// TODO: Assure Actions and Scene state are synced
	// Set Data for Actions to pass onto Scene when clicking
	/*	
		ShowAxes = 0x1,
		ShowGrid = 0x2,
		ShowNodes = 0x4,
		ShowCollision = 0x8,
		ShowConstraints = 0x10,
		ShowMarkers = 0x20,
		DoDoubleSided = 0x40, // Not implemented
		DoVertexColors = 0x80,
		DoSpecular = 0x100,
		DoGlow = 0x200,
		DoTexturing = 0x400,
		DoBlending = 0x800,   // Not implemented
		DoMultisampling = 0x1000, // Not implemented
		DoLighting = 0x2000,
		DoCubeMapping = 0x4000,
		DisableShaders = 0x8000,
		ShowHidden = 0x10000
	*/

	ui->aShowAxes->setData( Scene::ShowAxes );
	ui->aShowGrid->setData( Scene::ShowGrid );
	ui->aShowNodes->setData( Scene::ShowNodes );
	ui->aShowCollision->setData( Scene::ShowCollision );
	ui->aShowConstraints->setData( Scene::ShowConstraints );
	ui->aShowMarkers->setData( Scene::ShowMarkers );
	ui->aShowHidden->setData( Scene::ShowHidden );
	ui->aDoSkinning->setData( Scene::DoSkinning );

	ui->aTextures->setData( Scene::DoTexturing );
	ui->aVertexColors->setData( Scene::DoVertexColors );
	ui->aSpecular->setData( Scene::DoSpecular );
	ui->aGlow->setData( Scene::DoGlow );
	ui->aCubeMapping->setData( Scene::DoCubeMapping );
	ui->aLighting->setData( Scene::DoLighting );
	ui->aDisableShading->setData( Scene::DisableShaders );

	ui->aSelectObject->setData( Scene::SelObject );
	ui->aSelectVertex->setData( Scene::SelVertex );

	auto agroup = [this]( QVector<QAction *> actions, bool exclusive ) {
		QActionGroup * ag = new QActionGroup( this );
		for ( auto a : actions ) {
			ag->addAction( a );
		}

		ag->setExclusive( exclusive );

		return ag;
	};

	 auto faceAction=new QAction(tr("Faces"),this);
 faceAction->setObjectName("aSelectFace");faceAction->setCheckable(true);faceAction->setData(Scene::SelVertex);
 faceAction->setToolTip(tr("Select triangles: click nearest face, drag for X-ray box selection. Shift adds; Ctrl toggles. Move transforms shared vertices. Delete removes selected triangles."));
 ui->tRender->insertAction(ui->aViewTop,faceAction);ui->mRender->addAction(faceAction);
 selectActions = agroup( { ui->aSelectObject, ui->aSelectVertex, faceAction }, true );
 auto deleteFaces=new QAction(tr("Delete Selection"),this);deleteFaces->setObjectName("aDeleteFaces");
 deleteFaces->setToolTip(tr("Delete selected faces, or selected vertices and their connected faces (Delete)."));
 ui->tRender->insertAction(ui->aViewTop,deleteFaces);ui->mRender->addAction(deleteFaces);
 connect(deleteFaces,&QAction::triggered,this,[this]{if(ogl->faceSelection)ogl->deleteSelectedFaces();else ogl->deleteSelectedVertices();});
 auto xray=new QAction(tr("X-Ray Selection\tShift+X"),this);xray->setObjectName("aEditXRay");xray->setCheckable(true);xray->setChecked(true);
 xray->setToolTip(tr("Shift+X: Show and select through geometry in vertex and face modes. Turn off to show and select only visible geometry."));
 ui->mRender->addAction(xray);connect(xray,&QAction::toggled,ogl,&GLView::setEditXRay);
 connect(ogl,&GLView::editXRayChanged,xray,&QAction::setChecked);
 connect(ogl,&GLView::selectionModeCycled,this,[this,faceAction](int mode){
  (mode==0?ui->aSelectObject:mode==1?ui->aSelectVertex:faceAction)->setChecked(true);
 });
  auto newMaterial=new QAction(tr("New Material"),this);newMaterial->setObjectName("aNewMaterial");
 newMaterial->setToolTip(tr("Give the selected mesh a new default material. Existing texture settings are copied for independent editing."));
 ui->tRender->insertAction(ui->aViewTop,newMaterial);ui->mRender->addAction(newMaterial);
 connect(newMaterial,&QAction::triggered,this,[this]{auto spell=SpellBook::lookup("Mesh/New Material");if(spell)spell->castIfApplicable(nif,ogl->getScene()->currentBlock);});
 auto uniqueMaterial=new QAction(tr("Make Material Unique"),this);uniqueMaterial->setObjectName("aMakeMaterialUnique");
 uniqueMaterial->setToolTip(tr("Copy this mesh's material and texture properties, give the material a new numbered name, and keep the original mesh unchanged. Creates a material if none exists."));
 ui->tRender->insertAction(ui->aViewTop,uniqueMaterial);ui->mRender->addAction(uniqueMaterial);
 connect(uniqueMaterial,&QAction::triggered,this,[this]{auto spell=SpellBook::lookup("Mesh/Make Material Unique");if(spell)spell->castIfApplicable(nif,ogl->getScene()->currentBlock);});
	connect( selectActions, &QActionGroup::triggered, this, [this](QAction * action) {
		if(action->objectName()=="aSelectFace")ogl->setFaceSelectionMode();else ogl->setEditSelectionMode(Scene::SelMode(action->data().toInt()));
	} );
	ui->aSelectVertex->setToolTip(tr("Edit the mesh selected in Object mode. LMB drag selects through the mesh; Shift adds, Ctrl toggles. Space+LMB pans; Alt+LMB orbits."));

	auto gizmoActions = new QActionGroup( this );
	gizmoActions->setExclusive( true );
	const QStringList gizmoNames = { tr( "Select" ), tr( "Move" ), tr( "Rotate" ), tr( "Scale" ) };
	const QStringList gizmoTips = {
		tr( "Select blocks and orbit the camera" ),
		tr( "Drag an arrow to move along one local axis, or a square to move across two axes. Escape cancels. Works in Walk mode." ),
		tr( "Drag a colored ring to rotate around a local axis. Escape cancels." ),
		tr( "Drag the center handle to scale uniformly. Escape cancels." )
	};
	for ( int i = 0; i < gizmoNames.size(); ++i ) {
		auto action = new QAction( gizmoNames[i], this );
		ui->tRender->insertAction( ui->aViewTop, action );
		action->setObjectName( QString("aGizmo%1").arg(i) );
		action->setCheckable( true );
		action->setToolTip( gizmoTips[i] );
		action->setData( i );
		gizmoActions->addAction( action );
		action->setChecked( i == 0 );
	}
	connect(ogl,&GLView::gizmoModeChanged,this,[gizmoActions](int mode){for(auto action:gizmoActions->actions())action->setChecked(action->data().toInt()==mode);});
	ui->tRender->insertSeparator( ui->aViewTop );
	auto transformMenu=ui->mRender->addMenu(tr("Transform"));transformMenu->setObjectName("mTransformTools");transformMenu->addActions(gizmoActions->actions());
	transformMenu->addSeparator();auto numeric=new QAction(tr("Numeric Transform... (N)"),this);numeric->setObjectName("aNumericTransform");transformMenu->addAction(numeric);connect(numeric,&QAction::triggered,ogl,&GLView::showNumericTransform);
	connect( gizmoActions, &QActionGroup::triggered, this, [this]( QAction * action ) {
		if ( action->data().toInt() != GLView::GizmoSelect && ogl->getScene()->selMode != Scene::SelVertex ) ui->aSelectObject->trigger();
		ogl->setGizmoMode( GLView::GizmoMode( action->data().toInt() ) );
	} );
	auto snapVertices=new QAction(tr("Snap Vertices"),this);
	snapVertices->setObjectName("aSnapVertices");
	snapVertices->setCheckable(true);
	snapVertices->setToolTip(tr("During a vertex Move drag, snap the selection's nearest anchor vertex to the active-axis coordinates of another vertex within 14 pixels of the pointer. Other axes stay fixed."));
	ui->tRender->insertAction(ui->aViewTop,snapVertices);
	ui->mRender->addAction(snapVertices);
	connect(snapVertices,&QAction::toggled,ogl,&GLView::setVertexSnap);
	auto meshPivot=new QAction(tr("Mesh Center Pivot"),this);
	meshPivot->setObjectName("aMeshCenterPivot");meshPivot->setCheckable(true);
	meshPivot->setToolTip(tr("Temporarily place the object gizmo at the mesh's bounding-box center. Rotate and scale around that center. Turn off to use the stored origin. Vertex mode uses the selected vertices' center."));
	ui->tRender->insertAction(ui->aViewTop,meshPivot);ui->mRender->addAction(meshPivot);
	connect(meshPivot,&QAction::toggled,ogl,&GLView::setMeshCenterPivot);
	auto uvEditor=new QAction(tr("UV Editor"),this);uvEditor->setObjectName("aUVEditor");
	uvEditor->setToolTip(tr("Open the UV editor for the mesh selected in the 3D view or Block List."));
	ui->tRender->insertAction(ui->aViewTop,uvEditor);ui->mRender->addAction(uvEditor);
	 connect(uvEditor,&QAction::triggered,this,[this](){
  UVWidget::createEditor(nif,ogl->getScene()->currentBlock,this);
 });
	auto rebuildCollision=new QAction(tr("Rebuild Collision"),this);
	rebuildCollision->setObjectName("aRebuildCollision");
	rebuildCollision->setToolTip(tr("Update static PhysX triangle collision from the matching source meshes. Edit the source meshes first, then rebuild and save. This is undoable."));
	ui->tRender->insertAction(ui->aViewTop,rebuildCollision);
	ui->mRender->addAction(rebuildCollision);
	auto updateCollisionAction=[this,rebuildCollision](){rebuildCollision->setEnabled(PhysXCollision::hasCollision(nif));};
	connect(nif,&NifModel::modelReset,this,updateCollisionAction);
	connect(nif,&NifModel::linksChanged,this,updateCollisionAction);
	connect(this,&NifSkope::activeDocumentChanged,this,updateCollisionAction);
	updateCollisionAction();
	connect(rebuildCollision,&QAction::triggered,this,[this](){
		QVector<PhysXCollision::Mesh> meshes;QString error;
		if(!PhysXCollision::collect(nif,meshes,error)){QMessageBox::warning(this,tr("Rebuild Collision"),error);return;}
		QSettings settings;
		QString dll=settings.value("PhysX/Cooking DLL").toString();
		if(!QFileInfo::exists(dll)) {
			dll=QFileDialog::getOpenFileName(this,tr("Select MicroVolts Bin/PhysXCooking.dll"),QString(),tr("PhysX Cooking library (PhysXCooking.dll)"));
			if(dll.isEmpty())return;
		}
		QApplication::setOverrideCursor(Qt::WaitCursor);
		int count=0;
		bool ok=PhysXCollision::rebuild(nif,dll,QCoreApplication::applicationDirPath()+"/PhysxCooker.exe",error,&count);
		QApplication::restoreOverrideCursor();
		if(!ok){QMessageBox::warning(this,tr("Rebuild Collision"),error);return;}
		settings.setValue("PhysX/Cooking DLL",dll);
		if(!ui->aShowCollision->isChecked())ui->aShowCollision->trigger();
		int sourceCount=0;QStringList sourceGroups;
		for(const auto & mesh:meshes){sourceCount+=mesh.sourceMeshes.size();if(mesh.sourceMeshes.size()>1)sourceGroups<<tr("%1 (%2 meshes)").arg(mesh.name).arg(mesh.sourceMeshes.size());}
		QString groups=sourceGroups.isEmpty()?QString():tr(" Groups: %1.").arg(sourceGroups.join(", "));
		statusBar()->showMessage(tr("Rebuilt %1 colliders from %2 source meshes in %3.%4 Save this NIF to keep the changes. Ctrl+Z undoes the rebuild.").arg(count).arg(sourceCount).arg(QFileInfo(currentFile).fileName(),groups),20000);
	});

    // Keep the underlying actions for shortcuts/menus; the toolbar exposes
    // cycling mode buttons and flyouts instead of a row of individual actions.
    auto editorIcon=[](const char * name){return QIcon(QString(":/edit/%1").arg(name));};
    ui->aSelectObject->setIcon(editorIcon("object"));ui->aSelectVertex->setIcon(editorIcon("vertex"));faceAction->setIcon(editorIcon("face"));
    const char * toolIcons[]={"select","move","rotate","scale"};
    for(int i=0;i<gizmoActions->actions().size();++i)gizmoActions->actions()[i]->setIcon(editorIcon(toolIcons[i]));
    deleteFaces->setIcon(editorIcon("delete"));snapVertices->setIcon(editorIcon("snap"));meshPivot->setIcon(editorIcon("pivot"));xray->setIcon(editorIcon("xray"));
    newMaterial->setIcon(editorIcon("new-material"));uniqueMaterial->setIcon(editorIcon("unique-material"));
    uvEditor->setIcon(editorIcon("uv-editor"));rebuildCollision->setIcon(editorIcon("rebuild-collision"));
    gizmoActions->actions()[0]->setToolTip(tr("Select tool (V). Hide the transform gizmo."));
    gizmoActions->actions()[1]->setToolTip(tr("Move tool (W). Drag an axis or plane handle; Escape cancels."));
    gizmoActions->actions()[2]->setToolTip(tr("Rotate tool (E). Drag an axis ring; Escape cancels."));
    gizmoActions->actions()[3]->setToolTip(tr("Scale tool (R). Drag the center handle; Escape cancels."));
    for(auto action:ui->tRender->actions()){if(action==ui->aViewTop)break;ui->tRender->removeAction(action);}
    auto selectionMenu=new QMenu(tr("Selection Mode"),this);selectionMenu->setObjectName("mSelectionModes");selectionMenu->addActions(selectActions->actions());
    ui->mRender->removeAction(ui->aSelectObject);ui->mRender->removeAction(ui->aSelectVertex);ui->mRender->removeAction(faceAction);
    ui->mRender->insertMenu(transformMenu->menuAction(),selectionMenu);
    auto meshTools=new QMenu(tr("Mesh Tools"),this);meshTools->setObjectName("mMeshTools");
        auto worldAxesAction=new QAction(tr("World Axes"),this);worldAxesAction->setObjectName("aWorldAxes");worldAxesAction->setCheckable(true);connect(worldAxesAction,&QAction::toggled,ogl,&GLView::setWorldAxes);meshTools->addAction(worldAxesAction);
    auto isolate=new QAction(tr("Isolate Selected Mesh (Shift+H)"),this);isolate->setObjectName("aIsolateSelection");isolate->setCheckable(true);connect(isolate,&QAction::toggled,ogl,&GLView::setIsolateSelection);connect(ogl,&GLView::isolationChanged,this,[isolate](bool enabled){QSignalBlocker blocker(isolate);isolate->setChecked(enabled);});meshTools->addAction(isolate);
    meshTools->addAction(snapVertices);meshTools->addAction(meshPivot);meshTools->addAction(xray);meshTools->addSeparator();meshTools->addAction(deleteFaces);
    auto materials=new QMenu(tr("Materials"),this);materials->setObjectName("mMaterials");materials->addAction(newMaterial);materials->addAction(uniqueMaterial);
    auto materialEditor=new QAction(tr("Material Editor..."),this);materialEditor->setObjectName("aMaterialEditor");materials->addAction(materialEditor);connect(materialEditor,&QAction::triggered,this,[this]{if(auto dock=findChild<QDockWidget *>("MaterialEditorDock")){dock->show();dock->raise();}});
    for(auto action:{deleteFaces,snapVertices,meshPivot,xray,newMaterial,uniqueMaterial})ui->mRender->removeAction(action);
    ui->mRender->addMenu(meshTools);ui->mRender->addMenu(materials);
    auto toolButton=[this](const QString & name){
        auto button=new QToolButton(ui->tRender);button->setObjectName(name);button->setProperty("compactTool",true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);button->setFocusPolicy(Qt::NoFocus);button->setIconSize(ui->tRender->iconSize());
        connect(ui->tRender,&QToolBar::iconSizeChanged,button,&QToolButton::setIconSize);
        auto action=ui->tRender->insertWidget(ui->aViewTop,button);action->setObjectName(name+"Action");return button;
    };
    auto cyclingButton=[this,toolButton](const QString & name,const QString & label,QActionGroup * group,QMenu * menu,const QString & shortcut){
        auto button=toolButton(name);button->setMenu(menu);button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setProperty("cyclingTool",true);
        // Keep a dedicated arrow area, even when changing icons or hover state
        // causes the stylesheet to recalculate the button's size hint.
        auto sizeButton=[button](const QSize & size){button->setFixedSize(size.width()+24,size.height()+4);};
        connect(ui->tRender,&QToolBar::iconSizeChanged,button,sizeButton);
        sizeButton(ui->tRender->iconSize());
        auto sync=[button,group,label,shortcut]{
            for(auto action:group->actions())if(action->isChecked()){
                button->setIcon(action->icon());button->setText(action->text());button->setAccessibleName(label+": "+action->text());
                button->setToolTip(tr("%1: %2\nClick to cycle; arrow opens choices. %3").arg(label,action->text(),shortcut));break;
            }
        };
        for(auto action:group->actions())connect(action,&QAction::changed,button,sync);
        connect(button,&QToolButton::clicked,this,[group]{
            auto actions=group->actions();int active=actions.indexOf(group->checkedAction());actions[(active+1)%actions.size()]->trigger();
        });
        sync();return button;
    };
    cyclingButton("selectionModeButton",tr("Selection mode"),selectActions,selectionMenu,tr("Tab cycles modes."));
    cyclingButton("transformToolButton",tr("Transform tool"),gizmoActions,transformMenu,tr("V Select | W Move | E Rotate | R Scale"));
    auto meshButton=toolButton("meshToolsButton");meshButton->setMenu(meshTools);meshButton->setPopupMode(QToolButton::InstantPopup);meshButton->setIcon(editorIcon("mesh-tools"));meshButton->setText(tr("Mesh Tools"));meshButton->setToolTip(tr("Mesh Tools: delete selection, vertex snapping, center pivot and X-ray selection."));
    auto materialButton=toolButton("materialsButton");materialButton->setMenu(materials);materialButton->setPopupMode(QToolButton::InstantPopup);materialButton->setIcon(editorIcon("materials"));materialButton->setText(tr("Materials"));materialButton->setToolTip(tr("New Material / Make Material Unique"));
    ui->tRender->insertAction(ui->aViewTop,uvEditor);ui->tRender->insertAction(ui->aViewTop,rebuildCollision);ui->tRender->insertSeparator(ui->aViewTop);
	showActions = agroup( { ui->aShowAxes, ui->aShowGrid, ui->aShowNodes, ui->aShowCollision,
						  ui->aShowConstraints, ui->aShowMarkers, ui->aShowHidden, ui->aDoSkinning
	}, false );
	connect( showActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );
	connect( showActions, &QActionGroup::triggered, ogl, &GLView::updateScene );

	shadingActions = agroup( { ui->aTextures, ui->aVertexColors, ui->aSpecular, ui->aGlow, ui->aCubeMapping, ui->aLighting, ui->aDisableShading }, false );
	connect( shadingActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );
	connect( shadingActions, &QActionGroup::triggered, ogl, &GLView::updateScene );

	auto testActions = agroup( { ui->aTest1Dbg, ui->aTest2Dbg, ui->aTest3Dbg }, true );
	connect( testActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );

	// Sync actions to Scene state
	for ( auto a : showActions->actions() ) {
		a->setChecked( ogl->scene->options & a->data().toInt() );
	}

	// Sync actions to Scene state
	for ( auto a : shadingActions->actions() ) {
		a->setChecked( ogl->scene->options & a->data().toInt() );
	}

	// Setup blank QActions for Recent Files menus
	for ( int i = 0; i < NumRecentFiles; ++i ) {
		recentFileActs[i] = new QAction( this );
		recentArchiveActs[i] = new QAction( this );
		recentArchiveFileActs[i] = new QAction( this );

		recentFileActs[i]->setVisible( false );
		recentArchiveActs[i]->setVisible( false );
		recentArchiveFileActs[i]->setVisible( false );

		connect( recentFileActs[i], &QAction::triggered, this, &NifSkope::openRecentFile );
		connect( recentArchiveActs[i], &QAction::triggered, this, &NifSkope::openRecentArchive );
		connect( recentArchiveFileActs[i], &QAction::triggered, this, &NifSkope::openRecentArchiveFile );
	}

	aList->setChecked( list->model() == nif );
	aHierarchy->setChecked( list->model() == proxy );

	// Allow only List or Tree view to be selected at once
	gListMode = new QActionGroup( this );
	gListMode->addAction( aList );
	gListMode->addAction( aHierarchy );
	gListMode->setExclusive( true );
	connect( gListMode, &QActionGroup::triggered, this, &NifSkope::setListMode );

	connect( aCondition, &QAction::toggled, tree, &NifTreeView::setRowHiding );
	connect( aCondition, &QAction::toggled, kfmtree, &NifTreeView::setRowHiding );

	connect( ui->aAboutNifSkope, &QAction::triggered, []() {
		auto aboutDialog = new AboutDialog();
		aboutDialog->show();
	} );
	connect( ui->aAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt );

	connect( ui->aPrintView, &QAction::triggered, ogl, &GLView::saveImage );

#ifdef QT_NO_DEBUG
	ui->aColorKeyDebug->setDisabled( true );
	ui->aColorKeyDebug->setVisible( false );
	ui->aBoundsDebug->setDisabled( true );
	ui->aBoundsDebug->setVisible( false );
#else
	QAction * debugNone = new QAction( this );

	QActionGroup * debugActions = agroup( { debugNone, ui->aColorKeyDebug, ui->aBoundsDebug }, false );
	connect( ui->aColorKeyDebug, &QAction::triggered, [this]( bool checked ) {
		if ( checked )
			ogl->setDebugMode( GLView::DbgColorPicker );
		else
			ogl->setDebugMode( GLView::DbgNone );
		
		ogl->update();
	} );

	connect( ui->aBoundsDebug, &QAction::triggered, [this]( bool checked ) {
		if ( checked )
			ogl->setDebugMode( GLView::DbgBounds );
		else
			ogl->setDebugMode( GLView::DbgNone );

		ogl->update();
	} );

	connect( debugActions, &QActionGroup::triggered, [=]( QAction * action ) {
		for ( auto a : debugActions->actions() ) {
			if ( a == action )
				continue;

			a->setChecked( false );
		}
	} );
#endif

	connect( ui->aSilhouette, &QAction::triggered, [this]( bool checked ) {
		ogl->setVisMode( Scene::VisSilhouette, checked );
		ogl->updateScene();
	} );

	connect( ui->aVisNormals, &QAction::triggered, [this]( bool checked ) {
		ogl->setVisMode( Scene::VisNormalsOnly, checked );
	} );

	connect( ogl, &GLView::clicked, this, &NifSkope::select );
	connect( ogl, &GLView::sceneTimeChanged, inspect, &InspectView::updateTime );
	connect( ogl, &GLView::paintUpdate, inspect, &InspectView::refresh );
	connect( ogl, &GLView::viewpointChanged, [this]() {
		ui->aViewTop->setChecked( false );
		ui->aViewFront->setChecked( false );
		ui->aViewLeft->setChecked( false );
		ui->aViewUser->setChecked( false );

		ogl->setOrientation( GLView::ViewDefault, false );
	} );

	connect( ogl, &GLView::viewportContextMenuRequested, this, &NifSkope::contextMenu );

	// Update Inspector widget with current index
	connect( tree, &NifTreeView::sigCurrentIndexChanged, inspect, &InspectView::updateSelection );
}

void NifSkope::initDockWidgets()
{
	dRefr = ui->RefrDock;
	dList = ui->ListDock;
	dTree = ui->TreeDock;
	dHeader = ui->HeaderDock;
	dInsp = ui->InspectDock;
	dKfm = ui->KfmDock;
	dBrowser = ui->BrowserDock;

	// Tabify List and Header
	tabifyDockWidget( dList, dHeader );
	tabifyDockWidget( dHeader, dBrowser );

	// Raise List above Header
	dList->raise();

	// Hide certain docks by default
	dRefr->toggleViewAction()->setChecked( false );
	dInsp->toggleViewAction()->setChecked( false );
	dKfm->toggleViewAction()->setChecked( false );

	dRefr->setVisible( false );
	dInsp->setVisible( false );
	dKfm->setVisible( false );

	// Set Inspect widget
	dInsp->setWidget( inspect );

	connect( dList->toggleViewAction(), &QAction::triggered, tree, &NifTreeView::clearRootIndex );
 auto materialDock=new QDockWidget(tr("Materials"),this);materialDock->setObjectName("MaterialEditorDock");auto panel=new MaterialPanel(materialDock);materialDock->setWidget(panel);addDockWidget(Qt::RightDockWidgetArea,materialDock);materialDock->hide();
 auto updateMaterialTarget=[this,panel]{panel->setTarget(nif,ogl->getScene()->currentBlock);};
 connect(ogl,&GLView::currentBlockChanged,panel,updateMaterialTarget);connect(this,&NifSkope::activeDocumentChanged,panel,updateMaterialTarget);connect(ogl,&GLView::clicked,panel,updateMaterialTarget);updateMaterialTarget();
 auto roomDock=new QDockWidget(tr("Rooms"),this);roomDock->setObjectName("RoomEditorDock");auto roomPanel=new RoomPanel(roomDock);roomDock->setWidget(roomPanel);addDockWidget(Qt::RightDockWidgetArea,roomDock);roomDock->hide();
 auto updateRoomTarget=[this,roomPanel]{roomPanel->setTarget(nif,ogl->getScene()->currentBlock);roomPanel->setMeshSelection(nif,ogl->getScene()->selMode==Scene::SelVertex?QModelIndex(ogl->editMesh):QModelIndex(),ogl->getScene()->selMode==Scene::SelVertex?ogl->selectedVertices:QSet<int>());};
 connect(ogl,&GLView::currentBlockChanged,roomPanel,updateRoomTarget);connect(ogl,&GLView::clicked,roomPanel,updateRoomTarget);connect(this,&NifSkope::activeDocumentChanged,roomPanel,updateRoomTarget);connect(roomDock,&QDockWidget::visibilityChanged,roomPanel,[updateRoomTarget](bool visible){if(visible)updateRoomTarget();});connect(roomPanel,&RoomPanel::overlayChanged,ogl,&GLView::setRoomOverlay);updateRoomTarget();
 connect(ogl,&GLView::editSelectionChanged,roomPanel,updateRoomTarget,Qt::QueuedConnection);connect(roomPanel,&RoomPanel::portalPreviewChanged,ogl,&GLView::setPortalPreview);connect(roomPanel,&RoomPanel::portalHighlightChanged,ogl,&GLView::setPortalHighlight);
 auto roomsAction=new QAction(tr("Rooms..."),this);roomsAction->setObjectName("aRoomEditor");ui->mRender->addAction(roomsAction);connect(roomsAction,&QAction::triggered,roomDock,[roomDock,updateRoomTarget]{roomDock->show();roomDock->raise();updateRoomTarget();});

}

void NifSkope::initMenu()
{
	// Disable without NIF loaded
	ui->mRender->setEnabled( false );

	// Populate Toolbars menu with all enabled toolbars
	for ( QObject * o : children() ) {
		QToolBar * tb = qobject_cast<QToolBar *>(o);
		if ( tb && tb->objectName() != "tFile" ) {
			// Do not add tFile to the list
			ui->mToolbars->addAction( tb->toggleViewAction() );
		}
	}

	// Insert SpellBook class before Help
	ui->menubar->insertMenu( ui->menubar->actions().at( 3 ), book.get() );

	// Insert Import/Export menus
	mExport = ui->menuExport;
	mImport = ui->menuImport;

	fillImportExportMenus();
	connect( mExport, &QMenu::triggered, this, &NifSkope::sltImportExport );
	connect( mImport, &QMenu::triggered, this, &NifSkope::sltImportExport );

	// BSA Recent Files
	mRecentArchiveFiles = new QMenu( this );
	mRecentArchiveFiles->setObjectName( "mRecentArchiveFiles" );

	for ( int i = 0; i < NumRecentFiles; ++i ) {
		ui->mRecentFiles->addAction( recentFileActs[i] );
		ui->mRecentArchives->addAction( recentArchiveActs[i] );
		mRecentArchiveFiles->addAction( recentArchiveFileActs[i] );
	}

	// Load & Save
	QMenu * mSave = new QMenu( this );
	mSave->setObjectName( "mSave" );

	mSave->addAction( ui->aSave );
	mSave->addAction( ui->aSaveAs );

	QMenu * mOpen = new QMenu( this );
	mOpen->setObjectName( "mOpen" );

	mOpen->addAction( ui->aOpen );
	mOpen->addAction( ui->aBrowseArchive );

	aRecentFilesSeparator = mOpen->addSeparator();
	for ( int i = 0; i < NumRecentFiles; ++i )
		mOpen->addAction( recentFileActs[i] );

	auto setFlyout = []( QToolButton * btn, QMenu * m ) {
		btn->setObjectName( "btnFlyoutMenu" );
		btn->setMenu( m );
		btn->setPopupMode( QToolButton::InstantPopup );
	};

	// Append Menu to tFile actions
	for ( auto child : ui->tFile->findChildren<QToolButton *>() ) {
		if ( child->defaultAction() == ui->aSaveMenu ) {
			setFlyout( child, mSave );
		} else if ( child->defaultAction() == ui->aOpenMenu ) {
			setFlyout( child, mOpen );
		}
	}

	updateRecentFileActions();
	updateRecentArchiveActions();
	//updateRecentArchiveFileActions();

	// Lighting Menu
	auto mLight = lightingWidget();

	// Append Menu to tRender actions
	for ( auto child : ui->tRender->findChildren<QToolButton *>() ) {

		if ( child->defaultAction() == ui->aLightMenu ) {
			setFlyout( child, mLight );
		} else if(child->objectName().isEmpty()) {
			child->setObjectName( "btnRender" );
		}
	}


	// BSA Recent Archives
	auto tRecentArchives = new QToolButton( this );
	tRecentArchives->setText( "Recent Archives" );
	setFlyout( tRecentArchives, ui->mRecentArchives );

	// BSA Recent Files
	auto tRecentArchiveFiles = new QToolButton( this );
	tRecentArchiveFiles->setText( "Recent Files" );
	setFlyout( tRecentArchiveFiles, mRecentArchiveFiles );

	ui->bsaTitleBar->layout()->addWidget( tRecentArchives );
	ui->bsaTitleBar->layout()->addWidget( tRecentArchiveFiles );


	// Theme Menu

	QActionGroup * grpTheme = new QActionGroup( this );

	// Fill the action data with the integer correlating to 
	// their position in WindowTheme and add to the action group.
	int i = 0;
	auto themes = ui->mTheme->actions();
	for ( auto a : themes ) {
		a->setData( i++ );
		grpTheme->addAction( a );
	}
}


void NifSkope::initToolBars()
{
	// Disable without NIF loaded
	ui->tRender->setEnabled( false );
	ui->tRender->setContextMenuPolicy( Qt::PreventContextMenu );

	// Status Bar
	ui->statusbar->setContentsMargins( 0, 0, 0, 0 );
	ui->statusbar->addPermanentWidget( progress );
	
	// TODO: Split off into own widget
	ui->statusbar->addPermanentWidget( filePathWidget( this ) );


	// Render

	QActionGroup * grpView = new QActionGroup( this );
	grpView->addAction( ui->aViewTop );
	grpView->addAction( ui->aViewFront );
	grpView->addAction( ui->aViewLeft );
	grpView->addAction( ui->aViewWalk );
	grpView->setExclusive( true );


	// Animate
	connect( ui->aAnimate, &QAction::toggled, ui->tAnim, &QToolBar::setVisible );
	connect( ui->tAnim, &QToolBar::visibilityChanged, ui->aAnimate, &QAction::setChecked );

	/*enum AnimationStates
	{
		AnimDisabled = 0x0,
		AnimEnabled = 0x1,
		AnimPlay = 0x2,
		AnimLoop = 0x4,
		AnimSwitch = 0x8
	};*/

	ui->aAnimate->setData( GLView::AnimEnabled );
	ui->aAnimPlay->setData( GLView::AnimPlay );
	ui->aAnimLoop->setData( GLView::AnimLoop );
	ui->aAnimSwitch->setData( GLView::AnimSwitch );

	connect( ui->aAnimate, &QAction::toggled, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimPlay, &QAction::triggered, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimLoop, &QAction::toggled, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimSwitch, &QAction::toggled, ogl, &GLView::updateAnimationState );

	// Animation timeline slider
	auto animSlider = new FloatSlider( Qt::Horizontal, true, true );
	auto animSliderEdit = new FloatEdit( ui->tAnim );

	animSlider->addEditor( animSliderEdit );
	animSlider->setParent( ui->tAnim );
	animSlider->setMinimumWidth( 200 );
	animSlider->setMaximumWidth( 500 );
	animSlider->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::MinimumExpanding );

	connect( ogl, &GLView::sceneTimeChanged, animSlider, &FloatSlider::set );
	connect( ogl, &GLView::sceneTimeChanged, animSliderEdit, &FloatEdit::set );
	connect( animSlider, &FloatSlider::valueChanged, ogl, &GLView::setSceneTime );
	connect( animSlider, &FloatSlider::valueChanged, animSliderEdit, &FloatEdit::setValue );
	connect( animSliderEdit, static_cast<void (FloatEdit::*)(float)>(&FloatEdit::sigEdited), ogl, &GLView::setSceneTime );
	connect( animSliderEdit, static_cast<void (FloatEdit::*)(float)>(&FloatEdit::sigEdited), animSlider, &FloatSlider::setValue );
	
	// Animations
	animGroups = new QComboBox( ui->tAnim );
	animGroups->setMinimumWidth( 60 );
	animGroups->setSizeAdjustPolicy( QComboBox::AdjustToContents );
	animGroups->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Minimum );
	connect( animGroups, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::activated), ogl, &GLView::setSceneSequence );

	ui->tAnim->addWidget( animSlider );
	animGroupsAction = ui->tAnim->addWidget( animGroups );

	connect( ogl, &GLView::sequencesDisabled, ui->tAnim, &QToolBar::hide );
	connect( ogl, &GLView::sequenceStopped, ui->aAnimPlay, &QAction::toggle );
	connect( ogl, &GLView::sequenceChanged, [this]( const QString & seqname ) {
		animGroups->setCurrentIndex( ogl->getScene()->animGroups.indexOf( seqname ) );
	} );
	connect( ogl, &GLView::sequencesUpdated, [this]() {
		ui->tAnim->show();

		animGroups->clear();
		animGroups->addItems( ogl->getScene()->animGroups );
		animGroups->setCurrentIndex( ogl->getScene()->animGroups.indexOf( ogl->getScene()->animGroup ) );

		if ( animGroups->count() == 0 ) {
			animGroupsAction->setVisible( false );
			ui->aAnimSwitch->setVisible( false );
		} else {
			ui->aAnimSwitch->setVisible( animGroups->count() != 1 );
			animGroupsAction->setVisible( true );
			animGroups->adjustSize();
		}
	} );


	// LOD Toolbar
	QToolBar * tLOD = ui->tLOD;

	QSettings settings;
	int lodLevel = settings.value( "GLView/LOD Level", 2 ).toInt();
	settings.setValue( "GLView/LOD Level", lodLevel );

	QSlider * lodSlider = new QSlider( Qt::Horizontal );
	lodSlider->setFocusPolicy( Qt::StrongFocus );
	lodSlider->setTickPosition( QSlider::TicksBelow );
	lodSlider->setTickInterval( 1 );
	lodSlider->setSingleStep( 1 );
	lodSlider->setMinimum( 0 );
	lodSlider->setMaximum( 2 );
	lodSlider->setValue( lodLevel );

	tLOD->addWidget( lodSlider );
	tLOD->setEnabled( false );
	tLOD->setVisible( false );

	connect( lodSlider, &QSlider::valueChanged, ogl->getScene(), &Scene::updateLodLevel );
	connect( lodSlider, &QSlider::valueChanged, ogl, &GLView::updateGL );
}

void NifSkope::initConnections()
{
	connect( nif, &NifModel::beginUpdateHeader, this, &NifSkope::enableUi );

	connect( this, &NifSkope::beginLoading, this, &NifSkope::onLoadBegin );
	connect( this, &NifSkope::beginSave, this, &NifSkope::onSaveBegin );

	connect( this, &NifSkope::completeLoading, this, &NifSkope::onLoadComplete );
	connect( this, &NifSkope::completeSave, this, &NifSkope::onSaveComplete );
}


QMenu * NifSkope::lightingWidget()
{
	QMenu * mLight = new QMenu( this );
	mLight->setObjectName( "mLight" );
	

	auto lightingWidget = new LightingWidget( ogl, mLight );
	lightingWidget->setActions( {ui->aLighting, ui->aTextures, ui->aVertexColors,
								ui->aSpecular, ui->aCubeMapping, ui->aGlow,
								ui->aVisNormals, ui->aSilhouette} );
	auto aLightingWidget = new QWidgetAction( mLight );
	aLightingWidget->setDefaultWidget( lightingWidget );


	mLight->addAction( aLightingWidget );

	return mLight;
}


QWidget * NifSkope::filePathWidget( QWidget * parent )
{
	// Show Filepath of loaded NIF
	auto filepathWidget = new QWidget( parent );
	filepathWidget->setObjectName( "filepathStatusbarWidget" );
	auto filepathLayout = new QHBoxLayout( filepathWidget );
	filepathWidget->setLayout( filepathLayout );
	filepathLayout->setContentsMargins( 0, 0, 0, 0 );
	auto labelFilepath = new QLabel( "", filepathWidget );
	labelFilepath->setMinimumHeight( 16 );

	filepathLayout->addWidget( labelFilepath );

	// Navigate to Filepath
	auto navigateToFilepath = new QPushButton( "", filepathWidget );
	navigateToFilepath->setFlat( true );
	navigateToFilepath->setIcon( QIcon( ":btn/load" ) );
	navigateToFilepath->setIconSize( QSize( 16, 16 ) );
	navigateToFilepath->setStatusTip( tr( "Show in Explorer" ) );

	filepathLayout->addWidget( navigateToFilepath );

	filepathWidget->setVisible( false );

	// Show Filepath on successful NIF load
	connect( this, &NifSkope::completeLoading, [this, filepathWidget, labelFilepath, navigateToFilepath]( bool success, QString & fname ) {
		filepathWidget->setVisible( success );
		labelFilepath->setText( fname );

		if ( QFileInfo( fname ).exists() ) {
			navigateToFilepath->show();
		} else {
			navigateToFilepath->hide();
		}
	} );

	// Change Filepath on successful NIF save
	connect( this, &NifSkope::completeSave, [this, filepathWidget, labelFilepath, navigateToFilepath]( bool success, QString & fname ) {
		filepathWidget->setVisible( success );
		labelFilepath->setText( fname );

		if ( QFileInfo( fname ).exists() ) {
			navigateToFilepath->show();
		} else {
			navigateToFilepath->hide();
		}
	} );

	// Navigate to NIF in Explorer
	connect( navigateToFilepath, &QPushButton::clicked, [this]() {
#ifdef Q_OS_WIN
		QStringList args;
		args << "/select," << QDir::toNativeSeparators( currentFile );
		QProcess::startDetached( "explorer", args );
#endif
	} );


	return filepathWidget;
}


void NifSkope::archiveDlg()
{
	QString file = QFileDialog::getOpenFileName( this, tr( "Open Archive" ), "", "Archives (*.bsa *.ba2)" );
	if ( !file.isEmpty() )
		openArchive( file );
}

void NifSkope::openDlg()
{
	// Grab most recent filepath if blank window
	auto path = nif->getFileInfo().absolutePath();
	path = (path.isEmpty()) ? recentFileActs[0]->data().toString() : path;

	if ( !saveConfirm() )
		return;

	QStringList files = QFileDialog::getOpenFileNames( this, tr( "Open File" ), path, fileFilters() );
	if ( !files.isEmpty() )
		openFiles( files );
}

void NifSkope::onLoadBegin()
{
	nif->suspendUndoHistory();
	// Disconnect the models from the views
	swapModels();

	ogl->setUpdatesEnabled( false );
	ogl->setEnabled( false );
	setEnabled( false );
	ui->tAnim->setEnabled( false );

	ui->tLOD->setEnabled( false );
	ui->tLOD->setVisible( false );

	progress->setVisible( true );
	progress->reset();
}

void NifSkope::onLoadComplete( bool success, QString & fname )
{
	QApplication::restoreOverrideCursor();

	if ( nif && nif->getVersionNumber() >= 0x14050000 ) {
		mExport->setDisabled( true );
		mImport->setDisabled( true );
	} else {
		mExport->setDisabled( false );
		if ( nif->getUserVersion2() >= 100 )
			mImport->setDisabled( true );
		else
			mImport->setDisabled( false );
	}

	// Reconnect the models to the views
	swapModels();
	// Set List vs Tree
	setListMode();

	// Re-enable window
	ogl->setUpdatesEnabled( true );
	ogl->setEnabled( true );
	setEnabled( true ); // IMPORTANT!

	int timeout = 2500;
	if ( success ) {
		// Scroll panel back to top
		tree->scrollTo( nif->index( 0, 0 ) );

		select( nif->getHeader() );

		header->setRootIndex( nif->getHeader() );
		// Refresh the header rows
		header->updateConditions( nif->getHeader().child( 0, 0 ), nif->getHeader().child( 20, 0 ) );

		ogl->setOrientation( GLView::ViewFront );

		enableUi();

	} else {
		// File failed to load
		Message::append( this, NifModel::tr( readFail ), 
						 NifModel::tr( readFailFinal ).arg( fname ), QMessageBox::Critical );

		nif->clear();
		kfm->clear();
		timeout = 0;

		// Remove from Current Files
		clearCurrentFile();

		// Reset
		currentFile.clear();
		setWindowFilePath( "" );
		progress->reset();
	}

	// Mark window as unmodified
	setWindowModified( false );
	if(!documents.isEmpty())documents[activeDocument].path=currentFile;
	nif->resetUndoHistory();
	indexStack->clear();
	updateWorkspaceUi();refreshReferences();

	// Center the model on load
	ogl->center();

	// Hide Progress Bar
	QTimer::singleShot( timeout, progress, SLOT( hide() ) );
}


void NifSkope::saveAsDlg()
{
	QString filename = QFileDialog::getSaveFileName( this, tr( "Save File" ), nif->getFileInfo().absoluteFilePath(),
		fileFilters( false ),
		new QString( fileFilter( nif->getFileInfo().suffix() ) )
	);

	if ( filename.isEmpty() )
		return;

	saveFile( filename );
}

void NifSkope::onSaveBegin()
{
	setEnabled( false );
}

void NifSkope::onSaveComplete( bool success, QString & fname )
{
	setEnabled( true );

	if ( success ) {
		// Update if Save As results in filename change
		nif->flushUndoHistory();
		setWindowFilePath( fname );
		// Mark window as unmodified
		nif->undoStack->setClean();
		setWindowModified( false );
		updateWorkspaceUi();
	}
}

bool NifSkope::saveConfirm()
{
	if(documents.size()>1)return confirmWorkspace();
	nif->flushUndoHistory();
	if ( !cfg.suppressSaveConfirm && (isWindowModified() || !nif->undoStack->isClean()) ) {
		QMessageBox::StandardButton response;
		response = QMessageBox::question( this,
			tr( "Save Confirmation" ),
			tr( "<h3><b>You have unsaved changes to %1.</b></h3>Would you like to save them now?" ).arg( nif->getFileInfo().completeBaseName() ),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No );

		if ( response == QMessageBox::Yes ) {
			saveAsDlg();
			return nif->undoStack->isClean() && !isWindowModified();
		} else if ( response == QMessageBox::No ) {
			return true;
		} else if ( response == QMessageBox::Cancel ) {
			return false;
		}
	}

	return true;
}

void NifSkope::enableUi()
{
	// Re-enable toolbars, actions, and menus
	ui->aSaveMenu->setEnabled( true );
	ui->aSave->setEnabled( true );
	ui->aSaveAs->setEnabled( true );
	ui->aHeader->setEnabled( true );

	ui->mRender->setEnabled( true );
	ui->tAnim->setEnabled( true );
	animGroups->clear();


	ui->tRender->setEnabled( true );

	// We only need to enable the UI once, disconnect
	disconnect( nif, &NifModel::beginUpdateHeader, this, &NifSkope::enableUi );
}

void NifSkope::saveUi() const
{
	QSettings settings;
	// TODO: saveState takes a version number which can be incremented between releases if necessary
	settings.setValue( "Window State"_uip, saveState( 0x073 ) );
	settings.setValue( "Window Geometry"_uip, saveGeometry() );

	settings.setValue( "Theme", theme );

	settings.setValue( "File/Auto Sanitize", aSanitize->isChecked() );

	settings.setValue( "List Mode"_uip, (gListMode->checkedAction() == aList ? "list" : "hierarchy") );
	settings.setValue( "Show Non-applicable Rows"_uip, aCondition->isChecked() );

	settings.setValue( "List Header"_uip, list->header()->saveState() );
	settings.setValue( "Tree Header"_uip, tree->header()->saveState() );
	settings.setValue( "Header Header"_uip, header->header()->saveState() );
	settings.setValue( "Kfmtree Header"_uip, kfmtree->header()->saveState() );

	settings.setValue( "GLView/Enable Animations", ui->aAnimate->isChecked() );
	//settings.setValue( "GLView/Play Animation", ui->aAnimPlay->isChecked() );
	//settings.setValue( "GLView/Loop Animation", ui->aAnimLoop->isChecked() );
	//settings.setValue( "GLView/Switch Animation", ui->aAnimSwitch->isChecked() );
	settings.setValue( "GLView/Perspective", ui->aViewPerspective->isChecked() );
}


void NifSkope::restoreUi()
{
	QSettings settings;
	restoreGeometry( settings.value( "Window Geometry"_uip ).toByteArray() );
	restoreState( settings.value( "Window State"_uip ).toByteArray(), 0x073 );

	aSanitize->setChecked( settings.value( "File/Auto Sanitize", true ).toBool() );

	if ( settings.value( "List Mode"_uip, "hierarchy" ).toString() == "list" )
		aList->setChecked( true );
	else
		aHierarchy->setChecked( true );

	setListMode();

	aCondition->setChecked( settings.value( "Show Non-applicable Rows"_uip, false ).toBool() );

	list->header()->restoreState( settings.value( "List Header"_uip ).toByteArray() );
	tree->header()->restoreState( settings.value( "Tree Header"_uip ).toByteArray() );
	header->header()->restoreState( settings.value( "Header Header"_uip ).toByteArray() );
	kfmtree->header()->restoreState( settings.value( "Kfmtree Header"_uip ).toByteArray() );

	auto hideSections = []( NifTreeView * tree, bool hidden ) {
		tree->header()->setSectionHidden( NifModel::ArgCol, hidden );
		tree->header()->setSectionHidden( NifModel::Arr1Col, hidden );
		tree->header()->setSectionHidden( NifModel::Arr2Col, hidden );
		tree->header()->setSectionHidden( NifModel::CondCol, hidden );
		tree->header()->setSectionHidden( NifModel::Ver1Col, hidden );
		tree->header()->setSectionHidden( NifModel::Ver2Col, hidden );
		tree->header()->setSectionHidden( NifModel::VerCondCol, hidden );
	};

	// Hide advanced metadata loaded from nif.xml as it's not useful or necessary for editing
	if ( settings.value( "Settings/Nif/Hide metadata columns", true ).toBool() ) {
		hideSections( tree, true );
		hideSections( header, true );
	} else {
		// Unhide here, or header()->restoreState() will keep them perpetually hidden
		hideSections( tree, false );
		hideSections( header, false );
	}

	ui->aAnimate->setChecked( settings.value( "GLView/Enable Animations", true ).toBool() );
	//ui->aAnimPlay->setChecked( settings.value( "GLView/Play Animation", true ).toBool() );
	//ui->aAnimLoop->setChecked( settings.value( "GLView/Loop Animation", true ).toBool() );
	//ui->aAnimSwitch->setChecked( settings.value( "GLView/Switch Animation", true ).toBool() );

	auto isPersp = settings.value( "GLView/Perspective", true ).toBool();
	ui->aViewPerspective->setChecked( isPersp );

	ogl->setProjection( isPersp );

	QVariant fontVar = settings.value( "UI/View Font" );

	if ( fontVar.canConvert<QFont>() )
		setViewFont( fontVar.value<QFont>() );

	// Modify UI settings that cannot be set in Designer
	tabifyDockWidget( ui->InspectDock, ui->KfmDock );
}

void NifSkope::setViewFont( const QFont & font )
{
	list->setFont( font );
	QFontMetrics metrics( list->font() );
	list->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	tree->setFont( font );
	tree->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	header->setFont( font );
	header->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	kfmtree->setFont( font );
	kfmtree->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	ogl->setFont( font );
}

void NifSkope::reloadTheme()
{
	for ( QWidget * widget : QApplication::topLevelWidgets() ) {
		NifSkope * win = qobject_cast<NifSkope *>(widget);
		if ( win ) {
			win->loadTheme();
		}
	}
}

void NifSkope::loadTheme()
{
	QSettings settings;
	theme = WindowTheme( settings.value( "Theme", ThemeDark ).toInt() );
	ui->mTheme->actions()[theme]->setChecked( true );

	toolbarSize = ToolbarSize( settings.value( "Settings/Theme/Large Icons", ToolbarLarge ).toBool() );

	//setThemeActions();
	setToolbarSize();

	switch ( theme )
	{
	case ThemeWindowsXP:
		QApplication::setStyle( QStyleFactory::create( "WindowsXP" ) );
		qApp->setStyleSheet("");
		qApp->setPalette( style()->standardPalette() );
		return;
	case ThemeWindows:
		QApplication::setStyle( QStyleFactory::create( "WindowsVista" ) );
		qApp->setStyleSheet("");
		qApp->setPalette( style()->standardPalette() );
		return;
	case ThemeDark:
	case ThemeLight:
	default:
		QApplication::setStyle( QStyleFactory::create( "Fusion" ) );
	}

	QPalette pal;
	auto baseC = settings.value( "Settings/Theme/Base Color", defaultsDark[Base] ).value<QColor>();
	auto baseCAlt = settings.value( "Settings/Theme/Base Color Alt", defaultsDark[BaseAlt] ).value<QColor>();
	auto baseCTxt = settings.value( "Settings/Theme/Text", defaultsDark[Text] ).value<QColor>();
	auto baseCHighlight = settings.value( "Settings/Theme/Highlight", defaultsDark[Highlight] ).value<QColor>();
	auto baseCTxtHighlight = settings.value( "Settings/Theme/Highlight Text", defaultsDark[HighlightText] ).value<QColor>();
	auto baseCBrightTxt = settings.value( "Settings/Theme/Bright Text", defaultsDark[BrightText] ).value<QColor>();

	// Fill the standard palette
	pal.setColor( QPalette::Window, baseC );
	pal.setColor( QPalette::WindowText, baseCTxt );
	pal.setColor( QPalette::Base, baseC );
	pal.setColor( QPalette::AlternateBase, baseCAlt );
	pal.setColor( QPalette::ToolTipBase, baseC );
	pal.setColor( QPalette::ToolTipText, baseCTxt );
	pal.setColor( QPalette::Text, baseCTxt );
	pal.setColor( QPalette::Button, baseC );
	pal.setColor( QPalette::ButtonText, baseCTxt );
	pal.setColor( QPalette::BrightText, baseCBrightTxt );
	pal.setColor( QPalette::Link, baseCBrightTxt );
	pal.setColor( QPalette::Highlight, baseCHighlight );
	pal.setColor( QPalette::HighlightedText, baseCTxtHighlight );

	// Mute the disabled palette
	auto baseCDark = baseC.darker( 150 );
	auto baseCAltDark = baseCAlt.darker( 150 );
	auto baseCHighlightDark = QColor( 128, 128, 128 );
	auto baseCTxtDark = Qt::darkGray;
	auto baseCTxtHighlightDark = Qt::darkGray;
	auto baseCBrightTxtDark = Qt::darkGray;

	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Window, baseC ); // Leave base color the same
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::WindowText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Base, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::AlternateBase, baseCAltDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ToolTipBase, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ToolTipText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Text, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Button, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ButtonText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::BrightText, baseCBrightTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Link, baseCBrightTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Highlight, baseCHighlightDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::HighlightedText, baseCTxtHighlightDark );

	// Set Palette and Stylesheet
	
	QDir qssDir( QApplication::applicationDirPath() );
	QStringList qssList( QStringList()
						 << "style.qss"
#ifdef Q_OS_LINUX
						 << "/usr/share/nifskope/style.qss"
#endif
	);
	QString qssName;
	for ( const QString& str : qssList ) {
		if ( qssDir.exists( str ) ) {
			qssName = qssDir.filePath( str );
			break;
		}
	}

	// Load stylesheet
	QString styleData;
	if ( !qssName.isEmpty() ) {
		QFile style( qssName );
		if ( style.open( QFile::ReadOnly ) ) {
			styleData = style.readAll();
			style.close();
		}
	}

	// Remove comments first
	QRegularExpression cssComment( R"regex(\/\*[^*]*\*+([^/*][^*]*\*+)*\/)regex" );
	styleData.replace( cssComment, "" );

	// Theme name for icon path customization
	styleData.replace( "${theme}", (theme == ThemeDark) ? "dark" : "light" );

	// Highlight colors in an "R, G, B" string to combine with opacity in rgba()
	auto rgb = QString("%1, %2, %3").arg(baseCHighlight.red())
									.arg(baseCHighlight.green())
									.arg(baseCHighlight.blue());
	styleData.replace( "${rgb}", rgb );

	qApp->setPalette( pal );
	qApp->setStyleSheet( styleData );
}

void NifSkope::setThemeActions()
{
	// Map of QAction object names to QRC alias
	QMap<QString, QString> names = {
		//{"aTextures", "textures"}
	};

	QString themeString = (theme == ThemeDark) ? "dark" : "light";
	for ( auto a : allActions ) {
		auto obj = a->objectName();
		if ( names.contains( obj ) ) {
			a->setIcon( QIcon( QString(":btn/%1/%2").arg(themeString).arg(names[obj]) ) );
		}
	}
}

void NifSkope::setToolbarSize()
{
	QSize size = {18, 18};
	if ( toolbarSize == ToolbarLarge )
		size = {36, 36};

	for ( QObject * o : children() ) {
		auto tb = qobject_cast<QToolBar *>(o);
		if ( tb )
			tb->setIconSize(size);
	}
}

void NifSkope::setTheme( nstheme::WindowTheme t )
{
	theme = t;

	QSettings settings;
	settings.setValue( "Theme", theme );

	QColor * defaults = nullptr;
	QString iconPrefix;

	// If Dark reset to dark colors and icons
	// If Light reset to light colors and icons
	switch ( t ) {
	case ThemeDark:
		defaults = defaultsDark;
		break;
	case ThemeLight:
		defaults = defaultsLight;
		break;
	default:
		break;
	}

	if ( defaults ) {
		settings.setValue( "Settings/Theme/Base Color", defaults[Base] );
		settings.setValue( "Settings/Theme/Base Color Alt", defaults[BaseAlt] );
		settings.setValue( "Settings/Theme/Text", defaults[Text] );
		settings.setValue( "Settings/Theme/Highlight", defaults[Highlight] );
		settings.setValue( "Settings/Theme/Highlight Text", defaults[HighlightText] );
		settings.setValue( "Settings/Theme/Bright Text", defaults[BrightText] );
	}

	loadTheme();
}

bool NifSkope::eventFilter( QObject * o, QEvent * e )
{
	// TODO: This doesn't seem to be doing anything extra
	//if ( e->type() == QEvent::Polish ) {
	//	QTimer::singleShot( 0, this, SLOT( overrideViewFont() ) );
	//}

	// Global mouse press
	if ( o->isWindowType() && e->type() == QEvent::MouseButtonPress ) {
		//qDebug() << "Mouse Press";
	}
	// Global mouse release
	if ( o->isWindowType() && e->type() == QEvent::MouseButtonRelease ) {
		//qDebug() << "Mouse Release";

		// Back/Forward button support for cycling through indices
		auto mouseEvent = static_cast<QMouseEvent *>(e);
		if ( mouseEvent ) {
			if ( mouseEvent->button() == Qt::ForwardButton ) {
				mouseEvent->accept();
				indexStack->redo();
			}

			if ( mouseEvent->button() == Qt::BackButton ) {
				mouseEvent->accept();
				indexStack->undo();
			}
		}
	}

	// QGraphicsView and QGLWidget must receive their normal resize events.
	return QMainWindow::eventFilter( o, e );
}


/*
* Slots
* **********************
*/


void NifSkope::contextMenu( const QPoint & pos )
{
	QModelIndex idx;
	QPoint p = pos;

	if ( sender() == tree ) {
		idx = tree->indexAt( pos );
		p = tree->mapToGlobal( pos );
	} else if ( sender() == list ) {
		idx = list->indexAt( pos );
		p = list->mapToGlobal( pos );
	} else if ( sender() == header ) {
		idx = header->indexAt( pos );
		p = header->mapToGlobal( pos );
	} else if ( sender() == graphicsView || sender() == ogl ) {
		idx = ogl->getScene()->selMode==Scene::SelVertex?QModelIndex(ogl->getScene()->currentBlock):ogl->indexAt(pos);
		p = ogl->mapToGlobal( pos );
	} else {
		return;
	}

	while ( idx.model() && idx.model()->inherits( "NifProxyModel" ) ) {
		idx = qobject_cast<const NifProxyModel *>(idx.model())->mapTo( idx );
	}

	SpellBook contextBook( nif, idx, this, SLOT( select( const QModelIndex & ) ) );

	if ( !idx.isValid() || nif->flags( idx ) & (Qt::ItemIsEnabled | Qt::ItemIsSelectable) )
		contextBook.exec( p );
}

void NifSkope::overrideViewFont()
{
	QSettings settings;
	QVariant var = settings.value( "UI/View Font" );

	if ( var.canConvert<QFont>() ) {
		setViewFont( var.value<QFont>() );
	}
}


/*
* Automatic Slots
* **********************
*/


void NifSkope::on_aLoadXML_triggered()
{
	NifModel::loadXML();
	KfmModel::loadXML();
}

void NifSkope::on_aReload_triggered()
{
	if ( NifModel::loadXML() ) {
		reload();
	}
}

void NifSkope::on_aSelectFont_triggered()
{
	bool ok;
	QFont fnt = QFontDialog::getFont( &ok, list->font(), this );

	if ( !ok )
		return;

	setViewFont( fnt );
	QSettings settings;
	settings.setValue( "UI/View Font", fnt );
}

void NifSkope::on_aWindow_triggered()
{
	createWindow();
}

void NifSkope::on_aShredder_triggered()
{
	TestShredder::create();
}

void NifSkope::on_aHeader_triggered()
{
	if ( tree )
		tree->clearRootIndex();

	select( nif->getHeader() );
}


void NifSkope::on_tRender_actionTriggered( QAction * action )
{
	Q_UNUSED( action );
}

void NifSkope::on_aViewTop_triggered( bool checked )
{
	if ( checked ) {
		ogl->setOrientation( GLView::ViewTop );
	}
}

void NifSkope::on_aViewFront_triggered( bool checked )
{
	if ( checked ) {
		ogl->setOrientation( GLView::ViewFront );
	}
}

void NifSkope::on_aViewLeft_triggered( bool checked )
{
	if ( checked ) {
		ogl->setOrientation( GLView::ViewLeft );
	}
}

void NifSkope::on_aViewCenter_triggered()
{
	ogl->center();
}

void NifSkope::on_aViewFlip_triggered( bool checked )
{
	Q_UNUSED( checked );
	ogl->flipOrientation();
}

void NifSkope::on_aViewPerspective_toggled( bool checked )
{
	ogl->setProjection( checked );
}

void NifSkope::on_aViewWalk_triggered( bool checked )
{
	if ( checked ) {
		ogl->setOrientation( GLView::ViewWalk );
	}
}


void NifSkope::on_aViewUserSave_triggered( bool checked )
{ 
	Q_UNUSED( checked );
	ogl->saveUserView();
	ui->aViewUser->setChecked( true );
}


void NifSkope::on_aViewUser_toggled( bool checked )
{
	if ( checked ) {
		ogl->setOrientation( GLView::ViewUser, false );
		ogl->loadUserView();
	}
}

void NifSkope::on_aSettings_triggered()
{
	options->show();
	options->raise();
	options->activateWindow();
}

void NifSkope::on_mTheme_triggered( QAction * action )
{
	auto newTheme = WindowTheme( action->data().toInt() );

	setTheme( newTheme );
}

void NifSkope::synchronizeUVEditor(UVWidget * editor)
{
  if(!editor || editor->document()!=nif)return;
  syncedUVEditor=editor;
  if(editor->property("uvSelectionBound").toBool()){if(ogl->getScene()->selMode==Scene::SelVertex && editor->meshIndex()==ogl->editMesh)editor->setViewportSelection(ogl->selectedVertices,ogl->selectedFaces,ogl->faceSelection);return;}
  editor->setProperty("uvSelectionBound",true);
  auto fromViewport=[this,editor]{if(syncedUVEditor!=editor)return;auto target=ogl->getScene()->selMode==Scene::SelVertex?QModelIndex(ogl->editMesh):QModelIndex(ogl->getScene()->currentBlock);if(!editor->followMesh(nif,target))return;if(ogl->getScene()->selMode==Scene::SelVertex)editor->setViewportSelection(ogl->selectedVertices,ogl->selectedFaces,ogl->faceSelection);};
  connect(ogl,&GLView::editSelectionChanged,editor,fromViewport,Qt::QueuedConnection);
connect(ogl,&GLView::currentBlockChanged,editor,fromViewport,Qt::QueuedConnection);connect(ogl,&GLView::clicked,editor,fromViewport,Qt::QueuedConnection);
  connect(editor,&UVWidget::selectionChanged,this,[this,editor]{
   if(syncedUVEditor!=editor || editor->document()!=nif || !editor->meshIndex().isValid())return;
   if(ogl->getScene()->selMode!=Scene::SelVertex){ogl->setCurrentIndex(editor->meshIndex());ui->aSelectVertex->trigger();}
   if(ogl->editMesh!=editor->meshIndex())return;
   if(ogl->faceSelection)ogl->selectedFaces=editor->selectedFaces();else ogl->selectedVertices=editor->selectedVertices();
   ogl->refreshEditData();ogl->gizmoVisible=false;ogl->update();
  });
  fromViewport();
}
