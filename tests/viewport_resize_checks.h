#include <QEventLoop>
#include <QDockWidget>

// Exercise the real window/layout event filters. Do not manually call resizeGL:
// that would hide startup bugs in Qt's viewport resize delivery.
class ViewportResizeTest {
	static void settle() {
		QEventLoop loop;QTimer::singleShot(420,&loop,&QEventLoop::quit);loop.exec();
		QCoreApplication::processEvents();
	}
	static void verify(NifSkope * window,const char * message) {
		auto view=window->ogl;view->updateGL();
		auto available=window->graphicsView->contentsRect().size();
		std::printf("Viewport layout: available=%dx%d widget=%dx%d GL=%dx%d aspect=%g\n",available.width(),available.height(),view->width(),view->height(),view->gizmoViewport[2],view->gizmoViewport[3],double(view->aspect));
		check(view->isVisible() && view->updatesEnabled() && view->size()==available
			&& view->gizmoViewport[2]==view->width() && view->gizmoViewport[3]==view->height()
			&& std::abs(view->aspect-double(view->width())/view->height())<1e-6,message);
		QPointF center,right,up;
		view->setProjection(true);view->setRotation(0,0,0);view->setPosition(0,0,0);view->setDistance(200);view->view=GLView::ViewUser;view->updateGL();
		check(view->projectGizmo(Vector3(0,0,0),center) && view->projectGizmo(Vector3(20,0,0),right) && view->projectGizmo(Vector3(0,20,0),up)
			&& std::abs((right.x()-center.x())-(center.y()-up.y()))<.05,"perspective projection has equal horizontal and vertical pixel scale");
	}
public:
	static void run() {
		QTemporaryDir directory;QString path=directory.filePath("startup.nif");
		NifModel source;VertexEditorTest::fixture(source);check(source.saveToFile(path),"prepare startup viewport fixture");
		auto window=new NifSkope;window->resize(1400,850);window->show();window->loadFile(path);QCoreApplication::processEvents();
		auto view=window->ogl;view->timer->stop();view->scene->animate=false;view->animState=GLView::AnimDisabled;view->cfg.upAxis=GLView::ZAxis;view->cfg.fov=45;view->setZoom(1);
		verify(window,"initial single-NIF viewport fills the available area without a workspace tab switch");
		view->makeCurrent();glViewport(0,0,qMax(1,view->width()*3/4),qMax(1,view->height()*4/5));view->aspect=.75;
		verify(window,"a fresh frame replaces stale OpenGL viewport and projection state");
		auto options=view->scene->options;view->scene->options&=~(Scene::ShowGrid|Scene::ShowAxes|Scene::ShowNodes);
		view->cfg.background=QColor(47,61,73);view->makeCurrent();glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_SCISSOR_TEST);glScissor(0,0,view->width()/2,view->height()/2);view->updateGL();
		auto frame=view->grabFrameBuffer();auto corner=frame.pixelColor(frame.width()-7,7);
		check(std::abs(corner.red()-47)<=1 && std::abs(corner.green()-61)<=1 && std::abs(corner.blue()-73)<=1,"frame clearing covers the full drawable even after a stale partial scissor rectangle");
		view->makeCurrent();glDisable(GL_SCISSOR_TEST);view->scene->options=options;
		window->resize(1050,700);settle();verify(window,"shrinking the window resizes the drawable and projection together");
		window->resize(1550,920);settle();verify(window,"enlarging the window leaves no unused viewport region");
		window->showMaximized();settle();verify(window,"maximized viewport uses the full drawable with the correct aspect");
		window->showNormal();settle();verify(window,"restoring the window keeps projection synchronized");
		window->dKfm->hide();settle();verify(window,"changing dock visibility updates viewport size and aspect");
		QString reference=directory.filePath("reference.nif");check(source.saveToFile(reference),"prepare viewport reference fixture");
		QString error;check(window->addWorkspaceFile(reference,error),"add workspace tabs during viewport resize test");settle();verify(window,"showing workspace tabs resizes the viewport without distortion");
		window->activateDocument(1);settle();verify(window,"switching the active workspace file preserves correct viewport dimensions");
		window->removeWorkspaceDocument(0);settle();verify(window,"returning to one file expands the viewport to the full remaining area");
		view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/viewport-resize-preview.png");
		delete window;
	}
};
