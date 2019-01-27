// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposalfilterproxy.h"
#include "proposaltablemodel.h"

#include <cstdlib>

#include <QDateTime>

ProposalFilterProxy::ProposalFilterProxy(QObject *parent) :
    QSortFilterProxyModel(parent),
    startDate(0),
    endDate(0),
    proposalName(),
    minAmount(0),
    votesNeeded(0),
    minYesVotes(0),
    minNoVotes(0),
    minAbstainVotes(0)
{
}

bool ProposalFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    int proposalStartDate = index.data(ProposalTableModel::StartDateRole).toInt();
    int proposalEndDate = index.data(ProposalTableModel::EndDateRole).toInt();
    QString propName = index.data(ProposalTableModel::ProposalRole).toString();
    qint64 amount = llabs(index.data(ProposalTableModel::AmountRole).toLongLong());
    int yesVotes = index.data(ProposalTableModel::YesVotesRole).toInt();
    int noVotes = index.data(ProposalTableModel::NoVotesRole).toInt();
    int abstainVotes = index.data(ProposalTableModel::AbstainVotesRole).toInt();
    int numVotesNeeded = index.data(ProposalTableModel::VotesNeededRole).toInt();

    if(proposalStartDate < startDate)
       return false;
    if(proposalEndDate < endDate)
       return false;
    if(!propName.contains(proposalName, Qt::CaseInsensitive))
        return false;
    if(amount < minAmount)
        return false;
    if(yesVotes < minYesVotes)
        return false;
    if(noVotes < minNoVotes)
        return false;
    if(abstainVotes < minAbstainVotes)
        return false;
    if(votesNeeded && !numVotesNeeded)
        return false;

    return true;
}

void ProposalFilterProxy::setProposalStart(int minimum)
{
    this->startDate = minimum;
    invalidateFilter();
}

void ProposalFilterProxy::setProposalEnd(int minimum)
{
    this->endDate = minimum;
    invalidateFilter();
}

void ProposalFilterProxy::setProposal(const QString &proposal)
{
    this->proposalName = proposal;
    invalidateFilter();
}

void ProposalFilterProxy::setMinAmount(const CAmount& minimum)
{
    this->minAmount = minimum * BitcoinUnits::factor(BitcoinUnits::SWIFT);
    invalidateFilter();
}

void ProposalFilterProxy::setVotesNeeded(int needed)
{
    this->votesNeeded = (needed > 0);
    invalidateFilter();
}

void ProposalFilterProxy::setMinYesVotes(int minimum)
{
    this->minYesVotes = minimum;
    invalidateFilter();
}

void ProposalFilterProxy::setMinNoVotes(int minimum)
{
    this->minNoVotes = minimum;
    invalidateFilter();
}

void ProposalFilterProxy::setMinAbstainVotes(int minimum)
{
    this->minAbstainVotes = minimum;
    invalidateFilter();
}

int ProposalFilterProxy::rowCount(const QModelIndex &parent) const
{
    return QSortFilterProxyModel::rowCount(parent);
}
