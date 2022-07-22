// Copyright 2019 The Beam Team
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
#include "dex_board.h"
#include "wallet/transactions/dex/dex_tx.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"
#include "wallet/client/wallet_client.h"

namespace beam::wallet {

    DexBoard::DexBoard(IBroadcastMsgGateway& gateway, IWalletModelAsync::Ptr wallet, IWalletDB& wdb)
        : _gateway(gateway)
        , _wallet(std::move(wallet))
        , _wdb(wdb)
    {
        _gateway.registerListener(BroadcastContentType::DexOffers, this);
    }

    std::vector<DexOrder> DexBoard::getDexOrders() const
    {
        std::vector<DexOrder> result;
        result.reserve(_orders.size());

        for (auto pair: _orders)
        {
            result.push_back(pair.second);
        }

        return result;
    }

    boost::optional<DexOrder> DexBoard::getDexOrder(const DexOrderID& orderId) const
    {
        const auto it = _orders.find(orderId);
        if (it != _orders.end())
        {
            return it->second;
        }
        return boost::none;
    }

    void DexBoard::publishOrder(const DexOrder &offer)
    {
        auto message = createMessage(offer);
        _gateway.sendMessage(BroadcastContentType::DexOffers, message);
    }

    bool DexBoard::handleDexOrder(const boost::optional<DexOrder>& order)
    {
        LOG_INFO() << "DexBoard oder message received";

        auto it = _orders.find(order->getID());
        _orders[order->getID()] = *order;

        if (it == _orders.end())
        {
            notifyObservers(ChangeAction::Added, std::vector<DexOrder>{ *order });
        }
        else
        {
            notifyObservers(ChangeAction::Updated, std::vector<DexOrder>{ *order });
        }

        return true;
    }

    bool DexBoard::onMessage(uint64_t, BroadcastMsg&& msg)
    {
        auto order = parseAssetSwapMessage(msg);
        return order ? handleDexOrder(order) : false;
    }

    BroadcastMsg DexBoard::createMessage(const DexOrder& order)
    {
        const auto pkdf = _wdb.get_SbbsKdf();

        auto sk = order.derivePrivateKey(pkdf);
        auto buffer = toByteBuffer(order);
        auto message = BroadcastMsgCreator::createSignedMessage(buffer, sk);

        return message;
    }

    boost::optional<DexOrder> DexBoard::parseAssetSwapMessage(const BroadcastMsg& msg)
    {
        try
        {
            const auto pkdf = _wdb.get_SbbsKdf();
            DexOrder order(msg.m_content, msg.m_signature, pkdf);
            return order;
        }
        catch(std::runtime_error& err)
        {
            LOG_WARNING() << "DexBoard parse error: " << err.what();
            return boost::none;
        }
        catch(...)
        {
            LOG_WARNING() << "DexBoard message type parse error";
            return boost::none;
        }
    }

    void DexBoard::notifyObservers(ChangeAction action, const std::vector<DexOrder>& orders) const
    {
        for (const auto obs : _observers)
        {
            obs->onDexOrdersChanged(action, orders);
        }
    }

    bool DexBoard::acceptIncomingDexSS(const SetTxParameter& msg)
    {
        // TODO:DEX perform real check
        // always true is only for tests
        return true;
    }

    void DexBoard::onDexTxCreated(const SetTxParameter& msg, BaseTransaction::Ptr)
    {
        // TODO:DEX associate with the real order
    }
}
