#include "glview.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QTimer>
#include <QAction>

// Optional integration checks require a desktop OpenGL context. They exercise
// actual mouse/key handlers and the projected handles used by the renderer.
class GLViewGizmoTest {
public:
	static void mouse( GLView * view, QEvent::Type type, const QPoint & pos )
	{
		QMouseEvent event(type, pos, Qt::LeftButton,
			type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &event);
	}
	static void run()
	{
		NifModel model;
		model.insertNiBlock("NiNode");
		model.insertNiBlock("NiNode");
		model.set<uint>(model.getBlock(0), "Num Children", 1);
		model.updateArray(model.getIndex(model.getBlock(0), "Children"));
		model.setLink(model.getIndex(model.getBlock(0), "Children").child(0,0), 1);
		model.set<float>(model.getBlock(0), "Scale", 2);
		model.set<Matrix>(model.getBlock(0), "Rotation", Matrix::euler(0,0,.5f));
		model.set<Vector3>(model.getBlock(1), "Translation", Vector3(20,0,0));
		model.resetUndoHistory();
		QGLFormat format;
		format.setVersion(2,1);
		auto view = new GLView(format, nullptr);
		view->timer->stop();
		view->cfg.fov = 45;
		view->cfg.upAxis = GLView::ZAxis;
		view->cfg.background = QColor(30,30,30);
		view->setNif(&model);
		view->setCurrentIndex(model.getBlock(1));
		view->setPosition(0,0,0);
		view->setRotation(25,0,-20);
		view->setDistance(400);
		view->setProjection(true);
		view->view = GLView::ViewUser;
		view->resize(640,480);
		view->show();
		QCoreApplication::processEvents();
		view->resizeGL(640,480);
		view->setGizmoMode(GLView::GizmoMove);
		view->updateGL();
		check(view->gizmoVisible, "render perspective move gizmo");
		view->updateGL();
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath() + "/gizmo-move-preview.png");
		QPoint start = (view->gizmoScreenOrigin*.3 + view->gizmoScreenAxes[0]*.7).toPoint();
		check(view->pickGizmo(start) == 0, "pick rendered X arrow");
		Vector3 camera = view->Rot;
		Transform original(&model, model.getBlock(1));
		mouse(view, QEvent::MouseButtonPress, start);
		check(view->gizmoAxis == 0, "mouse press starts gizmo drag");
		QPoint end = start + (view->gizmoDragDirection * .25).toPoint();
		mouse(view, QEvent::MouseMove, end);
		QPointF pivotBefore = view->gizmoScreenOrigin;
		view->updateGL();
		check(QLineF(pivotBefore, view->gizmoScreenOrigin).length() > 1, "gizmo pivot follows live movement");
		mouse(view, QEvent::MouseButtonRelease, end);
		Transform moved(&model, model.getBlock(1));
		check(moved.translation[0] > original.translation[0] && std::abs(moved.translation[1]) < .001f
			&& std::abs(moved.translation[2]) < .001f, "drag translates along local X under rotated/scaled parent");
		check(view->Rot == camera, "gizmo drag does not orbit camera");
		check(model.undoStack->count() == 1, "mouse drag commits one history entry");
		model.undo();
		check(model.get<Vector3>(model.getBlock(1), "Translation") == original.translation, "undo mouse move");

		view->setCurrentIndex(model.getBlock(1));
		view->setGizmoMode(GLView::GizmoRotate);
		view->updateGL();
		check(view->gizmoVisible, "render rotate gizmo");
		view->updateGL();
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath() + "/gizmo-rotate-preview.png");
		int sample = -1;
		for ( int s = 4; s < 80; ++s ) {
			if ( view->pickGizmo(view->gizmoRings[2][s].toPoint()) == 2 ) { sample = s; break; }
		}
		check(sample >= 0, "pick rendered Z rotation ring");
		start = view->gizmoRings[2][sample].toPoint();
		end = view->gizmoRings[2][sample+12].toPoint();
		mouse(view, QEvent::MouseButtonPress, start);
		check(view->gizmoPlaneRotation, "rotation uses ray-plane intersection");
		mouse(view, QEvent::MouseMove, end);
		mouse(view, QEvent::MouseButtonRelease, end);
		Matrix rotation = model.get<Matrix>(model.getBlock(1), "Rotation");
		Matrix expected = Matrix::euler(0,0,float(3.14159265358979323846/4));
		check(std::abs(rotation(0,0)-expected(0,0)) < .03f && std::abs(rotation(1,0)-expected(1,0)) < .03f,
			"ring drag rotates 45 degrees around local Z");
		model.undo();

		view->setCurrentIndex(model.getBlock(1));
		view->setGizmoMode(GLView::GizmoScale);
		view->updateGL();
		view->updateGL();
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath() + "/gizmo-scale-preview.png");
		start = view->gizmoScreenOrigin.toPoint();
		end = start + QPoint(60,-60);
		mouse(view, QEvent::MouseButtonPress, start);
		mouse(view, QEvent::MouseMove, end);
		mouse(view, QEvent::MouseButtonRelease, end);
		check(std::abs(model.get<float>(model.getBlock(1), "Scale") - std::exp(1.f)) < .001f,
			"center handle scales uniformly");
		model.undo();

		view->setCurrentIndex(model.getBlock(1));
		view->updateGL();
		int historyIndex = model.undoStack->index();
		start = view->gizmoScreenOrigin.toPoint();
		mouse(view, QEvent::MouseButtonPress, start);
		mouse(view, QEvent::MouseMove, start + QPoint(30,-30));
		QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(view, &escape);
		mouse(view, QEvent::MouseButtonRelease, start + QPoint(30,-30));
		check(model.get<float>(model.getBlock(1), "Scale") == 1 && model.undoStack->index() == historyIndex,
			"Escape restores transform without adding history");
		view->updateGL();
		mouse(view, QEvent::MouseButtonPress, view->gizmoScreenOrigin.toPoint());
		mouse(view, QEvent::MouseMove, view->gizmoScreenOrigin.toPoint() + QPoint(20,0));
		QFocusEvent focusOut(QEvent::FocusOut);
		QApplication::sendEvent(view, &focusOut);
		check(model.get<float>(model.getBlock(1), "Scale") == 1 && view->gizmoAxis < 0,
			"losing focus cancels the drag");

		view->setProjection(false);
		view->setGizmoMode(GLView::GizmoMove);
		view->updateGL();
		check(view->gizmoVisible, "render orthographic gizmo");
		view->scene->selMode = Scene::SelVertex;
		view->updateGL();
		check(!view->gizmoVisible, "hide gizmo during vertex selection");
		view->scene->selMode = Scene::SelObject;
		view->animState |= GLView::AnimPlay;
		view->updateGL();
		check(!view->gizmoVisible, "hide gizmo during animation playback");
		view->animState &= ~GLView::AnimEnabled;
		view->updateGL();
		check(view->gizmoVisible, "allow gizmo when animation is disabled");
		if ( QFile::exists("build/qt-check/fixtures/Working.nif") ) {
			view->makeCurrent();
			auto hash = checkNiMeshFile("build/qt-check/fixtures/Working.nif", view->glContext, view->glFuncs);
			check(hash == "6de19da0d6d8d38b53c6f2c8db4dd0220aed5fe7342696300bc0c6485a16caee",
				"working MicroVolts sample geometry is unchanged from the original decoder");
		}
		if ( QFile::exists("build/qt-check/fixtures/NotWorking.nif") ) {
			view->makeCurrent();
			checkNiMeshFile("build/qt-check/fixtures/NotWorking.nif", view->glContext, view->glFuncs);
		}
		for (const auto & name : {QString("Working"),QString("NotWorking")}) {
			QString path="build/qt-check/fixtures/"+name+".nif";
			if(!QFile::exists(path)) continue;
			NifModel fixture;
			check(fixture.loadFromFile(path),"load full-scene render fixture");
			view->setNif(&fixture);
			view->scene->options &= ~Scene::DoTexturing;
			view->scene->animate=false;
			view->animState=GLView::AnimDisabled;
			view->setGizmoMode(GLView::GizmoSelect);
			view->setProjection(true);
			view->setOrientation(GLView::ViewFront);
			view->center();
			view->updateGL();
			view->updateGL();
			auto frame=view->grabFrameBuffer();
			int renderedMeshes=0;
			for(auto node:view->scene->nodes.list()) if(dynamic_cast<Mesh *>(node)) ++renderedMeshes;
			check(!frame.isNull() && renderedMeshes>0,"render complete MicroVolts scene");
			frame.save(QCoreApplication::applicationDirPath()+"/microvolts-"+name+".png");
			view->setNif(nullptr);
		}
		delete view;

		// Exercise the real window's actions and model-reset connections too.
		auto window = new NifSkope;
		auto document = window->findChild<NifModel *>();
		check(document != nullptr, "main window owns a document model");
		document->insertNiBlock("NiNode");
		document->resetUndoHistory();
		window->select(document->getBlock(0));
		document->set<float>(document->getBlock(0), "Scale", 2);
		document->flushUndoHistory();
		check(window->isWindowModified(), "main window marks field edit as unsaved");
		auto undoAction = window->findChild<QAction *>("aUndo");
		auto redoAction = window->findChild<QAction *>("aRedo");
		check(undoAction && undoAction->isEnabled(), "main window enables Undo");
		undoAction->trigger();
		check(document->get<float>(document->getBlock(0), "Scale") == 1 && !window->isWindowModified(),
			"main window Undo restores field and clean state");
		auto viewport = window->findChild<GLView *>("OGL1");
		check(viewport->getScene()->currentBlock == document->getBlock(0), "undo preserves current block selection");
		redoAction->trigger();
		check(document->get<float>(document->getBlock(0), "Scale") == 2 && window->isWindowModified(),
			"main window Redo restores edit and dirty state");
		SpellBook book(document);
		book.cast(document, document->getBlock(0), SpellBook::lookup("Block/Remove"));
		undoAction->trigger();
		check(document->getBlockCount() == 1 && document->get<float>(document->getBlock(0), "Scale") == 2,
			"main window restores deleted current block");
		check(window->findChild<QAction *>("aGizmo1") != nullptr, "main window exposes transform controls");
		viewport->getScene()->selMode = Scene::SelVertex;
		window->findChild<QAction *>("aGizmo1")->trigger();
		check(viewport->getScene()->selMode == Scene::SelVertex && viewport->gizmoMode == GLView::GizmoMove,
			"Move control preserves vertex editing mode");
		delete window;
	}
};
