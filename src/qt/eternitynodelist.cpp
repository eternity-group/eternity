#include "eternitynodelist.h"
#include "ui_eternitynodelist.h"

#include "activeeternitynode.h"
#include "clientmodel.h"
#include "init.h"
#include "guiutil.h"
#include "eternitynode-sync.h"
#include "eternitynodeconfig.h"
#include "eternitynodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QTimer>
#include <QMessageBox>

EternitynodeList::EternitynodeList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::EternitynodeList),
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

    ui->tableWidgetMyEternitynodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyEternitynodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyEternitynodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyEternitynodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyEternitynodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyEternitynodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetEternitynodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetEternitynodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetEternitynodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetEternitynodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetEternitynodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMyEternitynodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyEternitynodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

EternitynodeList::~EternitynodeList()
{
    delete ui;
}

void EternitynodeList::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when eternitynode count changes
        connect(clientModel, SIGNAL(strEternitynodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void EternitynodeList::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void EternitynodeList::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMyEternitynodes->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void EternitynodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
        if(mne.getAlias() == strAlias) {
            std::string strError;
            CEternitynodeBroadcast mnb;

            bool fSuccess = CEternitynodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if(fSuccess) {
                strStatusHtml += "<br>Successfully started eternitynode.";
                mnodeman.UpdateEternitynodeList(mnb);
                mnb.Relay();
                mnodeman.NotifyEternitynodeUpdates();
            } else {
                strStatusHtml += "<br>Failed to start eternitynode.<br>Error: " + strError;
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

void EternitynodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
        std::string strError;
        CEternitynodeBroadcast mnb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && mnodeman.Has(txin)) continue;

        bool fSuccess = CEternitynodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if(fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateEternitynodeList(mnb);
            mnb.Relay();
            mnodeman.NotifyEternitynodeUpdates();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d eternitynodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void EternitynodeList::updateMyEternitynodeInfo(QString strAlias, QString strAddr, eternitynode_info_t& infoMn)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMyEternitynodes->rowCount(); i++) {
        if(ui->tableWidgetMyEternitynodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyEternitynodes->rowCount();
        ui->tableWidgetMyEternitynodes->insertRow(nNewRow);
    }

    QTableWidgetItem *aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(infoMn.fInfoValid ? QString::fromStdString(infoMn.addr.ToString()) : strAddr);
    QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(infoMn.fInfoValid ? infoMn.nProtocolVersion : -1));
    QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(infoMn.fInfoValid ? CEternitynode::StateToString(infoMn.nActiveState) : "MISSING"));
    QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(infoMn.fInfoValid ? (infoMn.nTimeLastPing - infoMn.sigTime) : 0)));
    QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",
                                                                                                   infoMn.fInfoValid ? infoMn.nTimeLastPing + QDateTime::currentDateTime().offsetFromUtc() : 0)));
    QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(infoMn.fInfoValid ? CBitcoinAddress(infoMn.pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyEternitynodes->setItem(nNewRow, 6, pubkeyItem);
}

void EternitynodeList::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mymnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my eternitynode list only once in MY_ETERNITYNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_ETERNITYNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetEternitynodes->setSortingEnabled(false);
    BOOST_FOREACH(CEternitynodeConfig::CEternitynodeEntry mne, eternitynodeConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), nOutputIndex);

        eternitynode_info_t infoMn = mnodeman.GetEternitynodeInfo(txin);

        updateMyEternitynodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), infoMn);
    }
    ui->tableWidgetEternitynodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void EternitynodeList::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in ETERNITYNODELIST_UPDATE_SECONDS seconds
    // or ETERNITYNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + ETERNITYNODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + ETERNITYNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetEternitynodes->setSortingEnabled(false);
    ui->tableWidgetEternitynodes->clearContents();
    ui->tableWidgetEternitynodes->setRowCount(0);
    std::vector<CEternitynode> vEternitynodes = mnodeman.GetFullEternitynodeVector();

    BOOST_FOREACH(CEternitynode& mn, vEternitynodes)
    {
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(mn.nProtocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(mn.GetStatus()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + QDateTime::currentDateTime().offsetFromUtc())));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetEternitynodes->insertRow(0);
        ui->tableWidgetEternitynodes->setItem(0, 0, addressItem);
        ui->tableWidgetEternitynodes->setItem(0, 1, protocolItem);
        ui->tableWidgetEternitynodes->setItem(0, 2, statusItem);
        ui->tableWidgetEternitynodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetEternitynodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetEternitynodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetEternitynodes->rowCount()));
    ui->tableWidgetEternitynodes->setSortingEnabled(true);
}

void EternitynodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", ETERNITYNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void EternitynodeList::on_startButton_clicked()
{
    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyEternitynodes->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyEternitynodes->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm eternitynode start"),
        tr("Are you sure you want to start eternitynode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void EternitynodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all eternitynodes start"),
        tr("Are you sure you want to start ALL eternitynodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void EternitynodeList::on_startMissingButton_clicked()
{

    if(!eternitynodeSync.IsEternitynodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until eternitynode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing eternitynodes start"),
        tr("Are you sure you want to start MISSING eternitynodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void EternitynodeList::on_tableWidgetMyEternitynodes_itemSelectionChanged()
{
    if(ui->tableWidgetMyEternitynodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void EternitynodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
