// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2017 PIVX developers
// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SWIFTNODELIST_H
#define SWIFTNODELIST_H

#include "swiftnode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_SWIFTNODELIST_UPDATE_SECONDS 60
#define SWIFTNODELIST_UPDATE_SECONDS 15
#define SWIFTNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class SwiftnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Swiftnode Manager page widget */
class SwiftnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit SwiftnodeList(QWidget* parent = 0);
    ~SwiftnodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMySwiftnodeInfo(QString strAlias, QString strAddr, CSwiftnode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::SwiftnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMySwiftnodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // SWIFTNODELIST_H
