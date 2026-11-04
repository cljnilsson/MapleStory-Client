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

#include "../OutPacket.h"
#include "../../Audio/Audio.h"

#include "../../Character/Inventory/Inventory.h"

namespace ms
{
	// Packet which requests that the inventory is sorted
	// Opcode: GATHER_ITEMS(69)
	class GatherItemsPacket : public OutPacket
	{
	public:
		GatherItemsPacket(InventoryType::Id type) : OutPacket(OutPacket::Opcode::GATHER_ITEMS)
		{
			write_time();
			write_byte(type);
		}
	};

	// Packet which requests that the inventory is sorted
	// Opcode: SORT_ITEMS(70)
	class SortItemsPacket : public OutPacket
	{
	public:
		SortItemsPacket(InventoryType::Id type) : OutPacket(OutPacket::Opcode::SORT_ITEMS)
		{
			write_time();
			write_byte(type);
		}
	};

	// Packet which requests that an item is moved
	// Opcode: MOVE_ITEM(71)
	class MoveItemPacket : public OutPacket
	{
	public:
		MoveItemPacket(InventoryType::Id type, int16_t slot, int16_t action, int16_t qty) : OutPacket(OutPacket::Opcode::MOVE_ITEM)
		{
			write_time();
			write_byte(type);
			write_short(slot);
			write_short(action);
			write_short(qty);
		}
	};

	// Packet which requests that an item is equipped
	// Opcode: MOVE_ITEM(71)
	class EquipItemPacket : public MoveItemPacket
	{
	public:
		EquipItemPacket(int16_t src, EquipSlot::Id dest) : MoveItemPacket(InventoryType::Id::EQUIP, src, -dest, 1) {}
	};

	// Packet which requests that an item is unequipped
	// Opcode: MOVE_ITEM(71)
	class UnequipItemPacket : public MoveItemPacket
	{
	public:
		UnequipItemPacket(int16_t src, int16_t dest) : MoveItemPacket(InventoryType::Id::EQUIP, -src, dest, 1) {}
	};

	// A packet which requests that an 'USE' item is used
	// Opcode: USE_ITEM(72)
	class UseItemPacket : public OutPacket
	{
	public:
		UseItemPacket(int16_t slot, int32_t itemid) : OutPacket(OutPacket::Opcode::USE_ITEM)
		{
			Sound(itemid).play();

			write_time();
			write_short(slot);
			write_int(itemid);
		}
	};

	// v83 cash equipment bodies differ by family; no common leading timestamp.
	class CashEquipmentPacket : public OutPacket
	{
	public:
		CashEquipmentPacket(int16_t slot, int32_t item, int32_t type, int16_t target)
			: OutPacket(OutPacket::Opcode::USE_CASH_ITEM)
		{
			write_short(slot); write_int(item);
			if (item == 5060000) { write_short(target); write_time(); }
			else if (item / 10000 == 557) { write_int(1); write_int(target); write_int(0); }
			else { write_int(type); write_int(target); if (item / 10000 == 506) write_time(); }
		}
	};

	// Cash broadcast body follows the original UseCashItemHandler family layout.
	class CashBroadcastPacket : public OutPacket
	{
	public:
		CashBroadcastPacket(int16_t slot, int32_t item, const std::vector<std::string> &lines, bool ear, int type, int target, const std::string &partner)
			: OutPacket(Opcode::USE_CASH_ITEM)
		{
			write_short(slot); write_int(item);
			int family = item / 10000, kind = (item / 1000) % 10;
			if (family == 539) { for (const auto &line : lines) write_string(line); write_byte(ear); }
			else if (family != 507 || kind == 1) write_string(lines[0]);
			else if (kind == 2) { write_string(lines[0]); write_byte(ear); }
			else if (kind == 6) { write_string(lines[0]); write_byte(ear); write_byte(type != 0); if (type) { write_int(type); write_int(target); } }
			else if (kind == 7) { write_byte(lines.size()); for (const auto &line : lines) write_string(line); write_byte(ear); }
			else if (kind == 5)
			{
				int tv = item % 10;
				if (tv != 1) { if (tv >= 3) { if (tv == 3) write_byte(0); write_byte(ear); } else if (tv != 2) write_byte(0); if (tv != 4) write_string(partner == "-" ? "" : partner); }
				for (const auto &line : lines) write_string(line); write_int(0);
			}
		}
	};

	class OwlSearchPacket : public OutPacket
	{
	public:
		OwlSearchPacket(int16_t slot, int32_t item, int32_t searched) : OutPacket(Opcode::USE_CASH_ITEM)
		{ write_short(slot); write_int(item); write_int(searched); }
	};
	class MapleLifePacket : public OutPacket
	{
	public:
		MapleLifePacket(int16_t slot, int32_t item, const std::string &name, const std::vector<int32_t> &values)
			: OutPacket(Opcode::USE_CASH_ITEM)
		{ write_short(slot); write_int(item); write_string(name); for (int32_t value : values) write_int(value); }
	};
	class VegaSpellPacket : public OutPacket
	{
	public:
		VegaSpellPacket(int16_t cash_slot, int32_t item, int16_t equip_slot, int16_t scroll_slot)
			: OutPacket(Opcode::USE_CASH_ITEM)
		{ write_short(cash_slot); write_int(item); write_int(1); write_int(equip_slot); write_int(2); write_int(scroll_slot); }
	};

	// v83 cash reset body has no timestamp; destination precedes source.
	class CashResetPacket : public OutPacket
	{
	public:
		CashResetPacket(int16_t slot, int32_t item, int32_t to, int32_t from)
			: OutPacket(Opcode::USE_CASH_ITEM)
		{ write_short(slot); write_int(item); write_int(to); write_int(from); }
	};

	class ScriptedItemPacket : public OutPacket
	{
	public:
		ScriptedItemPacket(int16_t slot, int32_t item) : OutPacket(Opcode::SCRIPTED_ITEM)
		{ write_time(); write_short(slot); write_int(item); }
	};
	class UseChairPacket : public OutPacket
	{
	public:
		UseChairPacket(int32_t item) : OutPacket(Opcode::USE_CHAIR) { write_int(item); }
	};
	class CancelChairPacket : public OutPacket
	{
	public:
		CancelChairPacket(int16_t seat) : OutPacket(Opcode::CANCEL_CHAIR) { write_short(seat); }
	};

	// Requests using a scroll on an equip 
	// Opcode: SCROLL_EQUIP(86)
	class ScrollEquipPacket : public OutPacket
	{
	public:
		enum Flag : uint8_t
		{
			NONE = 0x00,
			UNKNOWN = 0x01,
			WHITESCROLL = 0x02
		};

		ScrollEquipPacket(int16_t source, EquipSlot::Id target, uint8_t flags) : OutPacket(OutPacket::Opcode::SCROLL_EQUIP)
		{
			write_time();
			write_short(source);
			write_short(-target);
			write_short(flags);
		}

		ScrollEquipPacket(int16_t source, EquipSlot::Id target) : ScrollEquipPacket(source, target, 0) {}
	};
}