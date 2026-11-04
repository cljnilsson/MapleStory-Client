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
#include "InventoryHandlers.h"
#include "../../HeadlessState.h"
#include "../../Data/ItemData.h"
#include <stdexcept>
#include "../../IO/UITypes/UINotice.h"

#include "Helpers/ItemParser.h"

#include "../Gameplay/Stage.h"
#include "../IO/UI.h"
#include "../IO/Messages.h"

#include "../Character/Inventory/Inventory.h"
#include "../IO/UITypes/UIEquipInventory.h"
#include "../IO/UITypes/UIItemInventory.h"
#include "../IO/UITypes/UIKeyConfig.h"
#include "../IO/UITypes/UIShop.h"

namespace ms
{
	OwlSearchState &OwlSearchState::get() { static OwlSearchState state; return state; }
	MapleLifeState &MapleLifeState::get() { static MapleLifeState state; return state; }
	void OwlSearchHandler::handle(InPacket &recv) const
	{
		int mode = uint8_t(recv.read_byte());
		if (mode == 7)
		{
			int count = uint8_t(recv.read_byte());
			for (int i = 0; i < count; ++i) recv.read_int();
			return; // Suggested IDs do not constitute search results.
		}
		if (mode != 6) throw std::runtime_error("Unsupported Owl result mode");
		recv.read_int();
		OwlSearchState next; next.item = recv.read_int();
		int count = recv.read_int();
		if (count < 0 || count > 512) throw std::runtime_error("Owl row limit exceeded");
		std::vector<std::string> lines;
		for (int i = 0; i < count; ++i)
		{
			OwlSearchState::Row row;
			row.owner = recv.read_string(); row.map = recv.read_int(); row.description = recv.read_string();
			row.bundles = recv.read_int(); row.quantity = recv.read_int(); row.price = recv.read_int();
			row.owner_id = recv.read_int(); row.channel = uint8_t(recv.read_byte()) + 1; row.type = uint8_t(recv.read_byte());
			if (row.type == 1) { Inventory preview; ItemParser::parse_item(recv, InventoryType::EQUIP, 1, preview); }
			lines.push_back(row.owner.substr(0, 20) + " CH" + std::to_string(row.channel) + " map " + std::to_string(row.map)
				+ "\\n" + std::to_string(row.price) + " mesos x" + std::to_string(row.quantity)
				+ " (" + std::to_string(row.bundles) + " bundles) " + row.description.substr(0, 30));
			next.rows.push_back(std::move(row));
		}
		next.known = true; OwlSearchState::get() = std::move(next);
		if (lines.empty()) lines.push_back("No shops found. Owl was not consumed.");
		UI::get().emplace<UICashResult>("Owl: " + std::to_string(OwlSearchState::get().item), lines, "owl");
		HeadlessState::event("owl_result", ",\"item\":" + std::to_string(OwlSearchState::get().item) + ",\"rows\":" + std::to_string(count));
	}
	void MapleLifeResultHandler::handle(InPacket &recv) const
	{
		int mode = recv.read_int();
		if (mode == 2)
		{
			auto &state = MapleLifeState::get(); state.known = true; state.code = recv.read_int(); recv.read_byte();
			UI::get().emplace<UICashResult>("Maple Life", std::vector<std::string>{"Name refused. Choose another name."}, "maple_life");
			HeadlessState::event("maple_life_result", ",\"code\":" + std::to_string(state.code));
		}
		else if (mode != 0) throw std::runtime_error("Unsupported Maple Life result mode");
	}
	void MapleLifeErrorHandler::handle(InPacket &recv) const
	{
		recv.read_byte();
		auto &state = MapleLifeState::get(); state.known = true; state.code = recv.read_int();
		std::string message = state.code == 0 ? "Character created. Reconnect to see the character list."
			: state.code == 3 ? "Character creation refused: no free character slot." : "Character creation refused (code " + std::to_string(state.code) + ").";
		UI::get().emplace<UICashResult>("Maple Life", std::vector<std::string>{message}, "maple_life");
		HeadlessState::event("maple_life_result", ",\"code\":" + std::to_string(state.code));
	}
	VegaSpellState &VegaSpellState::get() { static VegaSpellState state; return state; }
	int64_t VegaSpellState::elapsed_ms() const
	{
		return item ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - requested).count() : 0;
	}
	void VegaSpellHandler::handle(InPacket &recv) const
	{
		auto &state = VegaSpellState::get();
		const int code = uint8_t(recv.read_byte());
		HeadlessState::event("vega_packet", ",\"code\":" + std::to_string(code));
		if (!state.pending) return;
		if (code == 0x40)
		{
			state.started = true;
			UI::get().emplace<UIVegaResult>();
		}
		if (code == 0x41 || code == 0x43 || code == 0x42 || code == 0x44)
		{
			state.code = code;
			if (code == 0x42 || code == 0x44) state.pending = false;
		}
	}

	void GatherResultHandler::handle(InPacket&) const
	{
		if (auto iteminventory = UI::get().get_element<UIItemInventory>())
			iteminventory->set_sort(true);
	}

	void SortResultHandler::handle(InPacket&) const
	{
		if (auto iteminventory = UI::get().get_element<UIItemInventory>())
		{
			iteminventory->set_sort(false);
			iteminventory->clear_new();
		}
	}

	void ModifyInventoryHandler::handle(InPacket& recv) const
	{
		recv.read_bool(); // 'updatetick'

		Inventory& inventory = Stage::get()
			.get_player()
			.get_inventory();

		struct Mod
		{
			int8_t mode;
			InventoryType::Id type;
			int16_t pos;
			int16_t arg;
		};

		std::vector<Mod> mods;

		int8_t size = recv.read_byte();

		for (int8_t i = 0; i < size; i++)
		{
			Mod mod;
			mod.mode = recv.read_byte();
			mod.type = InventoryType::by_value(recv.read_byte());
			mod.pos = recv.read_short();

			mod.arg = 0;

			switch (mod.mode)
			{
			case Inventory::Modification::ADD:
				ItemParser::parse_item(recv, mod.type, mod.pos, inventory);
				{
					auto &state = VegaSpellState::get();
					if (state.pending && state.started && (state.code == 0x41 || state.code == 0x43)
						&& mod.type == InventoryType::EQUIP && mod.pos == state.equip_slot
						&& inventory.get_item_id(mod.type, mod.pos) == state.equip_id)
					{
						state.model_updated = true; state.pending = false;
						HeadlessState::event("vega_model", ",\"slot\":" + std::to_string(mod.pos)
							+ ",\"elapsed_ms\":" + std::to_string(state.elapsed_ms()));
					}
				}

				if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
				{
					int16_t count_now = inventory.get_item_count(mod.type, mod.pos);
					keyconfig->update_item_count(mod.type, mod.pos, count_now);
				}

				break;
			case Inventory::Modification::CHANGECOUNT:
			{
				mod.arg = recv.read_short();

				int16_t count_before = inventory.get_item_count(mod.type, mod.pos);
				int16_t count_now = mod.arg;

				inventory.modify(mod.type, mod.pos, mod.mode, mod.arg, Inventory::Movement::MOVE_NONE);

				if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
					keyconfig->update_item_count(mod.type, mod.pos, count_now - count_before);

				if (count_before < count_now)
					mod.mode = Inventory::Modification::ADDCOUNT;
			}
			break;
			case Inventory::Modification::SWAP:
				mod.arg = recv.read_short();
				break;
			case Inventory::Modification::REMOVE:
				if (auto keyconfig = UI::get().get_element<UIKeyConfig>())
				{
					int16_t count_before = inventory.get_item_count(mod.type, mod.pos);
					keyconfig->update_item_count(mod.type, mod.pos, -1 * count_before);
				}

				inventory.modify(mod.type, mod.pos, mod.mode, mod.arg, Inventory::Movement::MOVE_INTERNAL);
				break;
			}

			mods.push_back(mod);
		}

		Inventory::Movement move = (recv.length() > 0) ?
			Inventory::movementbyvalue(recv.read_byte()) :
			Inventory::Movement::MOVE_INTERNAL;

		for (const Mod& mod : mods)
		{
			if (mod.mode == 2)
				inventory.modify(mod.type, mod.pos, mod.mode, mod.arg, move);

			if (auto shop = UI::get().get_element<UIShop>())
				shop->modify(mod.type);

			auto eqinvent = UI::get().get_element<UIEquipInventory>();
			auto itinvent = UI::get().get_element<UIItemInventory>();

			switch (move)
			{
			case Inventory::Movement::MOVE_INTERNAL:
				switch (mod.type)
				{
				case InventoryType::Id::EQUIPPED:
					if (eqinvent)
						eqinvent->modify(mod.pos, mod.mode, mod.arg);

					Stage::get().get_player().change_equip(-mod.pos);
					Stage::get().get_player().change_equip(-mod.arg);
					break;
				case InventoryType::Id::EQUIP:
				case InventoryType::Id::USE:
				case InventoryType::Id::SETUP:
				case InventoryType::Id::ETC:
				case InventoryType::Id::CASH:
					if (itinvent)
						itinvent->modify(mod.type, mod.pos, mod.mode, mod.arg);

					break;
				}

				break;
			case Inventory::Movement::MOVE_EQUIP:
			case Inventory::Movement::MOVE_UNEQUIP:
				if (mod.pos < 0)
				{
					if (eqinvent)
						eqinvent->modify(-mod.pos, 3, 0);

					if (itinvent)
						itinvent->modify(InventoryType::Id::EQUIP, mod.arg, mod.mode, 0);

					Stage::get().get_player().change_equip(-mod.pos);
				}
				else if (mod.arg < 0)
				{
					if (eqinvent)
						eqinvent->modify(-mod.arg, 0, 0);

					if (itinvent)
						itinvent->modify(InventoryType::Id::EQUIP, mod.pos, Inventory::Modification::REMOVE, 0);

					Stage::get().get_player().change_equip(-mod.arg);
				}

				break;
			}
		}

		Stage::get().get_player().recalc_stats(true);
		UI::get().enable();
	}
}