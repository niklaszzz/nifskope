#ifndef ROOMEDIT_H
#define ROOMEDIT_H
#include "data/niftypes.h"
#include <QMap>
#include <QSet>
#include <QStringList>
class NifModel;
struct RoomRecord
{
    int id = -1;
    QVector<int> groups;
    QVector<qint32> fixtures, incoming, outgoing;
};
class RoomScene
{
  public:
    explicit RoomScene(NifModel *model);
    QVector<RoomRecord> rooms;
    QMap<int, QVector<int>> parents;
    bool world(int block, Transform &result, QString *reason = nullptr) const;
    QVector<int> memberships(int block) const;
    QVector<int> containingRooms(int mesh, QString *reason = nullptr) const;
    QVector<QPair<Vector3, Vector3>> cage(int room) const;
    QStringList validate() const;
    bool assign(int mesh, int destination, bool apply, QString *reason = nullptr);

  private:
    NifModel *model;
    const RoomRecord *room(int id) const;
    QVector<int> ancestry(int block) const;
};
#endif
