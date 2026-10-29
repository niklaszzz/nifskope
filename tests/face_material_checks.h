#include "model/nifproxymodel.h"
static void faceMaterialChecks()
{
 NifModel model;VertexEditorTest::fixture(model);auto original=bytes(model);FaceEditData faces;
 check(faces.read(&model,model.getBlock(1)) && faces.triangles.size()==2,"read classic faces");
 model.beginUndoTransaction("Delete face");check(faces.remove(&model,QSet<int>{0}),"delete classic face");model.endUndoTransaction();
 check(faces.read(&model,model.getBlock(1)) && faces.triangles==QVector<Triangle>{Triangle(1,3,2)},"keep other triangle and winding");
 auto deleted=bytes(model);model.undo();check(bytes(model)==original,"undo exact face deletion");model.redo();check(bytes(model)==deleted,"redo exact face deletion");
 NifModel loaded;QBuffer input(&deleted);input.open(QIODevice::ReadOnly);check(loaded.load(input) && faces.read(&loaded,loaded.getBlock(1)) && faces.triangles.size()==1,"face deletion save/reload");
 auto material=model.insertNiBlock("NiMaterialProperty");int mat=model.getBlockNumber(material);model.set<QString>(material,"Name","FOOTING_01");
 int tex=model.getBlockNumber(model.insertNiBlock("NiTexturingProperty"));
 for(int id:{1,3}){auto mesh=model.getBlock(id);model.set<uint>(mesh,"Num Properties",2);model.updateArray(mesh,"Properties");model.setLinkArray(mesh,"Properties",QVector<qint32>{mat,tex});}
 model.resetUndoHistory();original=bytes(model);auto unique=SpellBook::lookup("Mesh/Make Material Unique");check(bool(unique),"find unique material action");unique->cast(&model,model.getBlock(3));
 auto links=model.getLinkArray(model.getBlock(3),"Properties");check(links.size()==2 && links[0]!=mat && links[1]!=tex,"copy material and texture properties");
 check(model.get<QString>(model.getBlock(links[0]),"Name")=="FOOTING_02","increment padded suffix");
 check(model.getLinkArray(model.getBlock(1),"Properties")==QVector<qint32>({mat,tex}) && model.get<QString>(model.getBlock(mat),"Name")=="FOOTING_01","original material unchanged");
 auto uniqueBytes=bytes(model);model.undo();check(bytes(model)==original,"unique material undo one step");model.redo();check(bytes(model)==uniqueBytes,"unique material redo exact document");
 model.set<QString>(model.getBlock(mat),"Name","Plain");unique->cast(&model,model.getBlock(1));check(model.get<QString>(model.getBlock(model.getLinkArray(model.getBlock(1),"Properties")[0]),"Name")=="Plain_01","add missing suffix");
  auto newMaterial=SpellBook::lookup("Mesh/New Material");check(bool(newMaterial),"find new material action");
 newMaterial->cast(&model,model.getBlock(1));auto freshLinks=model.getLinkArray(model.getBlock(1),"Properties");int count=0;
 for(int id:freshLinks)if(model.inherits(model.getBlock(id),"NiMaterialProperty")){++count;check(model.get<float>(model.getBlock(id),"Alpha")==1,"new material has default opacity");}
 check(count==1 && model.get<QString>(model.getBlock(mat),"Name")=="Plain","new material replaces only this mesh material");
 for(const QString path:{QString("build/qt-check/fixtures/Working.nif"),QString("build/qt-check/fixtures/NotWorking.nif")}) {
  if(!QFile::exists(path))continue;NifModel real;check(real.loadFromFile(path),"load NiMesh face fixture");int tested=0;
  for(int b=0;b<real.getBlockCount();++b)if(real.inherits(real.getBlock(b),"NiMesh")) {
   if(!faces.read(&real,real.getBlock(b)) || faces.triangles.isEmpty())continue;
   NifProxyModel hierarchy;hierarchy.setModel(&real);
   original=bytes(real);real.resetUndoHistory();
   unique->cast(&real,real.getBlock(b));
   auto uniqueProperties=real.getLinkArray(real.getBlock(b),"Properties");int materialId=-1;
   for(int id:uniqueProperties)if(real.inherits(real.getBlock(id),"NiMaterialProperty"))materialId=id;
   check(materialId>=0,"NiMesh unique material is attached in Properties");
   check(real.getChildLinks(b).contains(materialId) && !real.getRootLinks().contains(materialId),"NiMesh material belongs to mesh rather than document root");
   auto meshRow=hierarchy.mapFrom(real.getBlock(b),QModelIndex());bool child=false;
   for(int r=0;r<hierarchy.rowCount(meshRow);++r)if(real.getBlockNumber(hierarchy.mapTo(hierarchy.index(r,0,meshRow)))==materialId)child=true;
   check(child,"NiMesh material appears under selected mesh in live hierarchy");
   real.undo();check(bytes(real)==original,"undo NiMesh unique material restores original document");
   newMaterial->cast(&real,real.getBlock(b));
   materialId=-1;for(int id:real.getLinkArray(real.getBlock(b),"Properties"))if(real.inherits(real.getBlock(id),"NiMaterialProperty"))materialId=id;
   check(materialId>=0 && real.getChildLinks(b).contains(materialId) && !real.getRootLinks().contains(materialId),"NiMesh New Material attaches under selected mesh");
   auto newBytes=bytes(real);NifModel materialReload;QBuffer materialInput(&newBytes);materialInput.open(QIODevice::ReadOnly);
   check(materialReload.load(materialInput) && materialReload.getChildLinks(b).contains(materialId),"NiMesh new material attachment survives save/reload");
   real.undo();check(bytes(real)==original,"undo NiMesh New Material restores exact document");
   check(faces.read(&real,real.getBlock(b)),"reread faces after material undo");
   auto before=faces.triangles;real.resetUndoHistory();original=bytes(real);
   real.beginUndoTransaction("Delete face");check(faces.remove(&real,QSet<int>{0}),"delete NiMesh index triangle");real.endUndoTransaction();
   check(faces.read(&real,real.getBlock(b)) && faces.triangles==before.mid(1),"NiMesh remaining index order");
   auto after=bytes(real);NifModel roundtrip;QBuffer buffer(&after);buffer.open(QIODevice::ReadOnly);check(roundtrip.load(buffer) && faces.read(&roundtrip,roundtrip.getBlock(b)) && faces.triangles==before.mid(1),"NiMesh face deletion save/reload");
   real.undo();auto undone=bytes(real);check(undone==original,"NiMesh undo exact stream bytes");real.redo();check(bytes(real)==after,"NiMesh redo exact stream bytes");   check(faces.read(&real,real.getBlock(b)),"read edited mesh before clearing faces");QSet<int> all;for(int f=0;f<faces.triangles.size();++f)all.insert(f);
   real.beginUndoTransaction("Delete all faces");check(faces.remove(&real,all),"delete all NiMesh faces");real.endUndoTransaction();
   auto empty=bytes(real);NifModel emptyMesh;QBuffer emptyBuffer(&empty);emptyBuffer.open(QIODevice::ReadOnly);
   check(emptyMesh.load(emptyBuffer) && faces.read(&emptyMesh,emptyMesh.getBlock(b)) && faces.triangles.isEmpty(),"empty NiMesh survives save/reload");
   ++tested;break;
  }
  check(tested>0,"tested real NiMesh face deletion");
 }
}

static void editingStorageChecks()
{
 auto exercise=[](NifModel & model,int source){
  auto duplicate=SpellBook::lookup("Block/Duplicate");check(bool(duplicate),"find mesh duplicate action");
  auto mesh=duplicate->cast(&model,model.getBlock(source));int target=model.getBlockNumber(mesh);
  VertexEditData vertices;check(vertices.read(&model,mesh) && vertices.isShared(&model),"duplicate initially references original geometry");
  auto originalPositions=vertices.positions;auto before=bytes(model);model.resetUndoHistory();
  check(makeMeshGeometryUnique(&model,mesh),"detach duplicated mesh geometry");
  mesh=model.getBlock(target);check(vertices.read(&model,mesh) && !vertices.isShared(&model) && vertices.positions==originalPositions,"duplicate has independent unchanged vertices");
  FaceEditData faces;check(faces.read(&model,mesh),"duplicate has independent triangle storage");
  auto independent=bytes(model);model.undo();check(bytes(model)==before,"geometry detach undo restores shared links and exact bytes");model.redo();check(bytes(model)==independent,"geometry detach redo restores independent buffers");
  mesh=model.getBlock(target);vertices.read(&model,mesh);faces.read(&model,mesh);auto oldTriangles=faces.triangles;
  QMap<int,QByteArray> originalStreams;QMap<int,int> steps;
  QMap<QPersistentModelIndex,QVector<NifValue>> originalArrays;
  if(model.inherits(mesh,"NiTriShape")) {
   auto geometry=model.getBlock(model.getLink(mesh,"Data"));
   auto record=[&](QModelIndex array){if(!array.isValid() || !model.rowCount(array))return;QVector<NifValue> values;for(int r=0;r<model.rowCount(array);++r)values<<model.getValue(array.child(r,0));originalArrays[array]=values;};
   for(const char * name:{"Normals","Tangents","Bitangents","Vertex Colors"})record(model.getIndex(geometry,name));
   auto uv=model.getIndex(geometry,"UV Sets");for(int r=0;uv.isValid() && r<model.rowCount(uv);++r)record(uv.child(r,0));
  }
  if(model.inherits(mesh,"NiMesh")) {
   auto refs=model.getIndex(mesh,"Datastreams");
   for(int r=0;r<model.rowCount(refs);++r){auto ref=refs.child(r,0);auto sem=model.getIndex(ref,"Component Semantics");if(model.get<QString>(sem.child(0,0),"Name")=="INDEX")continue;
    int id=model.getLink(ref,"Stream");auto stream=model.getBlock(id);originalStreams[id]=model.get<QByteArray>(model.getIndex(stream,"Data").child(0,0));int stride=0;
    for(auto format:model.getArray<uint>(stream,"Component Formats"))stride+=((format>>16)&15)*((format>>8)&15);steps[id]=stride;
   }
  }
  before=bytes(model);model.resetUndoHistory();QString error;
  check(deleteMeshVertices(&model,mesh,{0},&error),qPrintable(QString("delete duplicate vertex: ")+error));
  check(vertices.read(&model,mesh) && vertices.positions==originalPositions.mid(1),"vertex deletion compacts positions");
  QVector<Triangle> expected;for(auto t:oldTriangles)if(t[0]!=0 && t[1]!=0 && t[2]!=0)expected<<Triangle(t[0]-1,t[1]-1,t[2]-1);
  check(faces.read(&model,mesh) && faces.triangles==expected,"vertex deletion removes connected faces and remaps remaining indices");
  for(auto it=originalStreams.cbegin();it!=originalStreams.cend();++it)check(model.get<QByteArray>(model.getIndex(model.getBlock(it.key()),"Data").child(0,0))==it.value().mid(steps[it.key()]),"vertex deletion preserves remaining stream records byte for byte");
  for(auto it=originalArrays.cbegin();it!=originalArrays.cend();++it){bool preserved=model.rowCount(it.key())==it.value().size()-1;for(int r=0;r<model.rowCount(it.key());++r)preserved=preserved && model.getValue(it.key().child(r,0))==it.value()[r+1];check(preserved,"classic vertex deletion preserves matching attributes");}
  VertexEditData sourceVertices;check(sourceVertices.read(&model,model.getBlock(source)) && sourceVertices.positions==originalPositions,"deleting duplicate vertices leaves original mesh unchanged");
  auto deleted=bytes(model);NifModel loaded;QBuffer input(&deleted);input.open(QIODevice::ReadOnly);
  check(loaded.load(input) && vertices.read(&loaded,loaded.getBlock(target)) && vertices.positions==originalPositions.mid(1),"deleted vertices survive save/reload");
  model.undo();check(bytes(model)==before,"vertex deletion undo restores exact document");model.redo();check(bytes(model)==deleted,"vertex deletion redo restores exact document");
  mesh=model.getBlock(target);vertices.read(&model,mesh);QSet<int> all;for(int i=0;i<vertices.positions.size();++i)all.insert(i);
  check(deleteMeshVertices(&model,mesh,all,&error),qPrintable(QString("delete all vertices: ")+error));
  check(vertices.read(&model,mesh) && vertices.positions.isEmpty() && faces.read(&model,mesh) && faces.triangles.isEmpty(),"deleting all vertices leaves an empty mesh");
 };
 NifModel classic;VertexEditorTest::fixture(classic);auto attributes=classic.getBlock(2);
 classic.set<bool>(attributes,"Has Normals",true);classic.updateArray(attributes,"Normals");classic.setArray<Vector3>(attributes,"Normals",QVector<Vector3>{Vector3(1,0,0),Vector3(0,1,0),Vector3(0,0,1),Vector3(-1,0,0)});
 classic.set<bool>(attributes,"Has Vertex Colors",true);classic.updateArray(attributes,"Vertex Colors");classic.setArray<Color4>(attributes,"Vertex Colors",QVector<Color4>{Color4(1,0,0,1),Color4(0,1,0,1),Color4(0,0,1,1),Color4(1,1,0,1)});
 classic.set<quint16>(attributes,"Num UV Sets",1);classic.set<quint16>(attributes,"Vector Flags",1);classic.updateArray(attributes,"UV Sets");auto uv=classic.getIndex(attributes,"UV Sets");
 if(uv.isValid() && classic.rowCount(uv)) {classic.updateArray(uv.child(0,0));classic.setArray<Vector2>(uv.child(0,0),QVector<Vector2>{Vector2(0,0),Vector2(1,0),Vector2(0,1),Vector2(1,1)});}
 exercise(classic,1);
 for(const QString path:{QString("build/qt-check/fixtures/Working.nif"),QString("build/qt-check/fixtures/NotWorking.nif")}) {
  if(!QFile::exists(path))continue;NifModel model;check(model.loadFromFile(path),"load NiMesh duplicate editing fixture");
  int target=-1;for(int b=0;b<model.getBlockCount();++b)if(model.inherits(model.getBlock(b),"NiMesh")){VertexEditData data;FaceEditData faces;if(data.read(&model,model.getBlock(b)) && !data.positions.isEmpty() && faces.read(&model,model.getBlock(b))){target=b;break;}}
  check(target>=0,"find editable NiMesh for duplicate deletion");exercise(model,target);
 }
}
