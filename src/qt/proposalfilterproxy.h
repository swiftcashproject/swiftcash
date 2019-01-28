// Copyright (c) 2018 SwiftCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PROPOSALFILTERPROXY_H
#define BITCOIN_QT_PROPOSALFILTERPROXY_H

#include "amount.h"

#include <QDateTime>
#include <QSortFilterProxyModel>

class ProposalFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ProposalFilterProxy(QObject *parent = 0);

    void setProposalStart(int minimum);
    void setProposalEnd(int minimum);
    void setProposal(const QString &proposal);

    void setMinAmount(const CAmount& minimum);
    void setVotesNeeded(int needed);
    void setMinYesVotes(int minimum);
    void setMinNoVotes(int minimum);
    void setMinAbstainVotes(int minimum);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex & source_parent) const;

private:
    int startDate;
    int endDate;
    QString proposalName;
    CAmount minAmount;
    bool votesNeeded;
    int minYesVotes;
    int minNoVotes;
    int minAbstainVotes;
};

#endif // BITCOIN_QT_PROPOSALFILTERPROXY_H
