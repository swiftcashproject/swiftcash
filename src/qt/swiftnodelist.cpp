// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2018 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "swiftnodelist.h"
#include "ui_swiftnodelist.h"

#include "activeswiftnode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "swiftnode-sync.h"
#include "swiftnodeconfig.h"
#include "swiftnodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"
#include "askpassphrasedialog.h"

#include <QMessageBox>
#include <QTimer>

CCriticalSection cs_swiftnodes;

SwiftnodeList::SwiftnodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::SwiftnodeList),
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

    ui->tableWidgetMySwiftnodes->setAlternatingRowColors(true);
    ui->tableWidgetMySwiftnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMySwiftnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMySwiftnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMySwiftnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMySwiftnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMySwiftnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMySwiftnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMySwiftnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

SwiftnodeList::~SwiftnodeList()
{
    delete ui;
}

void SwiftnodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void SwiftnodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void SwiftnodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMySwiftnodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void SwiftnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CSwiftnodeConfig::CSwiftnodeEntry mne, swiftnodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CSwiftnodeBroadcast mnb;

            bool fSuccess = CSwiftnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started swiftnode.";
                mnodeman.UpdateSwiftnodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start swiftnode.<br>Error: " + strError;
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

void SwiftnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CSwiftnodeConfig::CSwiftnodeEntry mne, swiftnodeConfig.getEntries()) {
        std::string strError;
        CSwiftnodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CSwiftnode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CSwiftnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateSwiftnodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d swiftnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void SwiftnodeList::updateMySwiftnodeInfo(QString strAlias, QString strAddr, CSwiftnode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMySwiftnodes->rowCount(); i++) {
        if (ui->tableWidgetMySwiftnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMySwiftnodes->rowCount();
        ui->tableWidgetMySwiftnodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMySwiftnodes->setItem(nNewRow, 6, pubkeyItem);
}

void SwiftnodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my swiftnode list only once in MY_SWIFTNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_SWIFTNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMySwiftnodes->setSortingEnabled(false);
    BOOST_FOREACH (CSwiftnodeConfig::CSwiftnodeEntry mne, swiftnodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CSwiftnode* pmn = mnodeman.Find(txin);
        updateMySwiftnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMySwiftnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void SwiftnodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMySwiftnodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMySwiftnodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm swiftnode start"),
        tr("Are you sure you want to start swiftnode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void SwiftnodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all swiftnodes start"),
        tr("Are you sure you want to start ALL swiftnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void SwiftnodeList::on_startMissingButton_clicked()
{
    if (!swiftnodeSync.IsSwiftnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until swiftnode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing swiftnodes start"),
        tr("Are you sure you want to start MISSING swiftnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void SwiftnodeList::on_tableWidgetMySwiftnodes_itemSelectionChanged()
{
    if (ui->tableWidgetMySwiftnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void SwiftnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
