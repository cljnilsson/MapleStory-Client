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
#include <chrono>
#include <string>
#include <vector>

namespace ms
{
	// Tracks received Vega stages; equipment remains owned by inventory packets.
	struct VegaSpellState
	{
		static VegaSpellState &get();
		bool pending = false, started = false, model_updated = false;
		int32_t item = 0, equip_id = 0;
		int16_t cash_slot = 0, equip_slot = 0, scroll_slot = 0;
		int code = 0;
		uint64_t draw_calls = 0;
		bool asset = false;
		std::chrono::steady_clock::time_point requested{};
		int64_t elapsed_ms() const;
	};
	struct OwlSearchState
	{
		struct Row { std::string owner, description; int32_t map, bundles, quantity, price, owner_id; int channel, type; };
		static OwlSearchState &get();
		int32_t item = 0;
		bool known = false;
		std::vector<Row> rows;
		uint64_t draw_calls = 0;
	};
	struct MapleLifeState
	{
		static MapleLifeState &get();
		std::string name;
		bool known = false;
		int code = -1;
		uint64_t draw_calls = 0;
	};
	class OwlSearchHandler : public PacketHandler { void handle(InPacket &) const override; };
	class MapleLifeResultHandler : public PacketHandler { void handle(InPacket &) const override; };
	class MapleLifeErrorHandler : public PacketHandler { void handle(InPacket &) const override; };
	class VegaSpellHandler : public PacketHandler
	{
		void handle(InPacket &recv) const override;
	};

	// Handler for a packet which signifies that inventory items were gathered.
	class GatherResultHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Handler for a packet which signifies that inventory items were sorted.
	class SortResultHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Handler for a packet which modifies the player's inventory.
	class ModifyInventoryHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};
}