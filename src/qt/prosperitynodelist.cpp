#include "prosperitynodelist.h"
#include "ui_prosperitynodelist.h"

#include "activeprosperitynode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "prosperitynode-sync.h"
#include "prosperitynodeconfig.h"
#include "prosperitynodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"
#include "askpassphrasedialog.h"

#include <QMessageBox>
#include <QTimer>

CCriticalSection cs_prosperitynodes;

ProsperitynodeList::ProsperitynodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::ProsperitynodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyProsperitynodes->setAlternatingRowColors(true);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyProsperitynodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMyProsperitynodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyProsperitynodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

ProsperitynodeList::~ProsperitynodeList()
{
    delete ui;
}

void ProsperitynodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void ProsperitynodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void ProsperitynodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyProsperitynodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void ProsperitynodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CProsperitynodeConfig::CProsperitynodeEntry mne, prosperitynodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CProsperitynodeBroadcast mnb;

            bool fSuccess = CProsperitynodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started prosperitynode.";
                mnodeman.UpdateProsperitynodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start prosperitynode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void ProsperitynodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CProsperitynodeConfig::CProsperitynodeEntry mne, prosperitynodeConfig.getEntries()) {
        std::string strError;
        CProsperitynodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CProsperitynode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CProsperitynodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateProsperitynodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d prosperitynodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void ProsperitynodeList::updateMyProsperitynodeInfo(QString strAlias, QString strAddr, CProsperitynode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyProsperitynodes->rowCount(); i++) {
        if (ui->tableWidgetMyProsperitynodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyProsperitynodes->rowCount();
        ui->tableWidgetMyProsperitynodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyProsperitynodes->setItem(nNewRow, 6, pubkeyItem);
}

void ProsperitynodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my prosperitynode list only once in MY_PROSPERITYNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_PROSPERITYNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyProsperitynodes->setSortingEnabled(false);
    BOOST_FOREACH (CProsperitynodeConfig::CProsperitynodeEntry mne, prosperitynodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CProsperitynode* pmn = mnodeman.Find(txin);
        updateMyProsperitynodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyProsperitynodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void ProsperitynodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyProsperitynodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyProsperitynodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm prosperitynode start"),
        tr("Are you sure you want to start prosperitynode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void ProsperitynodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all prosperitynodes start"),
        tr("Are you sure you want to start ALL prosperitynodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void ProsperitynodeList::on_startMissingButton_clicked()
{
    if (!prosperitynodeSync.IsProsperitynodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until prosperitynode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing prosperitynodes start"),
        tr("Are you sure you want to start MISSING prosperitynodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void ProsperitynodeList::on_tableWidgetMyProsperitynodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyProsperitynodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void ProsperitynodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
