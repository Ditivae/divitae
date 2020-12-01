#ifndef PROSPERITYNODELIST_H
#define PROSPERITYNODELIST_H

#include "prosperitynode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_PROSPERITYNODELIST_UPDATE_SECONDS 60
#define PROSPERITYNODELIST_UPDATE_SECONDS 15
#define PROSPERITYNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class ProsperitynodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** prosperitynode Manager page widget */
class ProsperitynodeList : public QWidget
{
    Q_OBJECT

public:
    explicit ProsperitynodeList(QWidget* parent = 0);
    ~ProsperitynodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyProsperitynodeInfo(QString strAlias, QString strAddr, CProsperitynode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::ProsperitynodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyProsperitynodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // PROSPERITYNODELIST_H
