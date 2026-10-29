#include "model/vertexedit.h"
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QStyleOptionToolButton>
#include <QContextMenuEvent>
#include <QThread>

class VertexEditorTest {
public:
 static void selectFacesForUV(GLView * view,const QSet<int> & faces){view->selectedFaces=faces;view->refreshEditData();emit view->editSelectionChanged();view->update();}
 static void clickEditFace(GLView * view,int face)
 {
  view->view=GLView::ViewUser;view->cfg.upAxis=GLView::ZAxis;view->setPosition(0,0,0);view->setRotation(25,0,-20);view->setDistance(180);view->setProjection(false);view->setGizmoMode(GLView::GizmoSelect);view->setEditXRay(true);view->updateGL();view->updateGL();auto triangle=view->faceData.triangles[face];auto point=(view->editData.positions[triangle[0]]+view->editData.positions[triangle[1]]+view->editData.positions[triangle[2]])/3.f;auto world=view->scene->getNode(view->model,view->editMesh)->worldTrans();QPointF screen;check(view->projectGizmo(world*point,screen),"project a viewport face for UV synchronization");mouse(view,QEvent::MouseButtonPress,screen.toPoint());mouse(view,QEvent::MouseButtonRelease,screen.toPoint());
 }
 static void numericAndIsolation()
 {
  NifModel model;fixture(model);model.set<Matrix>(model.getBlock(0),"Rotation",Matrix::euler(.2f,.1f,.4f));model.set<float>(model.getBlock(0),"Scale",2);model.set<Matrix>(model.getBlock(1),"Rotation",Matrix::euler(.1f,.3f,.2f));
  auto before=bytes(model);model.resetUndoHistory();QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();view->cfg.upAxis=GLView::ZAxis;view->cfg.fov=45;view->setNif(&model);view->setPosition(0,0,0);view->setRotation(25,0,-20);view->setDistance(180);view->setProjection(false);view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);view->setCurrentIndex(model.getBlock(1));view->updateGL();
  auto initial=view->scene->getNode(&model,model.getBlock(1))->worldTrans();view->setIsolateSelection(true);view->updateGL();check(!view->scene->getNode(&model,model.getBlock(1))->isHidden() && view->scene->getNode(&model,model.getBlock(3))->isHidden(),"isolation hides other meshes even with Show Hidden enabled");check(bytes(model)==before && model.undoStack->count()==0,"isolation leaves saved visibility and undo history unchanged");
  view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0,2};auto invert=[&]{QKeyEvent key(QEvent::KeyPress,Qt::Key_I,Qt::ControlModifier);QApplication::sendEvent(view,&key);};invert();check(view->selectedVertices==QSet<int>({1,3}),"Ctrl+I inverts vertices on the active mesh");invert();check(view->selectedVertices==QSet<int>({0,2}),"inverting vertex selection twice restores the selection");view->selectedVertices.clear();invert();check(view->selectedVertices.size()==4,"inverting empty vertex selection selects all vertices");invert();check(view->selectedVertices.isEmpty(),"inverting full vertex selection clears it");view->setFaceSelectionMode();view->selectedFaces={0};view->refreshEditData();invert();check(view->selectedFaces==QSet<int>({1}) && view->selectedVertices.size()==3,"Ctrl+I inverts faces and rebuilds their vertex selection");invert();check(view->selectedFaces==QSet<int>({0}) && bytes(model)==before && model.undoStack->count()==0,"selection inversion preserves document bytes and undo history");view->setEditSelectionMode(Scene::SelObject);
  QString error;Vector3 delta(2,3,4);check(view->applyNumericTransform(GLView::GizmoMove,delta,false,&error),"apply local numeric object movement");view->updateGL();auto moved=view->scene->getNode(&model,model.getBlock(1))->worldTrans();check((moved.translation-initial.translation-initial.rotation*delta).length()<.001f,"numeric local object movement follows the mesh axes under transformed parents");model.undo();view->updateGL();check(bytes(model)==before && view->scene->isolatedRoot==model.getBlock(1),"numeric transform undo restores exact document and keeps isolation");
  check(view->applyNumericTransform(GLView::GizmoMove,Vector3(0,7,0),true),"apply world numeric object movement");view->updateGL();moved=view->scene->getNode(&model,model.getBlock(1))->worldTrans();check((moved.translation-initial.translation-Vector3(0,7,0)).length()<.001f,"world numeric movement preserves other world coordinates");model.undo();view->updateGL();
  check(view->applyNumericTransform(GLView::GizmoRotate,Vector3(0,0,90),true),"apply world numeric rotation");view->updateGL();auto rotated=view->scene->getNode(&model,model.getBlock(1))->worldTrans();check((rotated.rotation*Vector3(1,0,0)-Matrix::euler(0,0,3.141592653589793f/2)*initial.rotation*Vector3(1,0,0)).length()<.001f,"world numeric rotation converts correctly through parent rotation");model.undo();view->updateGL();
  view->setMeshCenterPivot(true);Vector3 pivot;check(view->meshPivot(model.getBlock(1),pivot),"find numeric scale pivot");auto center=initial*pivot;check(view->applyNumericTransform(GLView::GizmoScale,Vector3(2,2,2),false),"apply uniform numeric object scale");view->updateGL();check((view->scene->getNode(&model,model.getBlock(1))->worldTrans()*pivot-center).length()<.001f,"numeric scale keeps the mesh-center pivot fixed");model.undo();view->setMeshCenterPivot(false);view->updateGL();
  view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0,1};VertexEditData data;data.read(&model,model.getBlock(1));auto points=data.positions;
  check(view->applyNumericTransform(GLView::GizmoMove,Vector3(1,2,3),false),"apply numeric movement to selected vertices");data.read(&model,model.getBlock(1));check(data.positions[0]==points[0]+Vector3(1,2,3) && data.positions[1]==points[1]+Vector3(1,2,3) && data.positions[2]==points[2],"numeric vertex movement preserves unselected vertices");model.undo();view->updateGL();
  check(view->applyNumericTransform(GLView::GizmoMove,Vector3(0,7,0),true),"apply world numeric movement to selected vertices");data.read(&model,model.getBlock(1));check((initial*(data.positions[0]-points[0])-initial.translation-Vector3(0,7,0)).length()<.001f,"numeric world vertex movement converts scale and rotation correctly");model.undo();view->updateGL();
  view->setFaceSelectionMode();view->selectedFaces={0};view->refreshEditData();check(view->applyNumericTransform(GLView::GizmoScale,Vector3(2,2,2),false),"apply numeric scale to selected faces");data.read(&model,model.getBlock(1));check(data.positions[3]==points[3] && std::abs((data.positions[0]-data.positions[1]).length()-80)<.001f,"numeric face scale changes only the face vertices");model.undo();view->updateGL();
  auto unchanged=bytes(model);check(!view->applyNumericTransform(GLView::GizmoScale,Vector3(-1,-1,-1),false) && bytes(model)==unchanged,"invalid numeric scale is rejected without changes");
  view->setWorldAxes(true);view->setGizmoMode(GLView::GizmoMove);view->selectedFaces={0};view->refreshEditData();view->updateGL();check(view->gizmoDirections[0]==Vector3(1,0,0) && view->gizmoDirections[1]==Vector3(0,1,0),"world-axis option aligns viewport gizmo to world coordinates");
  QKeyEvent showAll(QEvent::KeyPress,Qt::Key_H,Qt::AltModifier);QApplication::sendEvent(view,&showAll);view->updateGL();check(!view->scene->isolatedRoot.isValid() && !view->scene->getNode(&model,model.getBlock(3))->isHidden(),"Alt+H restores other meshes");delete view;
 }
 static void toolbarAndContext()
 {
  NifModel model;fixture(model);QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();view->cfg.fov=45;view->setNif(&model);view->resize(800,600);view->show();QCoreApplication::processEvents();view->setCurrentIndex(model.getBlock(1));
  int menus=0;QObject::connect(view,&GLView::viewportContextMenuRequested,view,[&](const QPoint &){++menus;});
  QPoint center(400,300);auto rmb=[&](QEvent::Type type,QPoint p){QMouseEvent event(type,p,type==QEvent::MouseMove?Qt::NoButton:Qt::RightButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(view,&event);};
  auto nativeContext=[&](QObject * target){QContextMenuEvent event(QContextMenuEvent::Mouse,center);QApplication::sendEvent(target,&event);};
  rmb(QEvent::MouseButtonPress,center);nativeContext(view);check(menus==0,"native mouse context event does not interrupt RMB navigation");rmb(QEvent::MouseButtonRelease,center);check(menus==1 && !view->flyNavigating,"quick RMB click requests one menu after navigation stops");nativeContext(view);check(menus==1,"native release context event cannot open a duplicate menu");
  rmb(QEvent::MouseButtonPress,center);QThread::msleep(280);rmb(QEvent::MouseButtonRelease,center);check(menus==1,"holding RMB without moving does not open a context menu");
  rmb(QEvent::MouseButtonPress,center);rmb(QEvent::MouseMove,center+QPoint(20,10));rmb(QEvent::MouseMove,center);rmb(QEvent::MouseButtonRelease,center);check(menus==1,"RMB look movement suppresses menu even after returning to the starting point");
  rmb(QEvent::MouseButtonPress,center);QKeyEvent forward(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier);QApplication::sendEvent(view,&forward);rmb(QEvent::MouseButtonRelease,center);check(menus==1,"navigation keys suppress the RMB context menu");
  rmb(QEvent::MouseButtonPress,center);QWheelEvent wheel(QPointF(center),QPointF(view->mapToGlobal(center)),QPoint(),QPoint(0,120),Qt::RightButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&wheel);rmb(QEvent::MouseButtonRelease,center);check(menus==1,"changing fly speed suppresses the RMB context menu");
  rmb(QEvent::MouseButtonPress,center);QFocusEvent lost(QEvent::FocusOut);QApplication::sendEvent(view,&lost);rmb(QEvent::MouseButtonRelease,center);check(menus==1,"focus loss cannot turn navigation into a quick click");delete view;
  auto window=new NifSkope;auto document=window->findChild<NifModel *>();fixture(*document);window->select(document->getBlock(1));window->resize(1280,800);window->show();QCoreApplication::processEvents();view=window->findChild<GLView *>("OGL1");view->timer->stop();auto toolbar=window->findChild<QToolBar *>("tRender");
  auto mode=window->findChild<QToolButton *>("selectionModeButton"),tool=window->findChild<QToolButton *>("transformToolButton");
  check(mode && tool && !mode->icon().isNull() && !tool->icon().isNull(),"compact selection and transform buttons have icons");
  check(mode->popupMode()==QToolButton::MenuButtonPopup && mode->menu()->actions().size()==3,"selection mode button cycles with direct menu choices");
  mode->click();check(!view->faceSelection && view->scene->selMode==Scene::SelVertex && mode->icon().cacheKey()==window->findChild<QAction *>("aSelectVertex")->icon().cacheKey(),"selection button cycles to Vertex and updates its icon");
  mode->click();check(view->faceSelection && mode->icon().cacheKey()==window->findChild<QAction *>("aSelectFace")->icon().cacheKey(),"selection button cycles to Face and updates its icon");mode->click();check(view->scene->selMode==Scene::SelObject,"selection button cycles back to Object");
  QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(view,&tab);check(mode->icon().cacheKey()==window->findChild<QAction *>("aSelectVertex")->icon().cacheKey(),"Tab keeps the compact selection icon synchronized");window->findChild<QAction *>("aSelectObject")->trigger();
  for(int i=1;i<=4;++i){tool->click();int expected=i%4;check(int(view->gizmoMode)==expected && tool->icon().cacheKey()==window->findChild<QAction *>(QString("aGizmo%1").arg(expected))->icon().cacheKey(),"transform button cycles tools and shows the active icon");}
  QKeyEvent scale(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier);QApplication::sendEvent(view,&scale);check(view->gizmoMode==GLView::GizmoScale,"R shortcut still selects Scale with grouped toolbar");QKeyEvent select(QEvent::KeyPress,Qt::Key_V,Qt::NoModifier);QApplication::sendEvent(view,&select);check(view->gizmoMode==GLView::GizmoSelect && tool->icon().cacheKey()==window->findChild<QAction *>("aGizmo0")->icon().cacheKey(),"V selects Select and updates the active tool icon");
  auto meshTools=window->findChild<QToolButton *>("meshToolsButton"),materials=window->findChild<QToolButton *>("materialsButton");check(meshTools && materials && !meshTools->icon().isNull() && !materials->icon().isNull(),"mesh tools and materials have icon flyouts");
  for(const char * name:{"aDeleteFaces","aSnapVertices","aMeshCenterPivot","aEditXRay"})check(meshTools->menu()->actions().contains(window->findChild<QAction *>(name)),"mesh tools groups deletion snapping pivot and X-ray");
  for(const char * name:{"aNewMaterial","aMakeMaterialUnique"})check(materials->menu()->actions().contains(window->findChild<QAction *>(name)),"material actions are grouped");
  for(const char * name:{"aSelectObject","aSelectVertex","aSelectFace","aDeleteFaces","aSnapVertices","aMeshCenterPivot","aNewMaterial","aMakeMaterialUnique","aGizmo0","aGizmo1","aGizmo2","aGizmo3"})check(!toolbar->actions().contains(window->findChild<QAction *>(name)),"grouped actions no longer occupy separate toolbar buttons");
  for(const char * name:{"aUVEditor","aRebuildCollision"}){auto action=window->findChild<QAction *>(name);check(toolbar->actions().contains(action) && !action->icon().isNull(),"UV and collision keep standalone icon buttons");}
  auto snap=window->findChild<QAction *>("aSnapVertices");snap->trigger();check(view->vertexSnap==snap->isChecked(),"grouped Snap Vertices still updates editor state");
  for(auto size:{QSize(36,36),QSize(18,18)}){
   toolbar->setIconSize(size);QCoreApplication::processEvents();check(mode->iconSize()==size && tool->iconSize()==size,"compact buttons follow toolbar icon size");
   for(auto button:{mode,tool}){
    auto arrowRect=[button]{QStyleOptionToolButton option;option.initFrom(button);option.rect=button->rect();option.iconSize=button->iconSize();option.features=QStyleOptionToolButton::MenuButtonPopup|QStyleOptionToolButton::HasMenu;return button->style()->subControlRect(QStyle::CC_ToolButton,&option,QStyle::SC_ToolButtonMenu,button);};
    QSize stableSize=button->size();QRect stableArrow=arrowRect();
    for(int cycle=0;cycle<4;++cycle){
     button->click();QEvent enter(QEvent::Enter);QApplication::sendEvent(button,&enter);button->setDown(true);QCoreApplication::processEvents();
     check(button->size()==stableSize && arrowRect()==stableArrow && stableArrow.left()>=button->iconSize().width()+4,"cycling and pressing keep the dropdown arrow outside the full icon area");
     button->setDown(false);QEvent leave(QEvent::Leave);QApplication::sendEvent(button,&leave);
    }
   }
  }
  int realMenus=0;QObject::connect(view,&GLView::viewportContextMenuRequested,view,[&](const QPoint &){++realMenus;});
  auto container=window->findChild<GLGraphicsView *>();nativeContext(view);nativeContext(container);check(realMenus==0 && !QApplication::activePopupWidget(),"viewport and container suppress native mouse context popups");
  rmb(QEvent::MouseButtonPress,center);QApplication::sendEvent(view,&forward);nativeContext(container);rmb(QEvent::MouseButtonRelease,center);check(realMenus==0 && !QApplication::activePopupWidget(),"actual viewport fly gesture cannot open the native menu");
  QTimer::singleShot(0,[](){if(auto popup=QApplication::activePopupWidget())popup->close();});rmb(QEvent::MouseButtonPress,center);rmb(QEvent::MouseButtonRelease,center);check(realMenus==1,"swift click opens the actual viewport context menu");
  toolbar->grab().save(QCoreApplication::applicationDirPath()+"/compact-toolbar-preview.png");delete window;
 }
 static void axisSnapping()
 {
  NifModel model;fixture(model);auto geometry=model.getBlock(2);model.set<Vector3>(model.getIndex(geometry,"Vertices").child(3,0),Vector3(20,20,30));model.set<float>(model.getBlock(0),"Scale",1.75f);model.set<Matrix>(model.getBlock(0),"Rotation",Matrix::euler(.3f,.2f,.4f));model.set<Matrix>(model.getBlock(1),"Rotation",Matrix::euler(.1f,.2f,.1f));model.set<float>(model.getBlock(1),"Scale",1.2f);model.resetUndoHistory();
  QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();view->cfg.upAxis=GLView::ZAxis;view->cfg.fov=45;view->setNif(&model);view->setPosition(0,0,0);view->setRotation(25,0,-20);view->setDistance(180);view->setProjection(true);view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);view->setCurrentIndex(model.getBlock(1));view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0};view->setVertexSnap(true);view->setGizmoMode(GLView::GizmoMove);
  VertexEditData data;data.read(&model,model.getBlock(1));auto original=data.positions;
  auto axisPoint=[&](int axis){for(int step=3;step<10;++step){auto p=(view->gizmoScreenOrigin*(1-step/10.)+view->gizmoScreenAxes[axis]*(step/10.)).toPoint();if(view->pickGizmo(p)==axis)return p;}check(false,"find axis snap handle");return QPoint();};
  for(int axis=0;axis<3;++axis) {
   view->updateGL();view->updateGL();auto start=axisPoint(axis);auto world=view->scene->getNode(&model,model.getBlock(1))->worldTrans();QPointF target;check(view->projectGizmo(world*original[3],target),"project off-axis snapping target");
   mouse(view,QEvent::MouseButtonPress,start);check(view->gizmoAxis==axis,"start constrained axis movement");mouse(view,QEvent::MouseMove,target.toPoint());check(view->snapTarget>=0,"find snapping target away from active axis");mouse(view,QEvent::MouseButtonRelease,target.toPoint());data.read(&model,model.getBlock(1));
   bool constrained=true;for(int a=0;a<3;++a)constrained=constrained && std::abs(data.positions[0][a]-(a==axis?original[3][a]:original[0][a]))<.001f;
   check(constrained && data.positions[3]==original[3],"axis snapping copies only its coordinate under rotated scaled parents");model.undo();
  }
  view->updateGL();view->updateGL();check(view->gizmoPlanes[2].size()==4,"XY plane is available for constrained snapping");QPointF plane;for(auto p:view->gizmoPlanes[2])plane+=p;auto start=(plane/4).toPoint();check(view->pickGizmo(start)==5,"pick XY plane snap handle");
  auto world=view->scene->getNode(&model,model.getBlock(1))->worldTrans();QPointF target;view->projectGizmo(world*original[3],target);mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,target.toPoint());check(view->snapTarget>=0,"plane snapping accepts a target outside its plane");mouse(view,QEvent::MouseButtonRelease,target.toPoint());data.read(&model,model.getBlock(1));
  check(std::abs(data.positions[0][0]-original[3][0])<.001f && std::abs(data.positions[0][1]-original[3][1])<.001f && std::abs(data.positions[0][2]-original[0][2])<.001f,"plane snapping preserves the excluded coordinate");model.undo();
  view->setFaceSelectionMode();view->selectedFaces={0};view->refreshEditData();view->updateGL();view->updateGL();start=axisPoint(0);mouse(view,QEvent::MouseButtonPress,start);
  auto anchor=view->gizmoParent.rotation.inverted()*(view->snapAnchor-view->gizmoParent.translation)/view->gizmoParent.scale;mouse(view,QEvent::MouseMove,target.toPoint());mouse(view,QEvent::MouseButtonRelease,target.toPoint());data.read(&model,model.getBlock(1));
  bool constrained=true;for(int i:{0,1,2})constrained=constrained && std::abs(data.positions[i][0]-(original[i][0]+original[3][0]-anchor[0]))<.001f && std::abs(data.positions[i][1]-original[i][1])<.001f && std::abs(data.positions[i][2]-original[i][2])<.001f;
  check(constrained && data.positions[3]==original[3],"face snapping moves the selection only on the active axis");delete view;
 }
 static void viewportControls()
 {
  NifModel model;fixture(model);QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();view->cfg.upAxis=GLView::ZAxis;view->cfg.fov=45;view->cfg.moveSpd=50;
  view->setNif(&model);view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;
  view->setPosition(0,0,0);view->setRotation(25,0,-20);view->setDistance(180);view->setProjection(true);view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);view->setCurrentIndex(model.getBlock(1));view->updateGL();
  auto key=[&](int code,Qt::KeyboardModifiers mods=Qt::NoModifier,bool release=false){QKeyEvent event(release?QEvent::KeyRelease:QEvent::KeyPress,code,mods);QApplication::sendEvent(view,&event);};
  auto rmb=[&](QEvent::Type type,QPoint point){QMouseEvent event(type,point,type==QEvent::MouseMove?Qt::NoButton:Qt::RightButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(view,&event);};
  auto tick=[&]{view->lastTime=QTime::currentTime().addMSecs(-100);view->advanceGears();view->updateGL();};
  auto eye=[&]{view->updateGL();return view->viewTrans.rotation.inverted()*view->viewTrans.translation;};
  auto position=view->Pos;key(Qt::Key_W);tick();check(view->gizmoMode==GLView::GizmoMove && view->Pos==position,"W selects Move without moving the camera");key(Qt::Key_E);check(view->gizmoMode==GLView::GizmoRotate,"E selects Rotate");key(Qt::Key_R);check(view->gizmoMode==GLView::GizmoScale,"R selects Scale");
  auto originalDocument=bytes(model);auto beforeEye=eye();QPoint center(400,300);rmb(QEvent::MouseButtonPress,center);check(view->flyNavigating && (eye()-beforeEye).length()<.001f,"RMB begins fly navigation without jumping the camera");
  rmb(QEvent::MouseMove,center+QPoint(40,20));tick();check((eye()-beforeEye).length()<.001f && !(view->Rot==Vector3(25,0,-20)),"RMB mouse look rotates around the eye");
  position=view->Pos;key(Qt::Key_W);tick();key(Qt::Key_W,Qt::NoModifier,true);float normalDistance=(view->Pos-position).length();check(normalDistance>1 && view->gizmoMode==GLView::GizmoScale,"RMB+W moves the camera without changing the active tool");
  position=view->Pos;key(Qt::Key_Shift);key(Qt::Key_W);tick();key(Qt::Key_W,Qt::NoModifier,true);key(Qt::Key_Shift,Qt::NoModifier,true);check((view->Pos-position).length()>normalDistance*3.5f,"Shift boosts fly movement speed");
  position=view->Pos;key(Qt::Key_E);tick();key(Qt::Key_E,Qt::NoModifier,true);check(view->Pos[2]<position[2] && view->Pos[0]==position[0] && view->Pos[1]==position[1] && view->gizmoMode==GLView::GizmoScale,"RMB+E moves up along world up without switching tools");
  position=view->Pos;key(Qt::Key_Q);tick();key(Qt::Key_Q,Qt::NoModifier,true);check(view->Pos[2]>position[2],"RMB+Q moves down");
  float distance=view->Dist;auto zoom=view->Zoom;float speed=view->flySpeedMultiplier;QWheelEvent wheel(QPointF(center),QPointF(view->mapToGlobal(center)),QPoint(),QPoint(0,120),Qt::RightButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view,&wheel);
  check(view->flySpeedMultiplier>speed && view->Dist==distance && view->Zoom==zoom,"RMB wheel changes fly speed without zooming");
  auto selected=view->scene->currentBlock;rmb(QEvent::MouseButtonRelease,center);position=view->Pos;tick();check(!view->flyNavigating && view->Pos==position && view->scene->currentBlock==selected,"RMB release stops movement without changing selection");key(Qt::Key_E);check(view->gizmoMode==GLView::GizmoRotate,"releasing RMB restores editing shortcuts");
  rmb(QEvent::MouseButtonPress,center);key(Qt::Key_W);QFocusEvent focus(QEvent::FocusOut);QApplication::sendEvent(view,&focus);position=view->Pos;tick();check(!view->flyNavigating && view->Pos==position,"losing focus stops fly movement and held keys");
  view->setOrientation(GLView::ViewWalk);key(Qt::Key_W);position=view->Pos;tick();check(view->Pos==position && view->gizmoMode==GLView::GizmoMove,"Walk mode also reserves unmodified W for Move");
  rmb(QEvent::MouseButtonPress,center);key(Qt::Key_W);tick();check(!(view->Pos==position),"Walk mode supports RMB+W navigation");rmb(QEvent::MouseButtonRelease,center);view->setOrientation(GLView::ViewUser,false);
  check(bytes(model)==originalDocument,"tool shortcuts and fly navigation leave document unchanged");
  auto data=model.getBlock(2);QVector<Vector3> points;for(int island=0;island<3;++island)for(int v=0;v<4;++v)points<<Vector3((v%2)*20+(island==2?0:island*60),(v/2)*20,0);points<<Vector3(0,0,80);
  model.set<quint16>(data,"Num Vertices",points.size());model.updateArray(data,"Vertices");model.setArray<Vector3>(data,"Vertices",points);model.set<quint16>(data,"Num Triangles",6);model.set<uint>(data,"Num Triangle Points",18);model.updateArray(data,"Triangles");QVector<Triangle> triangles;for(int i=0;i<3;++i){int v=i*4;triangles<<Triangle(v,v+1,v+2)<<Triangle(v+1,v+3,v+2);}model.setArray<Triangle>(data,"Triangles",triangles);originalDocument=bytes(model);model.resetUndoHistory();
  view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0};key(Qt::Key_L,Qt::ControlModifier);check(view->selectedVertices==QSet<int>({0,1,2,3}),"Ctrl+L selects the vertex island without joining coincident separate geometry");
  view->selectedVertices={0,4,12};key(Qt::Key_L,Qt::ControlModifier);check(view->selectedVertices==QSet<int>({0,1,2,3,4,5,6,7,12}),"Select Linked supports multiple vertex seeds and loose vertices");
  view->selectedVertices.clear();key(Qt::Key_L,Qt::ControlModifier);check(view->selectedVertices.isEmpty(),"Select Linked does nothing without a seed");
  view->setFaceSelectionMode();view->selectedFaces={0};key(Qt::Key_L,Qt::ControlModifier);check(view->selectedFaces==QSet<int>({0,1}),"Ctrl+L selects connected faces in one island");view->selectedFaces={0,4};key(Qt::Key_L,Qt::ControlModifier);check(view->selectedFaces==QSet<int>({0,1,4,5}),"Select Linked expands multiple face islands");check(bytes(model)==originalDocument && model.undoStack->count()==0,"Select Linked changes only selection");delete view;
  auto window=new NifSkope;auto document=window->findChild<NifModel *>();fixture(*document);window->select(document->getBlock(1));view=window->findChild<GLView *>("OGL1");
  key(Qt::Key_R);check(window->findChild<QAction *>("aGizmo3")->isChecked(),"R updates the Scale toolbar action");key(Qt::Key_E);check(window->findChild<QAction *>("aGizmo2")->isChecked(),"E updates the Rotate toolbar action");key(Qt::Key_W);check(window->findChild<QAction *>("aGizmo1")->isChecked(),"W updates the Move toolbar action");
  rmb(QEvent::MouseButtonPress,center);key(Qt::Key_W);view->lastTime=QTime::currentTime().addMSecs(-100);view->advanceGears();check(view->flyNavigating,"main-window viewpoint updates do not interrupt RMB navigation");rmb(QEvent::MouseButtonRelease,center);delete window;
 }
 static void editModes()
 {
  NifModel model;fixture(model);auto data=model.getBlock(2);
  const QVector<Vector3> points={Vector3(-20,-20,20),Vector3(20,-20,20),Vector3(-20,20,20),Vector3(20,20,20),Vector3(-9,-9,-20),Vector3(9,-9,-20),Vector3(-9,9,-20),Vector3(9,9,-20)};
  model.set<quint16>(data,"Num Vertices",points.size());model.updateArray(data,"Vertices");model.setArray<Vector3>(data,"Vertices",points);
  model.set<quint16>(data,"Num Triangles",4);model.set<uint>(data,"Num Triangle Points",12);model.updateArray(data,"Triangles");
  model.setArray<Triangle>(data,"Triangles",QVector<Triangle>{Triangle(0,1,2),Triangle(1,3,2),Triangle(4,5,6),Triangle(5,7,6)});
  model.resetUndoHistory();QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);view->timer->stop();view->cfg.upAxis=GLView::ZAxis;
  view->setNif(&model);view->setPosition(0,0,0);view->setRotation(0,0,0);view->setDistance(180);view->setProjection(false);view->setGizmoMode(GLView::GizmoSelect);
  view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->scene->options&=~Scene::ShowGrid;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;
  view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);view->setCurrentIndex(model.getBlock(1));
  auto tab=[&](QObject * target){QKeyEvent key(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(target,&key);};
  tab(view);check(view->scene->selMode==Scene::SelVertex && !view->faceSelection,"Tab cycles Object to Vertex without moving focus");
  tab(view);check(view->faceSelection,"Tab cycles Vertex to Face");tab(view);check(view->scene->selMode==Scene::SelObject,"Tab cycles Face to Object");
  tab(view);view->updateGL();view->updateGL();auto before=bytes(model);
  QPoint a=vertex(view,0)-QPoint(8,-8),b=vertex(view,3)+QPoint(8,-8);
  auto box=[&]{mouse(view,QEvent::MouseButtonPress,a);mouse(view,QEvent::MouseMove,b);mouse(view,QEvent::MouseButtonRelease,b);};
  box();check(view->selectedVertices.size()==8,"X-ray box selects front and hidden vertices");
  view->setEditXRay(false);view->updateGL();view->updateGL();auto world=view->scene->getNode(&model,model.getBlock(1))->worldTrans();
  check(view->editPointVisible(world*points[0]) && !view->editPointVisible(world*points[4]),"visible-only depth distinguishes front and hidden vertices");
  box();check(view->selectedVertices==QSet<int>({0,1,2,3}),"X-ray off box excludes hidden vertices");
  view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/vertex-visible-only-preview.png");
  tab(view);view->updateGL();view->updateGL();box();check(view->selectedFaces==QSet<int>({0,1}),"X-ray off face box excludes hidden triangles");
  view->setEditXRay(true);view->updateGL();view->updateGL();box();check(view->selectedFaces.size()==4,"X-ray on face box includes hidden triangles");
  check(bytes(model)==before,"selection mode and X-ray changes do not edit geometry");
  view->setEditSelectionMode(Scene::SelVertex);view->selectedVertices={0};
  QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(view,&remove);view->updateGL();
  VertexEditData vertices;FaceEditData faces;check(vertices.read(&model,model.getBlock(1)) && vertices.positions.size()==7 && faces.read(&model,model.getBlock(1)) && faces.triangles.size()==3,"Delete key removes vertices and connected triangles");
  model.undo();check(bytes(model)==before,"keyboard vertex deletion undo restores document");
  view->setEditSelectionMode(Scene::SelObject);auto duplicate=SpellBook::lookup("Block/Duplicate")->cast(&model,model.getBlock(1));int id=model.getBlockNumber(duplicate);
  view->setCurrentIndex(duplicate);tab(view);view->updateGL();check(view->vertexEditReason.isEmpty() && view->editMesh==model.getBlock(id) && !view->editData.isShared(&model),"entering vertex mode automatically detaches duplicated geometry");
  view->selectedVertices={0};QApplication::sendEvent(view,&remove);VertexEditData original;
  check(original.read(&model,model.getBlock(1)) && original.positions==points,"keyboard deletion on a duplicate preserves original vertices");
  QKeyEvent all(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier);QApplication::sendEvent(view,&all);QApplication::sendEvent(view,&remove);view->updateGL();view->updateGL();
  check(vertices.read(&model,model.getBlock(id)) && vertices.positions.isEmpty() && view->vertexEditReason.isEmpty(),"deleting all vertices renders an empty duplicate without editing errors");delete view;
  auto window=new NifSkope;auto document=window->findChild<NifModel *>();fixture(*document);window->select(document->getBlock(1));auto viewport=window->findChild<GLView *>("OGL1");
  tab(viewport);check(window->findChild<QAction *>("aSelectVertex")->isChecked(),"Tab updates main window Vertex selection action");
  tab(viewport);check(window->findChild<QAction *>("aSelectFace")->isChecked(),"Tab updates main window Face selection action");
  tab(viewport);check(window->findChild<QAction *>("aSelectObject")->isChecked(),"Tab updates main window Object selection action");
  if(auto container=window->findChild<GLGraphicsView *>()){tab(container);check(window->findChild<QAction *>("aSelectVertex")->isChecked(),"Tab cycles selection when the viewport container receives the key");}
  auto xray=window->findChild<QAction *>("aEditXRay");xray->trigger();check(!viewport->editXRay,"Render menu toggles X-ray");
  QKeyEvent toggleXRay(QEvent::KeyPress,Qt::Key_X,Qt::ShiftModifier);QApplication::sendEvent(viewport,&toggleXRay);
  check(viewport->editXRay && xray->isChecked(),"Shift+X enables X-ray and updates the mesh tools action");
  QKeyEvent repeatXRay(QEvent::KeyPress,Qt::Key_X,Qt::ShiftModifier,QString(),true);QApplication::sendEvent(viewport,&repeatXRay);
  check(viewport->editXRay && xray->isChecked(),"holding Shift+X does not repeatedly toggle X-ray");
  if(auto container=window->findChild<GLGraphicsView *>())QApplication::sendEvent(container,&toggleXRay);
  else QApplication::sendEvent(viewport,&toggleXRay);
  check(!viewport->editXRay && !xray->isChecked(),"Shift+X through the viewport container keeps the X-ray action synchronized");delete window;
 }
 static void faces()
 {
  NifModel model;fixture(model);QGLFormat format;format.setVersion(2,1);auto view=new GLView(format,nullptr);
  view->setNif(&model);view->setPosition(0,0,0);view->setRotation(0,0,0);view->setDistance(180);view->setProjection(false);
  view->scene->animate=false;view->scene->options|=Scene::ShowHidden;view->animState=GLView::AnimDisabled;view->view=GLView::ViewUser;
  view->resize(800,600);view->show();QCoreApplication::processEvents();view->resizeGL(800,600);
  view->setCurrentIndex(model.getBlock(1));view->setFaceSelectionMode();view->setGizmoMode(GLView::GizmoSelect);view->updateGL();
  check(view->faceSelection && view->vertexProjectionValid,"face mode locks mesh and projects geometry");
  QPoint p=(QPointF(vertex(view,0))+QPointF(vertex(view,1))+QPointF(vertex(view,2))).toPoint()/3;
  mouse(view,QEvent::MouseButtonPress,p);mouse(view,QEvent::MouseButtonRelease,p);
  check(view->selectedFaces==QSet<int>({0}) && view->selectedVertices==QSet<int>({0,1,2}),"face click selects triangle and its vertices");
  view->setGizmoMode(GLView::GizmoMove);view->updateGL();auto before=view->editData.positions;
  QPoint start=(view->gizmoScreenOrigin*.3+view->gizmoScreenAxes[0]*.7).toPoint();QPoint end=start+(view->gizmoScreenAxes[0]-view->gizmoScreenOrigin).toPoint()/5;
  mouse(view,QEvent::MouseButtonPress,start);check(view->vertexGizmo,"face starts vertex transform");mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
  VertexEditData data;data.read(&model,model.getBlock(1));check(!(data.positions[0]==before[0]) && !(data.positions[1]==before[1]) && !(data.positions[2]==before[2]) && data.positions[3]==before[3],"face movement changes only triangle vertices");
  model.undo();view->updateGL();check(view->faceSelection && view->selectedFaces==QSet<int>({0}),"face undo preserves edit mode and selection");
  QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(view,&remove);view->updateGL();
  FaceEditData faces;check(faces.read(&model,model.getBlock(1)) && faces.triangles==QVector<Triangle>{Triangle(1,3,2)},"Delete key removes selected face");
  model.undo();view->updateGL();check(faces.read(&model,model.getBlock(1)) && faces.triangles.size()==2,"undo restores deleted face in viewport");
  view->setGizmoMode(GLView::GizmoSelect);QKeyEvent all(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier);QApplication::sendEvent(view,&all);
  check(view->selectedFaces.size()==2,"Ctrl+A selects all faces");view->deleteSelectedFaces();check(faces.read(&model,model.getBlock(1)) && faces.triangles.isEmpty(),"delete all faces leaves empty geometry");
  model.undo();view->setEditSelectionMode(Scene::SelVertex);view->updateGL();view->updateGL();check(!view->faceSelection,"vertex mode exits face selection");view->grabFrameBuffer().save(QCoreApplication::applicationDirPath()+"/vertex-wireframe-preview.png");
  delete view;
  auto window=new NifSkope;auto document=window->findChild<NifModel *>();fixture(*document);window->select(document->getBlock(1));
  window->findChild<QAction *>("aSelectFace")->trigger();auto viewport=window->findChild<GLView *>("OGL1");
  check(viewport->faceSelection,"main window Faces action enters face selection");
  window->findChild<QAction *>("aGizmo1")->trigger();check(viewport->faceSelection,"Move action preserves face mode");
  window->findChild<QAction *>("aMakeMaterialUnique")->trigger();auto links=document->getLinkArray(document->getBlock(1),"Properties");
  check(links.size()==1 && document->inherits(document->getBlock(links[0]),"NiMaterialProperty"),"main window unique action creates material when absent");
  window->findChild<QAction *>("aNewMaterial")->trigger();links=document->getLinkArray(document->getBlock(1),"Properties");
  check(links.size()==1,"main window new material replaces existing material");delete window;
 }
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
		view->setOrientation(GLView::ViewWalk);view->updateGL();
		check(view->gizmoVisible && view->gizmoPlanes[2].size()==4,"Walk mode displays the selected vertices' XY plane handle");
		QPointF planeCenter;for(auto corner:view->gizmoPlanes[2])planeCenter+=corner;planeCenter/=4;
		start=planeCenter.toPoint();end=start+QPoint(12,8);check(view->pickGizmo(start)==5,"pick XY movement for selected vertices");
		auto walkCamera=view->Pos;
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);
		data.read(&model,model.getBlock(1));
		check(std::abs(data.positions[0][0]-original[0][0])>.01f && std::abs(data.positions[0][1]-original[0][1])>.01f
			&& data.positions[0][2]==original[0][2] && data.positions[2]==original[2] && data.positions[3]==original[3]
			&& view->Pos==walkCamera,"Walk-mode plane drag moves selected vertices along two axes without moving the camera or other vertices");
		model.undo();view->updateGL();
		planeCenter=QPointF();for(auto corner:view->gizmoPlanes[2])planeCenter+=corner;start=(planeCenter/4).toPoint();
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,start+QPoint(12,8));
		QKeyEvent cancelPlane(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&cancelPlane);mouse(view,QEvent::MouseButtonRelease,start+QPoint(12,8));
		data.read(&model,model.getBlock(1));check(data.positions==original && model.undoStack->index()==0,"Escape cancels a two-axis vertex drag without a history entry");
		view->setOrientation(GLView::ViewUser,false);view->updateGL();

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
		auto snapWorld=view->scene->getNode(&model,model.getBlock(1))->worldTrans();auto targetLocal=snapWorld.rotation.inverted()*(targetWorld-snapWorld.translation)/snapWorld.scale;
		check(std::abs(data.positions[0][0]-targetLocal[0])<.001f && data.positions[0][1]==original[0][1] && data.positions[0][2]==original[0][2],"snap to another mesh changes only the active axis");
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

		// Coincident UV/normal seam vertices should behave as one clicked point.
		data.read(&model,model.getBlock(1));auto sourcePositions=data.positions;
		auto coincident=sourcePositions;coincident[2]=coincident[0];coincident[3]=coincident[0]+Vector3(0,0,12);
		check(data.write(&model,coincident,{0,1,2,3}),"prepare coincident vertices and a point at another depth");
		model.resetUndoHistory();view->setCurrentIndex(model.getBlock(1));view->setEditSelectionMode(Scene::SelVertex);view->setGizmoMode(GLView::GizmoSelect);view->updateGL();
		p=vertex(view,0);mouse(view,QEvent::MouseButtonPress,p);mouse(view,QEvent::MouseButtonRelease,p);
		check(view->selectedVertices==QSet<int>({0,2}),"click selects all identical XYZ vertices and excludes points at another depth");
		mouse(view,QEvent::MouseButtonPress,p,Qt::ControlModifier);mouse(view,QEvent::MouseButtonRelease,p,Qt::ControlModifier);
		check(view->selectedVertices.isEmpty() && model.undoStack->count()==0,"Ctrl toggles coincident vertices together without editing the file");
		view->setEditSelectionMode(Scene::SelObject);
		for(auto & point:sourcePositions)point+=Vector3(20,30,10);
		data.read(&model,model.getBlock(1));data.write(&model,sourcePositions,{0,1,2,3});data.updateBounds(&model);
		model.set<float>(model.getBlock(0),"Scale",1.75f);model.set<Matrix>(model.getBlock(0),"Rotation",Matrix::euler(.2f,.1f,.3f));
		auto beforePivot=bytes(model);model.resetUndoHistory();view->setCurrentIndex(model.getBlock(1));view->setGizmoMode(GLView::GizmoScale);view->setMeshCenterPivot(true);view->updateGL();
		Vector3 localPivot;check(view->meshPivot(model.getBlock(1),localPivot),"find selected mesh center independently of its stored origin");
		auto centerWorld=view->scene->getNode(&model,model.getBlock(1))->worldTrans()*localPivot;
		check((view->gizmoOrigin-centerWorld).length()<.001f && bytes(model)==beforePivot,"temporary mesh-center pivot moves the gizmo without editing the NIF");
		start=view->gizmoScreenOrigin.toPoint();end=start+QPoint(30,-30);
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);view->updateGL();
		check(model.get<float>(model.getBlock(1),"Scale")>1 && (view->scene->getNode(&model,model.getBlock(1))->worldTrans()*localPivot-centerWorld).length()<.01f,"object scaling keeps its mesh-center pivot fixed under a transformed parent");
		data.read(&model,model.getBlock(1));check(data.positions==sourcePositions,"temporary pivot transforms leave vertex storage intact");
		model.undo();check(bytes(model)==beforePivot,"undo restores the complete centered object transform");
		view->setCurrentIndex(model.getBlock(1));view->setGizmoMode(GLView::GizmoRotate);view->updateGL();
		int pivotRing=-1;for(int i=4;i<60;++i)if(view->pickGizmo(view->gizmoRings[2][i].toPoint())==2){pivotRing=i;break;}
		check(pivotRing>=0,"pick rotation ring at the temporary mesh center");
		start=view->gizmoRings[2][pivotRing].toPoint();end=view->gizmoRings[2][pivotRing+12].toPoint();
		mouse(view,QEvent::MouseButtonPress,start);mouse(view,QEvent::MouseMove,end);mouse(view,QEvent::MouseButtonRelease,end);view->updateGL();
		check((view->scene->getNode(&model,model.getBlock(1))->worldTrans()*localPivot-centerWorld).length()<.01f && model.undoStack->index()==1,"rotation keeps the temporary mesh center fixed and creates one undo step");
		model.undo();view->setCurrentIndex(model.getBlock(1));view->setMeshCenterPivot(false);view->updateGL();
		check((view->gizmoOrigin-view->scene->getNode(&model,model.getBlock(1))->worldTrans().translation).length()<.001f && bytes(model)==beforePivot,"turning off centered pivot restores the stored origin without a document edit");
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
