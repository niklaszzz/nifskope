#include "materialpanel.h"
#include "model/nifmodel.h"
#include "spellbook.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QTimer>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QFileDialog>
#include <QDir>
#include <QScrollArea>
#include <QBuffer>

namespace {
const char * textureSlots[]={"Base Texture","Dark Texture","Detail Texture","Gloss Texture","Glow Texture","Bump Map Texture","Normal Texture","Parallax Texture","Decal 0 Texture","Decal 1 Texture","Decal 2 Texture","Decal 3 Texture"};
const char * colorFields[]={"Ambient Color","Diffuse Color","Specular Color","Emissive Color"};
bool editableMesh(const NifModel * model,const QModelIndex & mesh) {
 return model && mesh.model()==model && (model->inherits(mesh,"NiMesh") || model->inherits(mesh,"NiTriBasedGeom")) && model->getIndex(mesh,"Properties").isValid() && model->evalCondition(static_cast<NifItem *>(model->getIndex(mesh,"Properties").internalPointer()),true);
}
QVector<qint32> effectiveProperties(const NifModel * model,QModelIndex block) {
 QVector<qint32> result;QSet<QString> types;QSet<int> visited;
 while(block.isValid() && !visited.contains(model->getBlockNumber(block))) {
  visited.insert(model->getBlockNumber(block));
  for(int id:model->getLinkArray(block,"Properties")) {
   auto property=model->getBlock(id);QString type=model->getBlockName(property);
   if(property.isValid() && !types.contains(type)){types.insert(type);result<<id;}
  }
  block=model->getBlock(model->getParent(block));
 }
 return result;
}
bool bundleProperty(const NifModel * model,int id) {
 auto block=model->getBlock(id);return model->inherits(block,"NiMaterialProperty") || model->inherits(block,"NiTexturingProperty") || model->inherits(block,"NiTextureProperty") || model->inherits(block,"NiAlphaProperty") || model->inherits(block,"NiSpecularProperty");
}
}

int MaterialPanel::assignedMaterial(const NifModel * model,const QModelIndex & mesh) {
 if(!model || !mesh.isValid() || mesh.model()!=model)return -1;
 for(int id:effectiveProperties(model,mesh))if(model->inherits(model->getBlock(id),"NiMaterialProperty"))return id;
 return -1;
}

int MaterialPanel::createMaterial(NifModel * model,const QString & requested) {
 if(!model)return -1;
 QString original=requested.trimmed();if(original.isEmpty())original="Material_01";
 QSet<QString> used;for(int b=0;b<model->getBlockCount();++b)if(model->inherits(model->getBlock(b),"NiMaterialProperty"))used.insert(model->get<QString>(model->getBlock(b),"Name"));
 QString candidate=original;auto match=QRegularExpression("^(.*?)([0-9]+)$").match(original);QString prefix=match.hasMatch()?match.captured(1):original+"_";int digits=match.hasMatch()?match.captured(2).size():2;qulonglong number=match.hasMatch()?match.captured(2).toULongLong():0;
 while(used.contains(candidate))candidate=prefix+QString::number(++number).rightJustified(digits,'0');
 model->beginUndoTransaction(QObject::tr("Create material"));auto material=model->insertNiBlock("NiMaterialProperty");
 model->set<QString>(material,"Name",candidate);model->set<Color3>(material,"Ambient Color",Color3(.2f,.2f,.2f));model->set<Color3>(material,"Diffuse Color",Color3(1,1,1));model->set<Color3>(material,"Specular Color",Color3(0,0,0));model->set<Color3>(material,"Emissive Color",Color3(0,0,0));model->set<float>(material,"Alpha",1);model->set<float>(material,"Glossiness",10);
 model->updateHeader();model->endUndoTransaction();return model->getBlockNumber(material);
}

bool MaterialPanel::assignMaterial(NifModel * model,const QModelIndex & target,int material,QString * reason) {
 auto fail=[&](const QString & text){if(reason)*reason=text;return false;};
 if(!editableMesh(model,target))return fail(QObject::tr("Select a mesh with editable material properties."));
 if(!model->inherits(model->getBlock(material),"NiMaterialProperty"))return fail(QObject::tr("Select a material from this file."));
 QPersistentModelIndex mesh=target;QVector<qint32> bundle{material};bool copyTextures=false;
 // Material properties and texture/alpha properties are separate NIF blocks.
 // Assign the source mesh's material bundle together when one is available.
 for(int b=0;b<model->getBlockCount();++b){auto source=model->getBlock(b);
  if(source==mesh || !editableMesh(model,source) || assignedMaterial(model,source)!=material)continue;
  copyTextures=true;for(int id:effectiveProperties(model,source))if(id!=material && !model->inherits(model->getBlock(id),"NiMaterialProperty") && bundleProperty(model,id))bundle<<id;break;
 }
 auto oldName=model->get<QString>(model->getBlock(assignedMaterial(model,mesh)),"Name");
 auto links=model->getLinkArray(mesh,"Properties");
 for(int i=links.size()-1;i>=0;--i)if(model->inherits(model->getBlock(links[i]),"NiMaterialProperty") || (copyTextures && bundleProperty(model,links[i])))links.remove(i);
 for(int id:bundle)if(!links.contains(id))links<<id;
 model->beginUndoTransaction(QObject::tr("Assign material"));bool held=model->holdUpdates(true);
 model->set<uint>(mesh,"Num Properties",links.size());model->updateArray(mesh,"Properties");bool ok=model->setLinkArray(mesh,"Properties",links);
 auto materialData=model->getIndex(mesh,"Material Data");auto names=materialData.isValid()?model->getIndex(materialData,"Material Name"):QModelIndex();
 for(int r=0;names.isValid() && r<model->rowCount(names);++r){auto field=names.child(r,0);if(model->rowCount(names)==1 || model->get<QString>(field)==oldName)model->set<QString>(field,model->get<QString>(model->getBlock(material),"Name"));}
 model->updateHeader();model->holdUpdates(held);emit model->dataChanged(mesh,mesh);model->endUndoTransaction();if(reason)reason->clear();return ok;
}

MaterialPanel::MaterialPanel(QWidget * parent):QWidget(parent) {
 setObjectName("materialPanel");setMinimumWidth(330);auto layout=new QVBoxLayout(this);auto form=new QFormLayout;
 targetLabel=new QLabel(this);targetLabel->setObjectName("materialTarget");targetLabel->setWordWrap(true);layout->addWidget(targetLabel);
 materials=new QComboBox(this);materials->setObjectName("materialList");form->addRow(tr("Material"),materials);
 name=new QLineEdit(this);name->setObjectName("materialName");form->addRow(tr("Name"),name);
 for(int i=0;i<4;++i){colors[i]=new QPushButton(tr("Choose color"),this);colors[i]->setObjectName(QString("materialColor%1").arg(i));form->addRow(tr(colorFields[i]),colors[i]);connect(colors[i],&QPushButton::clicked,this,[this,i]{auto color=QColorDialog::getColor(values[i],this,tr(colorFields[i]));if(color.isValid()){values[i]=color;colors[i]->setText(color.name());colors[i]->setStyleSheet(QString("background-color: %1; color: %2;").arg(color.name(),color.lightness()>128?"black":"white"));}});}
 opacity=new QDoubleSpinBox(this);opacity->setObjectName("materialOpacity");opacity->setRange(0,1);opacity->setDecimals(3);opacity->setSingleStep(.05);form->addRow(tr("Opacity"),opacity);
 glossiness=new QDoubleSpinBox(this);glossiness->setObjectName("materialGlossiness");glossiness->setRange(0,1000);glossiness->setDecimals(2);form->addRow(tr("Glossiness"),glossiness);layout->addLayout(form);
 usageLabel=new QLabel(this);usageLabel->setObjectName("materialUsage");usageLabel->setWordWrap(true);layout->addWidget(usageLabel);
 textureLabel=new QLabel(this);textureLabel->setWordWrap(true);textureLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(textureLabel);
 auto textureArea=new QScrollArea(this);textureArea->setWidgetResizable(true);textureArea->setMinimumHeight(160);
 auto textureContents=new QWidget(textureArea);auto textureForm=new QFormLayout(textureContents);
 for(int i=0;i<12;++i){
  auto row=new QHBoxLayout;texturePaths[i]=new QLineEdit(textureContents);texturePaths[i]->setObjectName(QString("materialTexture%1").arg(i));texturePaths[i]->setPlaceholderText(tr("No texture"));texturePaths[i]->setToolTip(tr("Enter a texture path. Clear it to remove this map. Changes affect only the selected mesh."));
  textureBrowse[i]=new QPushButton(tr("..."),textureContents);textureBrowse[i]->setFixedWidth(26);row->addWidget(texturePaths[i]);row->addWidget(textureBrowse[i]);textureForm->addRow(tr(textureSlots[i]),row);
  connect(textureBrowse[i],&QPushButton::clicked,this,[this,i]{QString path=QFileDialog::getOpenFileName(this,tr("Choose texture"),model?model->getFolder():QString(),tr("Textures (*.dds *.tga *.png *.bmp *.jpg *.jpeg);;All files (*)"));if(!path.isEmpty()){if(model && !model->getFolder().isEmpty())path=QDir(model->getFolder()).relativeFilePath(path);texturePaths[i]->setText(QDir::fromNativeSeparators(path));}});
 }
 textureArea->setWidget(textureContents);layout->addWidget(textureArea);
 auto row=new QHBoxLayout;assign=new QPushButton(tr("Assign to Mesh"),this);assign->setObjectName("assignMaterial");create=new QPushButton(tr("New + Assign"),this);create->setObjectName("createMaterial");row->addWidget(assign);row->addWidget(create);layout->addLayout(row);
 unique=new QPushButton(tr("Make Assigned Material Unique"),this);unique->setObjectName("uniqueMaterial");layout->addWidget(unique);
 save=new QPushButton(tr("Apply Material Changes"),this);save->setObjectName("applyMaterial");layout->addWidget(save);layout->addStretch();
 refreshTimer=new QTimer(this);refreshTimer->setSingleShot(true);connect(refreshTimer,&QTimer::timeout,this,&MaterialPanel::refresh);
 connect(materials,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{preferred=materials->currentData().toInt();readFields();});
 connect(assign,&QPushButton::clicked,this,[this]{QString error;if(assignMaterial(model,mesh,materials->currentData().toInt(),&error))refresh();else usageLabel->setText(error);});
 connect(create,&QPushButton::clicked,this,[this]{if(!editableMesh(model,mesh))return;model->beginUndoTransaction(tr("Create and assign material"));preferred=createMaterial(model);assignMaterial(model,mesh,preferred);model->endUndoTransaction();refresh();});
 connect(unique,&QPushButton::clicked,this,[this]{auto spell=SpellBook::lookup("Mesh/Make Material Unique");if(model && spell && editableMesh(model,mesh)){spell->cast(model,mesh);preferred=assignedMaterial(model,mesh);refresh();}});
 connect(save,&QPushButton::clicked,this,&MaterialPanel::apply);refresh();
}

void MaterialPanel::setTarget(NifModel * document,const QModelIndex & target) {
 if(applying)return;
 if(model!=document){
  for(auto connection:connections)disconnect(connection);connections.clear();model=document;preferred=lastAssigned=-1;
  if(model){
   auto schedule=[this]{refreshTimer->start(0);};
   connections<<connect(model,&NifModel::dataChanged,this,schedule)<<connect(model,&NifModel::linksChanged,this,schedule)<<connect(model,&NifModel::modelReset,this,schedule);
   connections<<connect(model,&NifModel::undoAboutToRestore,this,[this]{restoreMesh=model->getBlockNumber(mesh);});
   connections<<connect(model,&NifModel::undoRestored,this,[this]{mesh=model->getBlock(restoreMesh);refreshTimer->start(0);});
   connections<<connect(model,&NifModel::destroyed,this,[this]{model=nullptr;mesh=QModelIndex();refresh();});
  }
 }
 if(mesh!=target){mesh=target;preferred=lastAssigned=-1;}refresh();
}

void MaterialPanel::refresh() {
 if(applying)return;
 int assigned=assignedMaterial(model,mesh);if(assigned!=lastAssigned){preferred=assigned;lastAssigned=assigned;}
 QSignalBlocker blocker(materials);materials->clear();
 if(model)for(int b=0;b<model->getBlockCount();++b)if(model->inherits(model->getBlock(b),"NiMaterialProperty"))materials->addItem(QString("%1 (#%2)").arg(model->get<QString>(model->getBlock(b),"Name")).arg(b),b);
 int index=materials->findData(preferred);if(index<0 && materials->count())index=0;materials->setCurrentIndex(index);preferred=index>=0?materials->currentData().toInt():-1;
 bool target=editableMesh(model,mesh);targetLabel->setText(target?tr("Mesh: %1 (#%2)").arg(model->get<QString>(mesh,"Name")).arg(model->getBlockNumber(mesh)):tr("Select a mesh to create or assign a material."));
 assign->setEnabled(target && index>=0);create->setEnabled(target);unique->setEnabled(target && assigned>=0);readFields();
}

void MaterialPanel::readFields() {
 readTextures();
 auto material=model?model->getBlock(materials->currentData().toInt()):QModelIndex();bool valid=model && materials->currentIndex()>=0 && model->inherits(material,"NiMaterialProperty");
 name->setEnabled(valid);opacity->setEnabled(valid);glossiness->setEnabled(valid);save->setEnabled(valid);for(auto button:colors)button->setEnabled(valid);
 if(!valid){name->clear();usageLabel->setText(tr("No material selected."));textureLabel->clear();return;}
 name->setText(model->get<QString>(material,"Name"));opacity->setValue(model->get<float>(material,"Alpha"));glossiness->setValue(model->get<float>(material,"Glossiness"));
 for(int i=0;i<4;++i){auto color=model->get<Color3>(material,colorFields[i]);values[i]=color.toQColor();colors[i]->setText(values[i].name());colors[i]->setStyleSheet(QString("background-color: %1; color: %2;").arg(values[i].name(),values[i].lightness()>128?"black":"white"));}
 int id=model->getBlockNumber(material),users=0;QString texture;
 for(int b=0;b<model->getBlockCount();++b){auto candidate=model->getBlock(b);if(!editableMesh(model,candidate) || assignedMaterial(model,candidate)!=id)continue;++users;
  if(texture.isEmpty())for(int prop:effectiveProperties(model,candidate))if(model->inherits(model->getBlock(prop),"NiTexturingProperty")){int source=model->getLink(model->getIndex(model->getBlock(prop),"Base Texture"),"Source");texture=model->get<QString>(model->getBlock(source),"File Name");}
 }
 usageLabel->setText(tr("%1 mesh(es) use this material.%2").arg(users).arg(users>1?tr(" Changes apply to all users. Make Unique creates an independent copy for the selected mesh."):QString()));
 textureLabel->setText(texture.isEmpty()?tr("Base texture: none"):tr("Base texture: %1").arg(texture));
}

void MaterialPanel::apply() {
 if(!model || materials->currentIndex()<0)return;auto material=model->getBlock(materials->currentData().toInt());if(!model->inherits(material,"NiMaterialProperty"))return;
 QString text=name->text().trimmed();if(text.isEmpty()){usageLabel->setText(tr("Enter a material name."));return;}
 applying=true;
 model->beginUndoTransaction(tr("Edit material"));QString oldName=model->get<QString>(material,"Name");int id=model->getBlockNumber(material);
 model->set<QString>(material,"Name",text);for(int i=0;i<4;++i)model->set<Color3>(material,colorFields[i],Color3(values[i]));model->set<float>(material,"Alpha",opacity->value());model->set<float>(material,"Glossiness",glossiness->value());
 for(int b=0;b<model->getBlockCount();++b){auto candidate=model->getBlock(b);if(!editableMesh(model,candidate) || assignedMaterial(model,candidate)!=id)continue;auto data=model->getIndex(candidate,"Material Data");auto names=data.isValid()?model->getIndex(data,"Material Name"):QModelIndex();for(int r=0;names.isValid() && r<model->rowCount(names);++r)if(model->get<QString>(names.child(r,0))==oldName)model->set<QString>(names.child(r,0),text);}
 applyTextures();model->updateHeader();model->endUndoTransaction();applying=false;refresh();
}

void MaterialPanel::readTextures() {
 QModelIndex property;
 bool target=editableMesh(model,mesh) && materials->currentIndex()>=0 && assignedMaterial(model,mesh)==materials->currentData().toInt();
 if(target)for(int id:effectiveProperties(model,mesh))if(model->inherits(model->getBlock(id),"NiTexturingProperty")){property=model->getBlock(id);break;}
 const bool modern=model && model->getVersionNumber()>=0x14020005;
 for(int i=0;i<12;++i){
  bool supported=target && model->getVersionNumber()>=0x0303000d && (modern || (i!=6 && i!=7));QString path;
  texturePaths[i]->setPlaceholderText(tr("No texture"));
  if(property.isValid() && model->get<bool>(property,QString("Has %1").arg(textureSlots[i]))){auto source=model->getBlock(model->getLink(model->getIndex(property,textureSlots[i]),"Source"));if(model->get<quint8>(source,"Use External"))path=model->get<QString>(source,"File Name");else if(source.isValid())texturePaths[i]->setPlaceholderText(tr("Embedded texture (preserved)"));}
  texturePaths[i]->setText(path);originalPaths[i]=path;texturePaths[i]->setEnabled(supported);textureBrowse[i]->setEnabled(supported);
 }
 textureLabel->setToolTip(tr("Texture slots belong to the selected mesh. Assign the chosen material before editing its slots. Shared texture blocks are copied when changed."));
}

void MaterialPanel::applyTextures() {
 if(!editableMesh(model,mesh) || assignedMaterial(model,mesh)!=materials->currentData().toInt())return;
 bool changed=false;for(int i=0;i<12;++i)if(texturePaths[i]->isEnabled() && texturePaths[i]->text().trimmed()!=originalPaths[i])changed=true;if(!changed)return;
 QPersistentModelIndex property;for(int id:effectiveProperties(model,mesh))if(model->inherits(model->getBlock(id),"NiTexturingProperty")){property=model->getBlock(id);break;}
 // Copy descriptors before editing so shared and inherited slots stay intact.
 QByteArray data;QString type="NiTexturingProperty";
 if(property.isValid()){type=model->getBlockName(property);QBuffer buffer(&data);buffer.open(QIODevice::WriteOnly);if(!model->saveIndex(buffer,property))return;}
 auto copy=model->insertNiBlock(type);if(!data.isEmpty()){QBuffer buffer(&data);buffer.open(QIODevice::ReadOnly);model->loadIndex(buffer,copy);}property=copy;
 auto links=model->getLinkArray(mesh,"Properties");for(int i=links.size()-1;i>=0;--i)if(model->inherits(model->getBlock(links[i]),"NiTexturingProperty"))links.remove(i);links<<model->getBlockNumber(property);model->set<uint>(mesh,"Num Properties",links.size());model->updateArray(mesh,"Properties");model->setLinkArray(mesh,"Properties",links);
 const bool modern=model->getVersionNumber()>=0x14020005;
 for(int i=0;i<12;++i){QString path=texturePaths[i]->text().trimmed();if(!texturePaths[i]->isEnabled() || path==originalPaths[i])continue;
  const QString slot=textureSlots[i];if(path.isEmpty()){model->set<bool>(property,"Has "+slot,false);continue;}
  uint needed=i<6?uint(i+1):uint(modern?i+1:i-1);model->set<uint>(property,"Texture Count",qMax(model->get<uint>(property,"Texture Count"),needed));
  bool existed=model->get<bool>(property,"Has "+slot);model->set<bool>(property,"Has "+slot,true);
  QPersistentModelIndex descriptor=model->getIndex(property,slot);if(!existed)model->set<quint16>(descriptor,"Flags",0x3200);
  auto source=model->insertNiBlock("NiSourceTexture");model->set<quint8>(source,"Use External",1);model->set<QString>(source,"File Name",QDir::fromNativeSeparators(path));model->setLink(descriptor,"Source",model->getBlockNumber(source));
 }
}
