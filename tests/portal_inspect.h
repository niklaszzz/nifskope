#ifndef PORTAL_INSPECT_H
#define PORTAL_INSPECT_H
// Read-only inspection for room/portal editor research.
static int inspectPortals(const QString & path)
{
 NifModel model;if(!model.loadFromFile(path))return 1;
 QVector<int> groups,rooms,portals;QMap<int,int> flagCounts;
 for(int b=0;b<model.getBlockCount();++b){auto block=model.getBlock(b);auto type=model.getBlockName(block);if(type=="NiRoomGroup")groups<<b;else if(type=="NiRoom")rooms<<b;else if(type=="NiPortal"){portals<<b;flagCounts[model.get<quint16>(block,"Portal Flags")]++;}}
 std::printf("File: %s\nVersion: %s\nGroups: %d Rooms: %d Portals: %d\n",qPrintable(path),qPrintable(model.getVersion()),groups.size(),rooms.size(),portals.size());
 int badTargets=0,missingIncoming=0,missingChildren=0;
 for(int id:groups){auto group=model.getBlock(id);std::printf("Group #%d %s: shell=%d rooms=%d children=%d\n",id,qPrintable(model.get<QString>(group,"Name")),model.getLink(group,"Shell"),model.getLinkArray(group,"Rooms").size(),model.getLinkArray(group,"Children").size());}
 for(int i=0;i<rooms.size();++i){auto room=model.getBlock(rooms[i]);auto children=model.getLinkArray(room,"Children"),out=model.getLinkArray(room,"Out Portals"),in=model.getLinkArray(room,"In Portals"),fixtures=model.getLinkArray(room,"Fixtures");if(i<4){std::printf("Room #%d %s: walls=%d in=%d out=%d fixtures=%d children=%d\n",rooms[i],qPrintable(model.get<QString>(room,"Name")),model.rowCount(model.getIndex(room,"Wall Planes")),in.size(),out.size(),fixtures.size(),children.size());auto walls=model.getIndex(room,"Wall Planes");for(int p=0;p<qMin(2,model.rowCount(walls));++p){auto plane=walls.child(p,0);auto n=model.get<Vector3>(plane,"Normal");std::printf("  plane n=(%.4f %.4f %.4f) C=%.4f\n",n[0],n[1],n[2],model.get<float>(plane,"Constant"));}}
  for(int portal:out){if(!children.contains(portal))++missingChildren;auto target=model.getBlock(model.getLink(model.getBlock(portal),"Adjoiner"));if(!model.inherits(target,"NiRoom"))++badTargets;else if(!model.getLinkArray(target,"In Portals").contains(portal))++missingIncoming;}}
 for(int i=0;i<qMin(5,portals.size());++i){auto portal=model.getBlock(portals[i]);std::printf("Portal #%d %s: flags=%u planeCount=%u vertices=%d adjoiner=%d parent=%d\n",portals[i],qPrintable(model.get<QString>(portal,"Name")),model.get<quint16>(portal,"Portal Flags"),model.get<quint16>(portal,"Plane Count"),model.rowCount(model.getIndex(portal,"Vertices")),model.getLink(portal,"Adjoiner"),model.getParent(portal));}
 for(auto i=flagCounts.cbegin();i!=flagCounts.cend();++i)std::printf("Portal flags %d: %d objects\n",i.key(),i.value());
 std::printf("Outgoing portals missing from Children: %d\nOutgoing targets not NiRoom: %d\nMissing optional incoming references: %d\n",missingChildren,badTargets,missingIncoming);return 0;
}
#endif