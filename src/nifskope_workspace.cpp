#include "nifskope.h"
#include "ui_nifskope.h"
#include "glview.h"
#include "model/nifmodel.h"
#include "model/nifproxymodel.h"
#include "spellbook.h"
#include "physx/physxcollision.h"
#include "ui/widgets/nifview.h"
#include "ui/widgets/refrbrowser.h"
#include "ui/widgets/inspect.h"
#include <QTabBar>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QSaveFile>
#include <QSlider>
#include <QWidgetAction>
#include <QLabel>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QApplication>
#include <QStatusBar>
#include <QDir>
#include <vector>

void NifSkope::initWorkspace()
{
	WorkspaceDocument first;first.model=nif;first.path=currentFile;
	first.showHidden=ui->aShowHidden->isChecked();first.showCollision=ui->aShowCollision->isChecked();documents<<first;
	connectDocument(nif);
	auto container=new QWidget(this);auto layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
	documentTabs=new QTabBar(container);documentTabs->setObjectName("workspaceTabs");documentTabs->setExpanding(false);documentTabs->setTabsClosable(true);
	layout->addWidget(documentTabs);layout->addWidget(takeCentralWidget());setCentralWidget(container);
	connect(documentTabs,&QTabBar::currentChanged,this,&NifSkope::activateDocument);
	connect(documentTabs,&QTabBar::tabCloseRequested,this,&NifSkope::removeWorkspaceDocument);
	auto menu=menuBar()->addMenu(tr("Workspace"));
	auto add=menu->addAction(tr("Add NIF to Workspace..."));add->setObjectName("aWorkspaceAdd");
	connect(add,&QAction::triggered,this,[this](){
		QString path=QFileDialog::getOpenFileName(this,tr("Add NIF to Workspace"),QFileInfo(currentFile).absolutePath(),tr("NIF files (*.nif *.btr *.bto)"));
		if(path.isEmpty())return;
		QString error;if(!addWorkspaceFile(path,error))QMessageBox::warning(this,tr("Workspace"),error);
	});
	auto saveAll=menu->addAction(tr("Save All NIFs"));saveAll->setObjectName("aWorkspaceSaveAll");saveAll->setShortcut(QKeySequence("Ctrl+Shift+S"));
	connect(saveAll,&QAction::triggered,this,[this](){if(documents.size()==1){save();return;}QString error;if(!saveWorkspace(error))QMessageBox::warning(this,tr("Save All NIFs"),error);});
	menu->addSeparator();
	auto show=menu->addAction(tr("Show Reference NIFs"));show->setObjectName("aWorkspaceReferences");show->setCheckable(true);show->setChecked(true);
	connect(show,&QAction::toggled,ogl,&GLView::setReferencesVisible);
	auto opacityAction=new QWidgetAction(menu);auto opacityRow=new QWidget(menu);auto row=new QHBoxLayout(opacityRow);
	row->addWidget(new QLabel(tr("Reference opacity"),opacityRow));auto opacity=new QSlider(Qt::Horizontal,opacityRow);opacity->setObjectName("workspaceOpacity");opacity->setRange(0,100);opacity->setValue(30);row->addWidget(opacity);
	opacityAction->setDefaultWidget(opacityRow);menu->addAction(opacityAction);
	connect(opacity,&QSlider::valueChanged,ogl,&GLView::setReferenceOpacity);
	updateWorkspaceUi();
}

void NifSkope::connectDocument(NifModel * model)
{
	auto changed=[this](){updateWorkspaceUi();};
	connect(model->undoStack,&QUndoStack::cleanChanged,this,changed);
	connect(model->undoStack,&QUndoStack::canUndoChanged,this,changed);
	connect(model->undoStack,&QUndoStack::canRedoChanged,this,changed);
	connect(model->undoStack,&QUndoStack::undoTextChanged,this,changed);
	connect(model->undoStack,&QUndoStack::redoTextChanged,this,changed);
	auto undoBlock=std::make_shared<int>(-1);
	connect(model,&NifModel::undoAboutToRestore,this,[this,model,undoBlock](){if(model==nif)*undoBlock=nif->getBlockNumber(ogl->getScene()->currentBlock);});
	connect(model,&NifModel::undoRestored,this,[this,model,undoBlock](){
		if(model!=nif)return;
		indexStack->clear();currentIdx=QModelIndex();header->setRootIndex(nif->getHeader());
		auto block=nif->getBlock(*undoBlock);select(block.isValid()?block:nif->getHeader());indexStack->clear();updateWorkspaceUi();
	});
	connect(model,&NifModel::sigProgress,this,[this,model](int c,int maximum){if(model!=nif || workspaceBusy)return;progress->setRange(0,maximum);progress->setValue(c);qApp->processEvents();});
	connect(model,&NifModel::lodSliderChanged,this,[this,model](bool enabled){if(model==nif){ui->tLOD->setEnabled(enabled);ui->tLOD->setVisible(enabled);}});
	connect(model,&NifModel::modelReset,this,[this,model](){if(model==nif)emit activeDocumentChanged();});
	connect(model,&NifModel::linksChanged,this,[this,model](){if(model==nif)emit activeDocumentChanged();});
}

void NifSkope::updateWorkspaceUi()
{
	if(documents.isEmpty() || !documentTabs)return;
	QSignalBlocker blocker(documentTabs);bool dirty=false;
	while(documentTabs->count()<documents.size())documentTabs->addTab(QString());
	while(documentTabs->count()>documents.size())documentTabs->removeTab(documentTabs->count()-1);
	for(int i=0;i<documents.size();++i) {
		bool modified=!documents[i].model->undoStack->isClean();dirty|=modified;
		auto label=QFileInfo(documents[i].path).fileName();if(label.isEmpty())label=tr("Untitled NIF");
		documentTabs->setTabText(i,label+(modified?" *":""));documentTabs->setTabToolTip(i,documents[i].path);
	}
	documentTabs->setCurrentIndex(activeDocument);documentTabs->setVisible(documents.size()>1);
	setWindowModified(dirty);undoAction->setEnabled(nif->undoStack->canUndo());redoAction->setEnabled(nif->undoStack->canRedo());
	undoAction->setText(nif->undoStack->undoText().isEmpty()?tr("&Undo"):tr("&Undo %1").arg(nif->undoStack->undoText()));
	redoAction->setText(nif->undoStack->redoText().isEmpty()?tr("&Redo"):tr("&Redo %1").arg(nif->undoStack->redoText()));
}

bool NifSkope::addWorkspaceFile(const QString & filename,QString & error)
{
	if(workspaceBusy){error=tr("Wait for the current file operation to finish.");return false;}
	QFileInfo info(filename);QString path=info.canonicalFilePath();
	if(path.isEmpty() || !info.isFile()){error=tr("The reference NIF does not exist.");return false;}
	if(info.suffix().compare("kfm",Qt::CaseInsensitive)==0 || currentFile.endsWith(".kfm",Qt::CaseInsensitive)){error=tr("Workspaces support NIF documents. Open KFM files in a separate window.");return false;}
	for(int i=0;i<documents.size();++i)if(QFileInfo(documents[i].path).canonicalFilePath().compare(path,Qt::CaseInsensitive)==0){activateDocument(i);return true;}
	auto model=new NifModel(this);model->setMessageMode(BaseModel::UserMessage);
	// Loading the second model never clears or modifies the active document.
	workspaceBusy=true;bool loaded=model->loadFromFile(path);workspaceBusy=false;
	if(!loaded){delete model;error=tr("Could not load %1. The open documents were left unchanged.").arg(path);return false;}
	model->resetUndoHistory();
	if(documents.size()==1 && documents[0].path.isEmpty() && nif->getBlockCount()==0) {
		// An empty initial window has no content to retain as a reference.
		delete model;loadFile(path);return true;
	}
	WorkspaceDocument document;document.model=model;document.path=path;document.showHidden=ui->aShowHidden->isChecked() || PhysXCollision::hasCollision(model);
	documents<<document;connectDocument(model);updateWorkspaceUi();refreshReferences();
	statusBar()->showMessage(tr("Added %1 as a reference. Click its tab to edit it.").arg(info.fileName()),10000);return true;
}

void NifSkope::activateDocument(int index)
{
	if(workspaceBusy || index<0 || index>=documents.size() || index==activeDocument)return;
	workspaceBusy=true;ogl->finishGizmo(true);nif->flushUndoHistory();
	auto & previous=documents[activeDocument];previous.path=currentFile;previous.selectedBlock=nif->getBlockNumber(ogl->getScene()->currentBlock);
	previous.editBlock=nif->getBlockNumber(ogl->editMesh);previous.vertexMode=ogl->getScene()->selMode==Scene::SelVertex;previous.selectedVertices=ogl->selectedVertices;previous.faceMode=ogl->faceSelection;previous.selectedFaces=ogl->selectedFaces;
	previous.showHidden=ui->aShowHidden->isChecked();previous.showCollision=ui->aShowCollision->isChecked();
	selecting=true;indexStack->clear();currentIdx=QModelIndex();
	activeDocument=index;nif=documents[index].model;currentFile=documents[index].path;
	book->sltNif(nif);
	proxy->setModel(nif);list->setModel(proxy);tree->setModel(nif);header->setModel(nif);
	for(auto view:{list,tree,header}){auto delegate=view->itemDelegate();view->setItemDelegate(nif->createDelegate(this,book));delete delegate;}
	refrbrwsr->setNifModel(nif);inspect->setNifModel(nif);ogl->setNif(nif);
	ogl->getScene()->selMode=Scene::SelObject;ui->aSelectObject->setChecked(true);
	if(ui->aShowHidden->isChecked()!=documents[index].showHidden)ui->aShowHidden->trigger();
	if(ui->aShowCollision->isChecked()!=documents[index].showCollision)ui->aShowCollision->trigger();
	setWindowFilePath(currentFile);header->setRootIndex(nif->getHeader());selecting=false;
	const auto & document=documents[index];auto block=nif->getBlock(document.selectedBlock);
	select(block.isValid()?block:nif->getHeader());
	if(document.vertexMode && nif->getBlock(document.editBlock).isValid()) {
		ogl->setCurrentIndex(nif->getBlock(document.editBlock));ui->aSelectVertex->trigger();if(document.faceMode){if(auto face=findChild<QAction *>("aSelectFace"))face->trigger();ogl->selectedFaces=document.selectedFaces;}ogl->selectedVertices=document.selectedVertices;ogl->refreshEditData();
	}
	indexStack->clear();setListMode();enableUi();
	bool modern=nif->getVersionNumber()>=0x14050000;mExport->setDisabled(modern);mImport->setDisabled(modern || nif->getUserVersion2()>=100);
	workspaceBusy=false;updateWorkspaceUi();refreshReferences();emit activeDocumentChanged();ogl->update();
}

void NifSkope::refreshReferences()
{
	if(!ogl)return;
	QList<NifModel *> models;
	if(documents.size()>1)for(const auto & document:documents)models<<document.model;
	ogl->setReferenceModels(models);
}

bool NifSkope::saveWorkspace(QString & error)
{
	if(workspaceBusy){error=tr("Another file operation is still running.");return false;}
	ogl->finishGizmo(false);
	QSet<QString> paths;
	for(const auto & document:documents) {
		QFileInfo info(document.path);
		if(document.path.isEmpty() || !info.isAbsolute()){error=tr("Use Save As to give every NIF its own local filename before Save All.");return false;}
		QString resolved=info.canonicalFilePath();if(resolved.isEmpty())resolved=info.absoluteFilePath();
		QString key=QDir::cleanPath(resolved).toLower();
		if(paths.contains(key)){error=tr("Two workspace documents have the same destination. Choose separate filenames.");return false;}paths.insert(key);
	}
	workspaceBusy=true;setEnabled(false);QApplication::setOverrideCursor(Qt::WaitCursor);
	std::vector<std::unique_ptr<QSaveFile>> outputs;
	bool ok=true;
	for(const auto & document:documents) {
		document.model->flushUndoHistory();
		if(aSanitize->isChecked())SpellBook::sanitize(document.model);
		std::unique_ptr<QSaveFile> output(new QSaveFile(document.path));
		if(!output->open(QIODevice::WriteOnly) || !document.model->save(*output)){error=tr("Could not stage %1: %2. No workspace files were replaced.").arg(document.path,output->errorString());ok=false;break;}
		outputs.push_back(std::move(output));
	}
	QStringList saved;
	if(ok)for(int i=0;i<documents.size();++i) {
		if(!outputs[i]->commit()){error=tr("Could not replace %1: %2. Files already saved: %3").arg(documents[i].path,outputs[i]->errorString(),saved.isEmpty()?tr("none"):saved.join(", "));ok=false;break;}
		saved<<QFileInfo(documents[i].path).fileName();documents[i].model->flushUndoHistory();documents[i].model->undoStack->setClean();
	}
	QApplication::restoreOverrideCursor();setEnabled(true);workspaceBusy=false;updateWorkspaceUi();
	if(ok)statusBar()->showMessage(tr("Saved %1 separate NIF files.").arg(saved.size()),8000);
	return ok;
}

bool NifSkope::confirmWorkspace()
{
	QStringList dirty;
	for(const auto & document:documents){document.model->flushUndoHistory();if(!document.model->undoStack->isClean())dirty<<QFileInfo(document.path).fileName();}
	if(dirty.isEmpty() || cfg.suppressSaveConfirm)return true;
	auto response=QMessageBox::question(this,tr("Unsaved Workspace Changes"),tr("Save changes to these separate NIF files?\n%1").arg(dirty.join("\n")),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
	if(response==QMessageBox::Cancel)return false;
	if(response==QMessageBox::Discard)return true;
	QString error;bool ok=saveWorkspace(error);if(!ok)QMessageBox::warning(this,tr("Save All NIFs"),error);return ok;
}

void NifSkope::removeWorkspaceDocument(int index)
{
	if(workspaceBusy || documents.size()<2 || index<0 || index>=documents.size())return;
	auto model=documents[index].model;model->flushUndoHistory();
	if(!model->undoStack->isClean()) {
		auto response=QMessageBox::question(this,tr("Remove NIF from Workspace"),tr("%1 has unsaved changes. Save the workspace before removing it?").arg(QFileInfo(documents[index].path).fileName()),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
		if(response==QMessageBox::Cancel)return;
		if(response==QMessageBox::Save){QString error;if(!saveWorkspace(error)){QMessageBox::warning(this,tr("Save All NIFs"),error);return;}}
	}
	if(index==activeDocument)activateDocument(index==0?1:0);
	documents.remove(index);if(activeDocument>index)--activeDocument;
	refreshReferences();delete model;updateWorkspaceUi();
}
