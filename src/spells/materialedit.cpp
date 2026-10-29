#include "spellbook.h"

#include "ui/widgets/nifeditors.h"


// Brief description is deliberately not autolinked to class Spell
/*! \file materialedit.cpp
 * \brief Material editing spells (spMaterialEdit)
 *
 * All classes here inherit from the Spell class.
 */

/* XPM */
static char const * mat42_xpm[] = {
	"64 64 43 1",
	"   c None",
	".	c #866A36", "+	c #856E33", "@	c #817333", "#	c #836F45", "$	c #8B6D3E",
	"%	c #8C6F34", "&	c #917231", "*	c #77812C", "=	c #7D765D", "-	c #987147",
	";	c #6C8D29", ">	c #93792F", ",	c #7A7B71", "'	c #A0734E", ")	c #71828B",
	"!	c #5DA11E", "~	c #9B822B", "{	c #56AD16", "]	c #6E889D", "^	c #AD795F",
	"/	c #698DB0", "(	c #48BE13", "_	c #6691BD", ":	c #43C311", "<	c #A58D28",
	"[	c #6396CE", "}	c #1BE30C", "|	c #38D700", "1	c #BD7F71", "2	c #11F003",
	"3	c #C78581", "4	c #0CFF00", "5	c #B7A51C", "6	c #DA8C95", "7	c #C3B618",
	"8	c #ED93AA", "9	c #F898B9", "0	c #D6CD0A", "a	c #FF9BC4", "b	c #E6DF00",
	"c	c #F6F500", "d	c #FFFF00",
	"                                                                ",
	"                                                                ",
	"                                  .......                       ",
	"                               ................                 ",
	"                             .......................            ",
	"                           ...........................          ",
	"                          ...............................       ",
	"                        .................................       ",
	"                       ...................................      ",
	"                     ......................................     ",
	"                    ........................................    ",
	"                   ...........................@***+.........    ",
	"                  ................%~5<%......;2444}:*........   ",
	"                 ................&0ddd0&....@}4444442;........  ",
	"                ................%7ddddd7....;44444444|+.......  ",
	"               .................5ddddddb>...{444444442@.......  ",
	"              .................%0ddddddc<..+|444444442@.......  ",
	"             ..................%bddddddc<..@|444444442@.......  ",
	"            ...................%bddddddc<..@|44444444|........  ",
	"           ............'^1^-...%bddddddc>..@|44444444!........  ",
	"          ............39aaa8'..%bddddddb%..+:4444442!.........  ",
	"         ............^9aaaaa6$.%7dddddd0....*}4444};..........  ",
	"        .............6aaaaaa9^..<cddddd5.....*(||{@...........  ",
	"        ............$8aaaaaaa1...7ddddb>.....................   ",
	"       .............-9aaaaaaa1...%7cdb~......................   ",
	"      ..............-9aaaaaaa1.....>~&......................    ",
	"      ..............-8aaaaaaa1..............................    ",
	"     ........#)]],..$8aaaaaa9^..............................    ",
	"     .......=_[[[[,..^9aaaaa6..............................     ",
	"    .......$/[[[[[]$..^9aaa8'.............................      ",
	"    .......)[[[[[[_#...'363'..............................      ",
	"    ......#_[[[[[[_#....................................        ",
	"    ......=[[[[[[[/#..................................          ",
	"    ......,[[[[[[[_#.................................           ",
	"    ......,[[[[[[[_#...............................             ",
	"    ......,[[[[[[[]$.............................               ",
	"    ......=_[[[[[_#............................                 ",
	"    .......][[[[_,............................                  ",
	"    .......#][[_,...........................                    ",
	"    .........==#..........................                      ",
	"    .....................................                       ",
	"    ....................................                        ",
	"     ...................................                        ",
	"     ..................................                         ",
	"     .................................                          ",
	"      ................................                          ",
	"      ...............................                           ",
	"      ..................  ...........                           ",
	"      ................      .........                           ",
	"       ..............        ........                           ",
	"        .............        ........                           ",
	"        ............         .......                            ",
	"         ...........         .......                            ",
	"         ............        .......                            ",
	"          ...........       ........                            ",
	"           ...........     .........                            ",
	"           ........................                             ",
	"            .......................                             ",
	"             .....................                              ",
	"               .................                                ",
	"                .............                                   ",
	"                  ........                                      ",
	"                                                                ",
	"                                                                "
};

static QIconPtr mat42_xpm_icon = nullptr;

//! Edit a material
class spMaterialEdit final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Material" ); }
	QString page() const override final { return Spell::tr( "" ); }
	bool instant() const override final { return true; }
	QIcon icon() const override final
	{
		if ( !mat42_xpm_icon )
			mat42_xpm_icon = QIconPtr( new QIcon(QPixmap( mat42_xpm )) );

		return *mat42_xpm_icon;
	}

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iBlock  = nif->getBlock( index, "NiMaterialProperty" );
		QModelIndex sibling = index.sibling( index.row(), 0 );
		return index.isValid() && ( iBlock == sibling || nif->getIndex( iBlock, "Name" ) == sibling );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iMaterial = nif->getBlock( index );
		NifBlockEditor * me = new NifBlockEditor( nif, iMaterial );

		me->pushLayout( new QHBoxLayout );
		me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Ambient Color" ) ) );
		me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Diffuse Color" ) ) );
		me->popLayout();
		me->pushLayout( new QHBoxLayout );
		me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Specular Color" ) ) );
		me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Emissive Color" ) ) );
		me->popLayout();
		me->add( new NifFloatSlider( nif, nif->getIndex( iMaterial, "Alpha" ), 0.0, 1.0 ) );
		me->add( new NifFloatSlider( nif, nif->getIndex( iMaterial, "Glossiness" ), 0.0, 100.0 ) );
		me->setWindowModality( Qt::ApplicationModal );
		me->show();

		return index;
	}
};

REGISTER_SPELL( spMaterialEdit )


#include <QBuffer>
#include <QRegularExpression>
#include <functional>

// Copy the mesh's property graph without touching geometry or other meshes.
class spMakeMaterialUnique : public Spell
{
public:
 virtual bool newMaterial() const {return false;}
 QString name() const override {return tr("Make Material Unique");}
 QString page() const override {return tr("Mesh");}
 bool isApplicable(const NifModel * nif,const QModelIndex & index) override {
  return (nif->inherits(index,"NiMesh") || nif->inherits(index,"NiTriBasedGeom")) && nif->getIndex(index,"Properties").isValid();
 }
 QModelIndex cast(NifModel * nif,const QModelIndex & index) override {
  if(!isApplicable(nif,index))return index;
  QPersistentModelIndex mesh=index;
  auto properties=nif->getIndex(mesh,"Properties");
  QVector<QPersistentModelIndex> fields;
  for(int i=0;i<nif->rowCount(properties);++i)fields<<properties.child(i,0);
  QList<int> sources;
  std::function<void(int)> collect=[&](int id){
   if(id<0 || sources.contains(id))return;
   auto block=nif->getBlock(id);
   // Follow owned property dependencies, including controllers and texture data.
   // Scene objects referenced by a controller remain outside the material copy.
   if(!block.isValid() || nif->inherits(block,"NiAVObject"))return;
   sources<<id;for(int child:nif->getChildLinks(id))collect(child);
  };
  for(auto field:fields)if(!newMaterial() || !nif->inherits(nif->getBlock(nif->getLink(field)),"NiMaterialProperty"))collect(nif->getLink(field));
  QMap<int,QByteArray> data;
  for(int id:sources){QBuffer buffer(&data[id]);buffer.open(QIODevice::WriteOnly);if(!nif->saveIndex(buffer,nif->getBlock(id)))return index;}
  auto uniqueName=[&](QString original){
   if(original.isEmpty())original="Material";
   QRegularExpression re("^(.*?)([0-9]+)$");auto match=re.match(original);
   QString prefix=match.hasMatch()?match.captured(1):original+"_";
   int digits=match.hasMatch()?match.captured(2).size():2;
   qulonglong number=match.hasMatch()?match.captured(2).toULongLong():0;
   QSet<QString> used;
   for(int b=0;b<nif->getBlockCount();++b){auto block=nif->getBlock(b);used.insert(nif->get<QString>(block,"Name"));auto materialData=nif->getIndex(block,"Material Data");auto names=materialData.isValid()?nif->getIndex(materialData,"Material Name"):QModelIndex();for(int r=0;names.isValid() && r<nif->rowCount(names);++r)used.insert(nif->get<QString>(names.child(r,0)));}
   QString candidate;do{candidate=prefix+QString::number(++number).rightJustified(digits,'0');}while(used.contains(candidate));return candidate;
  };
  QMap<QString,QString> renamed;
  int initialBlocks=nif->getBlockCount();
  nif->beginUndoTransaction(name());
  bool held=nif->holdUpdates(true);
  QMap<qint32,qint32> map;
  for(int id:sources)map[id]=nif->getBlockNumber(nif->insertNiBlock(nif->getBlockName(nif->getBlock(id))));
  for(int id:sources){auto block=nif->getBlock(map[id]);QBuffer buffer(&data[id]);buffer.open(QIODevice::ReadOnly);
   if(!nif->loadIndex(buffer,block)){
    while(nif->getBlockCount()>initialBlocks)nif->removeNiBlock(nif->getBlockCount()-1);
    nif->holdUpdates(held);nif->endUndoTransaction();return mesh;
   }
   std::function<void(QModelIndex)> relink=[&](QModelIndex parent){for(int r=0;r<nif->rowCount(parent);++r){auto field=parent.child(r,0);if(nif->isLink(field)){int link=nif->getLink(field);if(map.contains(link))nif->setLink(field,map[link]);}else relink(field);}};
   relink(block);
   if(nif->inherits(block,"NiMaterialProperty")){auto old=nif->get<QString>(block,"Name");auto fresh=uniqueName(old);renamed[old]=fresh;nif->set<QString>(block,"Name",fresh);}
  }
  bool material=false;
  for(auto field:fields){int id=nif->getLink(field);if(map.contains(id)){nif->setLink(field,map[id]);if(nif->inherits(nif->getBlock(map[id]),"NiMaterialProperty"))material=true;}}
  if(!material){auto block=nif->insertNiBlock("NiMaterialProperty");nif->set<QString>(block,"Name",uniqueName("Material"));
   nif->set<Color3>(block,"Ambient Color",Color3(1,1,1));nif->set<Color3>(block,"Diffuse Color",Color3(1,1,1));nif->set<float>(block,"Alpha",1);
   auto links=nif->getLinkArray(mesh,"Properties");for(int i=links.size()-1;i>=0;--i)if(nif->inherits(nif->getBlock(links[i]),"NiMaterialProperty"))links.remove(i);links<<nif->getBlockNumber(block);nif->set<uint>(mesh,"Num Properties",links.size());nif->updateArray(mesh,"Properties");nif->setLinkArray(mesh,"Properties",links);
  }
  auto materialData=nif->getIndex(mesh,"Material Data");auto names=materialData.isValid()?nif->getIndex(materialData,"Material Name"):QModelIndex();for(int r=0;names.isValid() && r<nif->rowCount(names);++r){auto field=names.child(r,0);auto old=nif->get<QString>(field);nif->set<QString>(field,renamed.contains(old)?renamed[old]:uniqueName(old));}
  nif->updateHeader();nif->holdUpdates(held);nif->endUndoTransaction();return mesh;
 }
};
REGISTER_SPELL(spMakeMaterialUnique)

class spNewMaterial final : public spMakeMaterialUnique
{
public:
 bool newMaterial() const override {return true;}
 QString name() const override {return tr("New Material");}
};
REGISTER_SPELL(spNewMaterial)
