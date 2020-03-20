// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "utility/logger.h"
#include "wallet/laser/mediator.h"
#include "node/node.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"
#include "laser_test_utils.h"
#include "test_helpers.h"

WALLET_TEST_INIT
#include "wallet_test_environment.cpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace {
const Amount kTransferFirst = 10000;
}  // namespace

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    const auto path = boost::filesystem::system_complete("logs");
    auto logger = Logger::create(logLevel, logLevel, logLevel, "laser_test", path.string());

    Rules::get().pForks[1].m_Height = 1;
	Rules::get().FakePoW = true;
    Rules::get().MaxRollback = 5;
	Rules::get().UpdateChecksum();

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto wdbFirst = createSqliteWalletDB("laser_test_send_first.db", false, false);
    auto wdbSecond = createSqliteWalletDB("laser_test_send_second.db", false, false);

    const AmountList amounts = {100000000, 100000000, 100000000, 100000000};
    for (auto amount : amounts)
    {
        Coin coinFirst = CreateAvailCoin(amount, 1);
        wdbFirst->storeCoin(coinFirst);

        Coin coinSecond = CreateAvailCoin(amount, 1);
        wdbSecond->storeCoin(coinSecond);
    }

    // m_hRevisionMaxLifeTime, m_hLockTime, m_hPostLockReserve, m_Fee
    Lightning::Channel::Params params = {kRevisionMaxLifeTime, kLockTime, kPostLockReserve, kFee};
    auto laserFirst = std::make_unique<laser::Mediator>(wdbFirst, params);
    auto laserSecond = std::make_unique<laser::Mediator>(wdbSecond, params);

    LaserObserver observer_1, observer_2;
    laser::ChannelIDPtr channel_1, channel_2;
    bool laser1Closed = false, laser2Closed = false;
    bool transferInProgress = false;
    bool closeProcessStarted = false;
    storage::Totals::AssetTotals totals_1, totals_1_a, totals_2, totals_2_a;
    int transfersCount = 0;
    Height openedAt = 0;

    observer_1.onOpened = [&channel_1, &laserFirst, &openedAt] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: first opened";
        channel_1 = chID;
        const auto& channelFirst = laserFirst->getChannel(channel_1);
        openedAt = channelFirst->m_pOpen->m_hOpened;
        LOG_INFO() << "Test laser SEND: openedAt: " << openedAt;
    };
    observer_2.onOpened = [&channel_2] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: second opened";
        channel_2 = chID;
    };
    observer_1.onOpenFailed = observer_2.onOpenFailed = [] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: open failed";
        WALLET_CHECK(false);
    };
    observer_1.onClosed = [&laser1Closed] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: first closed";
        laser1Closed = true;
    };
    observer_2.onClosed = [&laser2Closed] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: second closed";
        laser2Closed = true;
    };
    observer_1.onCloseFailed = observer_2.onCloseFailed = [] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: close failed";
        WALLET_CHECK(false);
    };
    observer_1.onUpdateFinished = [&channel_1, &laserFirst, &transfersCount, &transferInProgress] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: first updated";
        
        const auto& channelFirst = laserFirst->getChannel(channel_1);
        WALLET_CHECK(channelFirst->get_amountCurrentMy() == 
                     channelFirst->get_amountMy() - transfersCount * kTransferFirst);
        transferInProgress = false;
    };
    observer_2.onUpdateFinished = [&channel_2, &laserSecond, &transfersCount] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: second updated";

        const auto& channelSecond = laserSecond->getChannel(channel_2);
        WALLET_CHECK(channelSecond->get_amountCurrentMy() ==
                     channelSecond->get_amountMy() + transfersCount * kTransferFirst);
    };
    observer_1.onTransferFailed = observer_2.onTransferFailed = [&transferInProgress] (const laser::ChannelIDPtr& chID)
    {
        LOG_INFO() << "Test laser SEND: transfer failed";
        WALLET_CHECK(false);
        transferInProgress = false;
    };

    laserFirst->AddObserver(&observer_1);
    laserSecond->AddObserver(&observer_2);

    auto newBlockFunc = [
        &laserFirst,
        &laserSecond,
        &channel_1,
        &channel_2,
        &laser1Closed,
        &laser2Closed,
        &transferInProgress,
        &totals_1,
        &totals_1_a,
        &totals_2,
        &totals_2_a,
        &transfersCount,
        &closeProcessStarted,
        &openedAt
    ] (Height height)
    {
        if (height > kMaxTestHeight)
        {
            LOG_ERROR() << "Test laser SEND: time expired";
            WALLET_CHECK(false);
            io::Reactor::get_Current().stop();
        }

        if (height == kTestStartBlock)
        {
            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            totals_1= totalsCalc_1.GetTotals(Zero);

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            totals_2= totalsCalc_2.GetTotals(Zero);

            laserFirst->WaitIncoming(100000000, 100000000, kFee);
            auto firstWalletID = laserFirst->getWaitingWalletID();
            laserSecond->OpenChannel(100000000, 100000000, kFee, firstWalletID, kOpenTxDh);
        }

        if (channel_1 && channel_2 && height < openedAt + 20 && !transferInProgress)
        {
            transferInProgress = true;
            auto channel2Str = to_hex(channel_2->m_pData, channel_2->nBytes);
            LOG_INFO() << "Test laser SEND: first send to second";
            ++transfersCount;
            WALLET_CHECK(laserFirst->Transfer(kTransferFirst, channel2Str));
        }

        if (channel_1 && channel_2 && height > openedAt + 20 && !transferInProgress && !closeProcessStarted)
        {
            auto channel1Str = to_hex(channel_1->m_pData, channel_1->nBytes);
            WALLET_CHECK(laserFirst->Close(channel1Str));
            closeProcessStarted = true;
        }

        if (laser1Closed && laser2Closed)
        {
            storage::Totals totalsCalc_1(*(laserFirst->getWalletDB()));
            totals_1_a = totalsCalc_1.GetTotals(Zero);

            storage::Totals totalsCalc_2(*(laserSecond->getWalletDB()));
            totals_2_a = totalsCalc_2.GetTotals(Zero);

            WALLET_CHECK(
                totals_1.Unspent ==
                totals_1_a.Unspent + kTransferFirst * transfersCount + kFee / 2 * 3);

            WALLET_CHECK(
                totals_2_a.Unspent + kFee / 2 * 3 ==
                totals_2.Unspent + kTransferFirst * transfersCount);

            LOG_INFO() << "Test laser SEND: finished";
            io::Reactor::get_Current().stop();
        }

    };

    ConfigureNetwork(*laserFirst, *laserSecond);

    io::Timer::Ptr timer = io::Timer::create(*mainReactor);
    TestNode node(newBlockFunc, kNewBlockFunkStart, kDefaultTestNodePort);

    timer->start(kNewBlockInterval, true, [&node]() {node.AddBlock(); });
    mainReactor->run();

    return WALLET_CHECK_RESULT;
}
