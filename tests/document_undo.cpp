// Include the application entry point under another name so the IPCsocket
// implementation is available when linking the application's existing objects.
#define main nifskopeApplicationMain
#include "../src/main.cpp"
#undef main

#include "spellbook.h"
#include "model/undocommands.h"
#include <QBuffer>
#include <QUndoStack>
#include <cstdio>
#include <cstdlib>

static void check( bool condition, const char * message )
{
	if ( !condition ) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
	std::printf("PASS: %s\n", message);
}

static QByteArray bytes( NifModel & model )
{
	QByteArray result;
	QBuffer buffer(&result);
	buffer.open(QIODevice::WriteOnly);
	check(model.save(buffer), "serialize document");
	model.flushUndoHistory();
	return result;
}

#include "nimesh_checks.h"
#include "gizmo_checks.h"
#include "vertex_checks.h"
#include "face_material_checks.h"
#include "physx_inspect.h"
#include "portal_inspect.h"
#include "room_checks.h"
#include "portal_checks.h"
#include "physx_checks.h"
#include "physx_group_checks.h"
#include "uv_checks.h"
#include "editor_features.h"
#include "workspace_checks.h"
#include "viewport_resize_checks.h"
#include "lighting_checks.h"
#include "color_hex_checks.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

int main( int argc, char ** argv )
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	QApplication app(argc, argv);
	app.setOrganizationName("NifSkopeTests");
	app.setApplicationName("DocumentUndo");
	// Keep test preferences writable and isolated from installed application settings.
	QTemporaryDir testPreferences;
	check(testPreferences.isValid(),"create isolated test preferences");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,testPreferences.path());
	QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,testPreferences.path());
	QSettings().setValue("GLView/Unshaded",false);
	QSettings().setValue("GLView/ClampDetailBrightness",false);
	QSettings().setValue("GLView/DetailBrightness",200);
	check(argc > 1, "source directory supplied");
	QDir::setCurrent(QString::fromLocal8Bit(argv[1]));
	check(NifModel::loadXML(), "load NIF schema");
	qRegisterMetaType<NifValue>();
	QMetaType::registerComparators<NifValue>();
	if(argc==3 && QByteArray(argv[2])=="--color-hex"){colorHexChecks();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--portals"){portalEditingChecks();realPortalEditingChecks();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--rooms"){roomTests();realRoomTests();return 0;}
	if(argc==4 && QByteArray(argv[2])=="--portal-inspect")return inspectPortals(QString::fromLocal8Bit(argv[3]));
	if(argc==3 && QByteArray(argv[2])=="--editor-features"){VertexEditorTest::numericAndIsolation();UVEditorTest::synchronization();UVEditorTest::realSynchronization();materialEditorChecks();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--toolbar-ui"){VertexEditorTest::toolbarAndContext();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--uv-ui"){UVEditorTest::cubeProjection();UVEditorTest::textureChannels();UVEditorTest::run();UVEditorTest::cage();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--controls-ui"){VertexEditorTest::viewportControls();VertexEditorTest::axisSnapping();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--vertex-ui"){VertexEditorTest::run();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--face-ui"){VertexEditorTest::faces();VertexEditorTest::editModes();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--face-check"){faceMaterialChecks();editingStorageChecks();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--light-falloff"){LightingPreviewTest::falloff();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--light-indicators"){LightingPreviewTest::indicators();return 0;}
	if(argc==3 && QByteArray(argv[2])=="--lighting-check") {
		LightingPreviewTest::run();return 0;
	}
	if(argc==4 && QByteArray(argv[2])=="--alpha-inspect") {
		LightingPreviewTest::inspect(QString::fromLocal8Bit(argv[3]));return 0;
	}
	if(argc==4 && QByteArray(argv[2])=="--alpha-check") {
		LightingPreviewTest::realAlpha(QString::fromLocal8Bit(argv[3]));return 0;
	}
	if(argc==5 && QByteArray(argv[2])=="--physx-groups"){groupedCollisionChecks(QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));return 0;}
	if(argc==5 && QByteArray(argv[2])=="--physx-check") {
		checkPhysx(QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));return 0;
	}
	if(argc>3 && QByteArray(argv[2])=="--physx") {
		for(int i=3;i<argc;++i) inspectPhysx(QString::fromLocal8Bit(argv[i]));
		return 0;
	}
	if ( argc > 3 && QByteArray(argv[2]) == "--inspect" ) {
		for ( int i=3; i<argc; ++i ) inspectNiMesh(QString::fromLocal8Bit(argv[i]));
		return 0;
	}
	if ( argc > 3 && QByteArray(argv[2]) == "--mesh-check" ) {
		QOpenGLContext context;
		check(context.create(),"create mesh test OpenGL context");
		QOffscreenSurface surface;
		surface.setFormat(context.format());
		surface.create();
		check(context.makeCurrent(&surface),"activate mesh test OpenGL context");
		for (int i=3;i<argc;++i) checkNiMeshFile(QString::fromLocal8Bit(argv[i]), &context, context.functions());
		return 0;
	}
	NifModel model;
	model.insertNiBlock("NiNode");
	model.insertNiBlock("NiNode");
	model.insertNiBlock("NiNode");
	model.set<uint>(model.getBlock(0), "Num Children", 2);
	model.updateArray(model.getIndex(model.getBlock(0), "Children"));
	model.setLink(model.getIndex(model.getBlock(0), "Children").child(0,0), 1);
	model.setLink(model.getIndex(model.getBlock(0), "Children").child(1,0), 2);
	model.set<Vector3>(model.getBlock(1), "Translation", Vector3(3,4,5));
	model.set<QString>(model.getBlock(1), "Name", "Child");
	bytes(model);
	model.resetUndoHistory();
	SpellBook book(&model);
	auto before = bytes(model);
	check(model.undoStack->count() == 0, "read-only serialization creates no undo entry");

	model.set<float>(model.getBlock(1), "Scale", 2);
	model.flushUndoHistory();
	check(model.undoStack->count() == 1, "direct field edit recorded");
	model.undo();
	check(model.get<float>(model.getBlock(1), "Scale") == 1, "undo direct edit");
	model.redo();
	check(model.get<float>(model.getBlock(1), "Scale") == 2, "redo direct edit");
	model.undo();
	check(bytes(model) == before, "field undo restores exact file bytes");
	model.resetUndoHistory();

	book.cast(&model, model.getBlock(1), SpellBook::lookup("Block/Remove"));
	check(model.getBlockCount() == 2, "remove block spell");
	check(model.getLink(model.getIndex(model.getBlock(0), "Children").child(1,0)) == 1, "delete renumbers surviving links");
	model.undo();
	check(model.getBlockCount() == 3, "undo restores deleted block");
	check(bytes(model) == before, "undo delete restores blocks, strings, arrays and links exactly");
	model.redo();
	check(model.getBlockCount() == 2, "redo block deletion");
	model.undo();
	model.resetUndoHistory();

	book.cast(&model, model.getBlock(1), SpellBook::lookup("Block/Copy"));
	check(model.undoStack->count() == 0, "copy creates no undo entry");
	book.cast(&model, QModelIndex(), SpellBook::lookup("Block/Paste"));
	check(model.getBlockCount() == 4, "paste block spell");
	check(model.undoStack->count() == 1, "paste is a single undo step");
	auto pasted = bytes(model);
	model.undo();
	check(bytes(model) == before, "undo paste restores exact file bytes");
	model.redo();
	check(bytes(model) == pasted, "redo paste restores exact file bytes");

	model.beginUndoTransaction("Gizmo drag");
	for ( int i = 0; i < 20; ++i ) {
		Transform transform(&model, model.getBlock(1));
		transform.translation = Vector3(i, i*2, i*3);
		transform.rotation = Matrix::euler(.01f*i, .02f*i, .03f*i);
		transform.scale = 1 + i*.1f;
		transform.writeBack(&model, model.getBlock(1));
		QCoreApplication::processEvents();
	}
	model.endUndoTransaction();
	check(model.undoStack->count() == 2, "live transform drag creates one undo step");
	model.undo();
	check(bytes(model) == pasted, "undo drag restores translation, rotation and scale");
	model.redo();
	check(model.get<Vector3>(model.getBlock(1), "Translation") == Vector3(19,38,57), "redo drag");

	model.resetUndoHistory();
	Transform original(&model, model.getBlock(1));
	model.beginUndoTransaction("Cancelled drag");
	model.set<float>(model.getBlock(1), "Scale", 15);
	original.writeBack(&model, model.getBlock(1));
	model.endUndoTransaction();
	check(model.undoStack->count() == 0, "cancelled drag creates no history");
	check(model.undoStack->isClean(), "cancelled drag leaves document clean");

	// Mixing value edits and structural edits must not retain stale model indexes.
	model.set<float>(model.getBlock(1), "Scale", 8);
	model.flushUndoHistory();
	book.cast(&model, model.getBlock(1), SpellBook::lookup("Block/Remove"));
	model.undo();
	check(model.get<float>(model.getBlock(1), "Scale") == 8, "undo delete after field edit");
	model.undo();
	check(model.get<float>(model.getBlock(1), "Scale") == original.scale, "undo field edit across model restoration");
	model.redo();
	model.redo();
	check(model.getBlockCount() == 3, "redo mixed field edit and deletion");
	model.undo();
	model.undo();
	model.resetUndoHistory();

	model.set<uint>(model.getBlock(0), "Num Children", 1);
	model.flushUndoHistory();
	model.applyCommand(new ArrayUpdateCommand(model.getIndex(model.getBlock(0), "Children"), &model));
	model.flushUndoHistory();
	check(model.rowCount(model.getIndex(model.getBlock(0), "Children")) == 1, "shrink an array");
	model.undo();
	check(model.rowCount(model.getIndex(model.getBlock(0), "Children")) == 2, "undo array update restores removed rows");
	check(model.getLink(model.getIndex(model.getBlock(0), "Children").child(1,0)) == 2, "undo array update restores row contents");
	model.undo();
	check(model.get<uint>(model.getBlock(0), "Num Children") == 2, "undo array count edit");
	model.resetUndoHistory();

	auto translationIndex = model.getIndex(model.getBlock(1), "Translation");
	model.setData(translationIndex.sibling(translationIndex.row(), NifModel::NameCol), "Offset", Qt::EditRole);
	model.flushUndoHistory();
	check(model.getIndex(model.getBlock(1), "Offset").isValid(), "edit field metadata");
	model.undo();
	check(model.getIndex(model.getBlock(1), "Translation").isValid(), "undo restores field metadata");
	model.resetUndoHistory();

	model.insertNiBlock("NiBinaryExtraData");
	model.resetUndoHistory();
	int binaryBlock = model.getBlockCount()-1;
	model.set<QByteArray>(model.getBlock(binaryBlock), "Binary Data", QByteArray("abc\0def", 7));
	model.flushUndoHistory();
	model.undo();
	check(model.get<QByteArray>(model.getBlock(binaryBlock), "Binary Data").isEmpty(), "undo binary data edit");
	model.redo();
	check(model.get<QByteArray>(model.getBlock(binaryBlock), "Binary Data") == QByteArray("abc\0def", 7), "redo preserves binary data including zero bytes");
	model.removeNiBlock(binaryBlock);
	model.resetUndoHistory();

	int branchBlocks = model.getBlockCount();
	auto beforeBranch = bytes(model);
	book.cast(&model, model.getBlock(0), SpellBook::lookup("Block/Copy Branch"));
	book.cast(&model, model.getBlock(0), SpellBook::lookup("Block/Paste Branch"));
	check(model.getBlockCount() == branchBlocks + 3, "paste branch with linked children");
	model.undo();
	check(model.getBlockCount() == branchBlocks, "undo removes entire pasted branch");
	check(bytes(model) == beforeBranch, "undo branch paste restores parent links exactly");
	model.redo();
	check(model.getBlockCount() == branchBlocks + 3, "redo restores entire pasted branch");
	model.undo();
	model.resetUndoHistory();

	model.set<float>(model.getBlock(1), "Scale", 4);
	model.flushUndoHistory();
	model.undoStack->setClean();
	model.set<float>(model.getBlock(1), "Scale", 5);
	model.flushUndoHistory();
	check(!model.undoStack->isClean(), "edit after save marks document dirty");
	model.undo();
	check(model.undoStack->isClean(), "undo to saved state marks document clean");
	model.undo();
	model.set<float>(model.getBlock(1), "Scale", 7);
	model.flushUndoHistory();
	check(!model.undoStack->canRedo(), "new edit discards redo branch");
	model.resetUndoHistory();
	for ( int i = 0; i < 60; ++i ) {
		model.set<float>(model.getBlock(1), "Scale", 10+i);
		model.flushUndoHistory();
	}
	check(model.undoStack->count() == 50, "history is bounded to 50 operations");
	model.suspendUndoHistory();
	model.clear();
	model.resetUndoHistory();
	check(model.undoStack->count() == 0, "new document clears history");
	VertexEditorTest::storage();
	UVEditorTest::storage();
	VertexEditorTest::microvolts("build/qt-check/fixtures/Working.nif");
	VertexEditorTest::microvolts("build/qt-check/fixtures/NotWorking.nif");
	if ( argc > 2 && QByteArray(argv[2]) == "--gizmo" ) {
		GLViewGizmoTest::run();
		VertexEditorTest::run();
		PhysXCollisionTest::run();
		UVEditorTest::run();
		WorkspaceTest::run();
		WorkspaceTest::drops();
		ViewportResizeTest::run();
		LightingPreviewTest::run();
	}
	return 0;
}
