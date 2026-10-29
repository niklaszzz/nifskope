#ifndef ROOMPANEL_H
#define ROOMPANEL_H
#include "model/portaledit.h"
#include <QPersistentModelIndex>
#include <QPointer>
#include <QWidget>
class NifModel;
class QLabel;
class QComboBox;
class QPushButton;
class QListWidget;
class QTimer;
class QDoubleSpinBox;
class QCheckBox;
class RoomPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit RoomPanel(QWidget *parent = nullptr);
    void setTarget(NifModel *model, const QModelIndex &selected);
    void setMeshSelection(NifModel *model, const QModelIndex &mesh, const QSet<int> &vertices);
  signals:
    void overlayChanged(bool enabled);
    void portalPreviewChanged(const QVector<QPair<Vector3, Vector3>> &edges);
    void portalHighlightChanged(int portal);

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void refresh();
    void clearPreview();
    void refreshPortals();
    QPointer<NifModel> model;
    QPersistentModelIndex selected;
    int restoreSelected = -1;
    bool applying = false;
    QVector<QMetaObject::Connection> connections;
    QLabel *objectLabel, *membershipLabel, *suggestionLabel, *details, *status;
    QComboBox *destination;
    QPushButton *useSuggested, *assign;
    QListWidget *issues;
    QTimer *timer;
    QVector<int> suggestions;
    QComboBox *portals;
    QLabel *portalStatus, *portalSelection;
    QPushButton *fitPortal, *applyPortal;
    QCheckBox *pairedPortals;
    QDoubleSpinBox *portalValues[4];
    QPersistentModelIndex selectionMesh;
    QSet<int> selectionVertices;
    PortalEditPlan portalPlan;
};
#endif
