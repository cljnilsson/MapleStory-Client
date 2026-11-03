//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton						//
//																				//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by	//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.											//
//																				//
//	This program is distributed in the hope that it will be useful,				//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of				//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the				//
//	GNU Affero General Public License for more details.							//
//																				//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../PacketHandler.h"
#include <vector>
#include <string>

namespace ms
{
	// Server acknowledgements are separate from dispatched requests.
	struct CashShopState
	{
		struct Item { int64_t id, expiration; int32_t account, item, serial; uint16_t quantity; std::string from; };
		bool opened = false, balances_known = false, inventory_known = false;
		int32_t credit = 0, points = 0, prepaid = 0;
		uint64_t revision = 0;
		std::vector<Item> inventory, gifts;
		std::vector<int32_t> wishlist;
		std::string pending, result, rules;
		int message = -1, rules_error = -1;
		int64_t retrieval = 0;
		static CashShopState& get();
		static bool request(const std::string& action, const std::string& args);
		void finish(const std::string& outcome);
	};
	class CashBalanceHandler : public PacketHandler { public: void handle(InPacket&) const override; };
	class CashOperationHandler : public PacketHandler { public: void handle(InPacket&) const override; };
	class NameRulesHandler : public PacketHandler { public: void handle(InPacket&) const override; };
	class WorldRulesHandler : public PacketHandler { public: void handle(InPacket&) const override; };
	// Handler for entering the Cash Shop
	class SetCashShopHandler : public PacketHandler
	{
	public:
		void handle(InPacket& recv) const override;

	private:
		void transition() const;
	};
}