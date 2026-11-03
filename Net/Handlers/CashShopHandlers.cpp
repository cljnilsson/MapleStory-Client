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
#include "CashShopHandlers.h"

#include "Helpers/CashShopParser.h"

#include "../../Gameplay/Stage.h"
#include "../../IO/UI.h"
#include "../../IO/Window.h"

#include "Timer.h"
#include "../../HeadlessState.h"
#include "Helpers/ItemParser.h"
#include "../Packets/GameplayPackets.h"
#include <sstream>
#include <algorithm>

namespace ms
{
	void SetCashShopHandler::handle(InPacket& recv) const
	{
		CashShopParser::parseCharacterInfo(recv);

		recv.skip_byte();	// Not MTS
		recv.skip_string();	// account_name
		recv.skip_int();

		int16_t specialcashitem_size = recv.read_short();

		for (size_t i = 0; i < specialcashitem_size; i++)
		{
			recv.skip_int();	// sn
			recv.skip_int();	// mod
			recv.skip_byte();	// info
		}

		recv.skip(121);

		if (recv.length() < 11 || (recv.length() - 11) % 12 != 0 || recv.length() > 11 + 960)
			throw PacketError("Unsupported Cash Shop seller list");
		recv.skip(recv.length() - 11);

		recv.skip_int();
		recv.skip_short();
		recv.skip_byte();
		recv.skip_int();

		CashShopState::get() = CashShopState{};
		CashShopState::get().opened = true;
		transition();

		UI::get().change_state(UI::State::CASHSHOP);
		HeadlessState::set_screen("cash_shop");
	}

	void SetCashShopHandler::transition() const
	{
		Constants::Constants::get().set_viewwidth(1024);
		Constants::Constants::get().set_viewheight(768);

		float fadestep = 0.025f;

		Window::get().fadeout(
			fadestep,
			[]()
			{
				GraphicsGL::get().clear();

				Stage::get().load(-1, 0);

				UI::get().enable();
				Timer::get().start();
				GraphicsGL::get().unlock();
			}
		);

		GraphicsGL::get().lock();
		Stage::get().clear();
		Timer::get().start();
	}
	CashShopState& CashShopState::get() { static CashShopState state; return state; }
	void CashShopState::finish(const std::string& outcome)
	{
		result = outcome; pending.clear(); ++revision;
		HeadlessState::event("cash_result", ",\"result\":\"" + outcome + "\",\"revision\":" + std::to_string(revision));
	}

	// v83 request bodies, matching HeavenMS CashOperation/Coupon/Transfer handlers.
	class CashRequest : public OutPacket
	{
	public:
		CashRequest(int16_t opcode) : OutPacket(opcode) {}
		using OutPacket::write_byte; using OutPacket::write_int; using OutPacket::write_short; using OutPacket::write_string;
	};
	bool CashShopState::request(const std::string& action, const std::string& args)
	{
		auto& s = get();
		if (!s.opened || !s.pending.empty()) return false;
		std::istringstream in(args);
		auto done = [&] { in >> std::ws; return in.eof(); };
		int32_t a = 0, b = 0;
		CashRequest packet(action == "cash-coupon" ? 0xe6 : action == "cash-name-rules" ? 0x10 : action == "cash-world-rules" ? 0x12 : 0xe5);
		if (action == "cash-buy")
		{
			if (!(in >> a >> b) || !done() || (a != 1 && a != 2 && a != 4) || b <= 0) return false;
			packet.write_byte(3); packet.write_byte(0); packet.write_int(a); packet.write_int(b);
		}
		else if (action == "cash-retrieve")
		{
			if (!(in >> a) || !done() || a <= 0 || !s.inventory_known) return false;
			auto item = std::find_if(s.inventory.begin(), s.inventory.end(), [a](const Item& item) { return item.id == a; });
			if (item == s.inventory.end()) return false;
			s.retrieval = a;
			packet.write_byte(0x0d); packet.write_int(a);
		}
		else if (action == "cash-gift")
		{
			std::string recipient, message;
			if (!(in >> a >> b >> recipient) || a < 0 || b <= 0 || recipient.size() > 13) return false;
			std::getline(in >> std::ws, message);
			if (message.size() > 73) return false;
			packet.write_byte(4); packet.write_int(a); packet.write_int(b); packet.write_string(recipient); packet.write_string(message);
		}
		else if (action == "cash-coupon")
		{
			if (args.empty() || args.size() > 64 || args.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) return false;
			packet.write_short(0); packet.write_string(args);
		}
		else if (action == "cash-name-rules" || action == "cash-world-rules")
		{
			if (!(in >> a) || !done() || a < 0) return false;
			packet.write_int(Stage::get().get_player().get_oid()); packet.write_int(a);
			s.rules.clear(); s.rules_error = -1;
		}
		else return false;
		s.pending = action; s.result = "pending"; s.message = -1; ++s.revision;
		packet.dispatch();
		return true;
	}
	static CashShopState::Item cash_item(InPacket& recv, bool gift = false)
	{
		CashShopState::Item item{};
		item.id = recv.read_long();
		if (!gift) { item.account = recv.read_int(); recv.skip_int(); }
		item.item = recv.read_int();
		if (!gift) { item.serial = recv.read_int(); item.quantity = uint16_t(recv.read_short()); }
		item.from = recv.read_padded_string(13);
		if (gift) recv.skip(73);
		else { item.expiration = recv.read_long(); recv.skip_long(); }
		return item;
	}
	void CashBalanceHandler::handle(InPacket& recv) const
	{
		if (recv.length() != 12) throw PacketError("Cash balance length");
		auto& s = CashShopState::get();
		s.credit = recv.read_int(); s.points = recv.read_int(); s.prepaid = recv.read_int(); s.balances_known = true; ++s.revision;
	}
	void CashOperationHandler::handle(InPacket& recv) const
	{
		auto& s = CashShopState::get();
		const uint8_t op = recv.read_byte();
		if (op == 0x4b || op == 0x4d)
		{
			const uint16_t count = recv.read_short();
			if (count > 512) throw PacketError("Cash inventory exceeds snapshot bound");
			std::vector<CashShopState::Item> items;
			for (uint16_t i = 0; i < count; ++i) items.push_back(cash_item(recv, op == 0x4d));
			if (op == 0x4b) { recv.skip(4); s.inventory = std::move(items); s.inventory_known = true; }
			else s.gifts = std::move(items);
			++s.revision;
		}
		else if (op == 0x4f || op == 0x55)
		{
			std::vector<int32_t> list;
			for (int i = 0; i < 10; ++i) { int32_t sn = recv.read_int(); if (sn) list.push_back(sn); }
			s.wishlist = std::move(list); ++s.revision;
		}
		else if (op == 0x57 || op == 0x6a)
		{
			auto item = cash_item(recv);
			if (s.inventory.size() >= 512) throw PacketError("Cash inventory exceeds bound");
			s.inventory.push_back(std::move(item)); s.finish(op == 0x57 ? "purchase" : "deposit");
		}
		else if (op == 0x68)
		{
			const int16_t slot = recv.read_short();
			if (recv.length() < 8) throw PacketError("Cash retrieval item missing");
			// Inspect the item ID after its type byte without consuming the item record.
			const int32_t id = int32_t((uint64_t(recv.inspect_long()) >> 8) & 0xffffffff); // at least 8 bytes in every item record
			ItemParser::parse_item(recv, InventoryType::by_item_id(id), slot, Stage::get().get_player().get_inventory());
			s.inventory.erase(std::remove_if(s.inventory.begin(), s.inventory.end(), [&](const CashShopState::Item& item) { return item.id == s.retrieval; }), s.inventory.end());
			s.retrieval = 0; s.finish("retrieval");
		}
		else if (op == 0x5c) { s.message = uint8_t(recv.read_byte()); s.finish("refused"); }
		else if (op == 0x5e) { recv.skip_string(); recv.skip(10); s.finish("gift"); }
		else if (op == 0x8d) { recv.skip(12); s.finish("coupon"); }
		else if (op == 0x59)
		{
			uint8_t count = recv.read_byte();
			if (s.inventory.size() + count > 512) throw PacketError("Coupon cash inventory exceeds bound");
			std::vector<CashShopState::Item> items;
			for (uint8_t i = 0; i < count; ++i) items.push_back(cash_item(recv));
			recv.skip_int(); const int32_t plain = recv.read_int();
			if (plain < 0 || plain > 512) throw PacketError("Coupon items exceed bound");
			recv.skip(size_t(plain) * 8); recv.skip_int();
			s.inventory.insert(s.inventory.end(), items.begin(), items.end()); s.finish("coupon");
		}
		else { HeadlessState::event("cash_unsupported", ",\"operation\":" + std::to_string(op)); return; }
		if (recv.available()) throw PacketError("Cash operation trailing bytes");
	}
	void NameRulesHandler::handle(InPacket& recv) const
	{
		recv.skip_int(); const uint8_t error = recv.read_byte(); recv.skip_int();
		if (recv.available()) throw PacketError("Name rules trailing bytes");
		auto& s = CashShopState::get(); s.rules = "name"; s.rules_error = error; s.finish("rules");
	}
	void WorldRulesHandler::handle(InPacket& recv) const
	{
		recv.skip_int(); const uint8_t error = recv.read_byte(); recv.skip_int();
		bool worlds = recv.read_bool();
		if (worlds) { int32_t count = recv.read_int(); if (count < 0 || count > 32) throw PacketError("World list bound"); for (int32_t i = 0; i < count; ++i) recv.skip_string(); }
		if (recv.available()) throw PacketError("World rules trailing bytes");
		auto& s = CashShopState::get(); s.rules = "world"; s.rules_error = error; s.finish("rules");
	}
}