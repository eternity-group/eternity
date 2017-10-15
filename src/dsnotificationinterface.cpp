// Copyright (c) 2016-2017 The Eternity group Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dsnotificationinterface.h"
#include "spysend.h"
#include "instantx.h"
#include "governance.h"
#include "eternitynodeman.h"
#include "eternitynode-payments.h"
#include "eternitynode-sync.h"

CDSNotificationInterface::CDSNotificationInterface()
{
}

CDSNotificationInterface::~CDSNotificationInterface()
{
}

void CDSNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindex)
{
    mnodeman.UpdatedBlockTip(pindex);
    spySendPool.UpdatedBlockTip(pindex);
    instantsend.UpdatedBlockTip(pindex);
    enpayments.UpdatedBlockTip(pindex);
    governance.UpdatedBlockTip(pindex);
    eternitynodeSync.UpdatedBlockTip(pindex);
}

void CDSNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlock *pblock)
{
    instantsend.SyncTransaction(tx, pblock);
}