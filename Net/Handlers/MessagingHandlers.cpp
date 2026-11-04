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
#include "../../IO/UITypes/UISocial.h"
#include "../../HeadlessState.h"
#include "MessagingHandlers.h"
#include "Helpers/LoginParser.h"
#include "Helpers/ItemParser.h"
#include "../../Graphics/Geometry.h"
#include "../../Constants.h"
#include <sstream>
#include <nlnx/nx.hpp>
#include <stdexcept>
#include "../../IO/UI.h"

#include "../../Data/ItemData.h"
#include "../../Gameplay/Stage.h"
#include "../../IO/UI.h"

#include "../../IO/UITypes/UIChatBar.h"
#include "../../IO/UITypes/UIStatusMessenger.h"

namespace ms
{
	int64_t BroadcastDisplay::age_ms() const
	{ return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - received).count(); }
	BroadcastState &BroadcastState::get() { static BroadcastState state; return state; }
	static void clear_broadcast(BroadcastDisplay &d)
	{
		if (!d.active) return;
		d.active = false; d.cleared_ms = d.age_ms();
		HeadlessState::event("broadcast_removed", ",\"family\":\"" + d.family + "\",\"id\":" + std::to_string(d.id) + ",\"elapsed_ms\":" + std::to_string(d.cleared_ms));
	}
	void BroadcastState::notice(BroadcastDisplay d)
	{
		d.active = true;
		if (auto bar = UI::get().get_element<UIChatBar>()) bar->send_chatline(d.message, UIChatBar::LineType::YELLOW);
		notices.push_back(std::move(d));
		while (notices.size() > 20) notices.pop_front();
	}
	void BroadcastState::clear_field()
	{ kites.clear(); clear_broadcast(weather); }
	void BroadcastState::update()
	{
		for (auto &d : notices) if (d.active && d.age_ms() >= 10000) clear_broadcast(d);
		if (weather.active) weather.effect.update(8);
		for (auto &entry : kites) if (entry.second.active) entry.second.effect.update(8);
		for (auto *d : {&avatar, &tv}) if (d->active) { if (d->look) d->look->update(8); if (d->partner) d->partner->update(8); }
	}
	static void broadcast_drawn(BroadcastDisplay &d)
	{
		if (++d.draws == 1) HeadlessState::event("broadcast_draw", ",\"family\":\"" + d.family + "\",\"id\":" + std::to_string(d.id));
	}
	void BroadcastState::draw(Point<int16_t> view)
	{
		auto panel = [](BroadcastDisplay &d, int16_t x, int16_t y, int16_t w, int16_t h, const std::string &title) {
			ColorBox(w, h, Color::Name::BLACK, .85f).draw(Point<int16_t>(x, y));
			if (d.layout_width != w || d.layout_title != title)
			{
				d.layout_width = w; d.layout_title = title;
				d.title_text = Text(Text::A12B, Text::LEFT, Color::Name::YELLOW, title, w - 16, false);
				d.lines.clear(); std::istringstream input(d.message); std::string line;
				while (std::getline(input, line)) d.lines.emplace_back(Text::A12M, Text::LEFT, Color::Name::WHITE, line, w - (d.look ? 110 : 16), false);
			}
			d.title_text.draw(Point<int16_t>(x + 8, y + 5));
			int16_t row_y = 25;
			for (const auto &line : d.lines)
			{
				if (row_y >= h - 4) break;
				line.draw(Point<int16_t>(x + 8, y + row_y), Range<int16_t>(y + 25, y + h - 4));
				row_y += line.height() + 3;
			}
			if (d.look) d.look->draw(Point<int16_t>(x + w - 40, y + h - 8), false, Stance::STAND1, Expression::DEFAULT);
			if (d.partner) d.partner->draw(Point<int16_t>(x + w - 85, y + h - 8), true, Stance::STAND1, Expression::DEFAULT);
			broadcast_drawn(d);
		};
		int16_t width = Constants::Constants::get().get_viewwidth();
		int shown = 0;
		for (auto it = notices.rbegin(); it != notices.rend() && shown < 2; ++it) if (it->active)
			panel(*it, 10, 35 + 92 * shown++, width - 20, 88, "Broadcast " + std::to_string(it->id) + (it->channel ? " / CH " + std::to_string(it->channel) : "") + (it->item ? " / " + ItemData::get(it->item).get_name() : ""));
		if (avatar.active) panel(avatar, width - 390, 230, 380, 130, "Avatar / " + avatar.owner + " / CH " + std::to_string(avatar.channel));
		if (tv.active) panel(tv, 10, 365, width - 20, 145, "Maple TV / " + tv.owner + (tv.partner_name.empty() ? "" : " + " + tv.partner_name));
		if (weather.active)
		{
			if (weather.asset)
				for (int i = 0; i < 36; ++i)
					weather.effect.draw(Point<int16_t>((i * 173 + weather.age_ms() / 40) % width, (i * 97 + weather.age_ms() / 12) % 520), 1.0f);
			panel(weather, 10, 515, width - 20, 65, "Weather / " + ItemData::get(weather.item).get_name());
		}
		for (auto &entry : kites)
		{
			auto &d = entry.second; if (!d.active) continue;
			int16_t x = d.x + view.x(), y = d.y + view.y() - 125;
			if (x < -180 || x > width || y < -100 || y > Constants::Constants::get().get_viewheight()) continue;
			// A readable field marker anchored to the received foothold, never the viewer.
			ColorLine(1, Color::Name::YELLOW, .8f).draw(Point<int16_t>(x, y + 85));
			panel(d, x, y, 220, 85, "Kite / " + d.owner);
			if (d.asset) d.effect.draw(Point<int16_t>(x + 195, y + 60), 1.0f);
			else ItemData::get(d.item).get_icon(false).draw(Point<int16_t>(x + 190, y + 55));
		}
	}
	void BroadcastState::snapshot(std::ostream &o) const
	{
		auto write = [&](const BroadcastDisplay &d) {
			o << "{\"family\":\"" << HeadlessState::escape(d.family) << "\",\"owner\":\"" << HeadlessState::escape(d.owner)
			  << "\",\"message\":\"" << HeadlessState::escape(d.message) << "\",\"id\":" << d.id << ",\"item\":" << d.item
			  << ",\"channel\":" << d.channel << ",\"ear\":" << (d.ear ? "true" : "false") << ",\"active\":" << (d.active ? "true" : "false")
			  << ",\"draw_calls\":" << d.draws << ",\"age_ms\":" << d.age_ms() << ",\"cleared_ms\":" << d.cleared_ms
			  << ",\"x\":" << d.x << ",\"y\":" << d.y << ",\"foothold\":" << d.foothold
			  << ",\"asset\":" << (d.asset ? "true" : "false") << ",\"asset_path\":\"" << HeadlessState::escape(d.asset_path) << "\""
			  << ",\"portrait\":" << (d.look ? "true" : "false") << ",\"partner\":\"" << HeadlessState::escape(d.partner_name) << "\"}";
		};
		o << ",\"broadcasts\":{\"avatar\":"; write(avatar); o << ",\"tv\":"; write(tv); o << ",\"weather\":"; write(weather);
		o << ",\"notices\":["; bool first = true; for (const auto &d : notices) { if (!first) o << ','; first = false; write(d); }
		o << "],\"kites\":["; first = true; for (const auto &entry : kites) { if (!first) o << ','; first = false; write(entry.second); } o << "]}";
	}
	void BroadcastEffectHandler::handle(InPacket &recv) const
	{
		auto &s = BroadcastState::get(); BroadcastDisplay d; d.active = true;
		if (opcode == 0x70) { recv.read_byte(); clear_broadcast(s.avatar); return; }
		if (opcode == 0x156) { clear_broadcast(s.tv); return; }
		if (opcode == 0x157) { recv.read_int(); recv.read_byte(); return; }
		if (opcode == 0x110)
		{ recv.read_byte(); int id = recv.read_int(); auto it = s.kites.find(id); if (it != s.kites.end()) clear_broadcast(it->second); return; }
		if (opcode == 0x10e) { d.family = "kite_refusal"; d.message = "A kite cannot be placed here."; s.notice(std::move(d)); return; }
		if (opcode == 0x8e)
		{
			bool removal = recv.read_byte() != 0; int item = recv.read_int();
			if (removal) { clear_broadcast(s.weather); return; }
			d.family = "weather"; d.item = item; d.message = recv.read_string();
			d.asset_path = nl::nx::item["Cash"]["0" + std::to_string(item / 10000) + ".img"]["0" + std::to_string(item)]["info"]["path"].get_string();
			if (d.asset_path.rfind("Map/", 0) == 0)
			{
				auto node = nl::nx::map.resolve(d.asset_path.substr(4));
				d.asset = static_cast<bool>(node["0"]); if (d.asset) d.effect = Animation(node);
			}
			s.weather = std::move(d);
		}
		if (opcode == 0x10f)
		{
			d.family = "kite"; d.id = recv.read_int(); d.item = recv.read_int(); d.message = recv.read_string(); d.owner = recv.read_string();
			auto node = nl::nx::item["Cash"]["0" + std::to_string(d.item / 10000) + ".img"]["0" + std::to_string(d.item)]["iconReward"];
			d.asset_path = "Item/Cash/0" + std::to_string(d.item / 10000) + ".img/0" + std::to_string(d.item) + "/iconReward";
			d.asset = static_cast<bool>(node["0"]); if (d.asset) d.effect = Animation(node);
			d.x = recv.read_short(); d.foothold = recv.read_short(); d.y = Stage::get().foothold_y(d.foothold, d.x);
			if (s.kites.size() >= 100) s.kites.erase(s.kites.begin()); s.kites[d.id] = std::move(d);
		}
		if (opcode == 0x6f)
		{
			d.family = "avatar"; d.item = recv.read_int(); d.owner = recv.read_string();
			for (int i = 0; i < 4; ++i) { if (i) d.message += "\n"; d.message += recv.read_string(); }
			d.channel = recv.read_int() + 1; d.ear = recv.read_bool(); d.look = std::make_shared<CharLook>(LoginParser::parse_look(recv)); s.avatar = std::move(d);
		}
		if (opcode == 0x155)
		{
			d.family = "tv"; int mode = recv.read_byte(); d.id = recv.read_byte(); d.look = std::make_shared<CharLook>(LoginParser::parse_look(recv));
			d.owner = recv.read_string(); d.partner_name = recv.read_string();
			for (int i = 0; i < 5; ++i) { if (i) d.message += "\n"; d.message += recv.read_string(); }
			recv.read_int(); if (mode == 3) d.partner = std::make_shared<CharLook>(LoginParser::parse_look(recv)); s.tv = std::move(d);
		}
		HeadlessState::event("broadcast_received", ",\"opcode\":" + std::to_string(opcode));
	}

	void PlayerHintHandler::handle(InPacket& recv) const
	{
		std::string message = recv.read_string();
		int16_t width = recv.read_short();
		int16_t height = recv.read_short();
		recv.read_byte(); // Display flag.

        // Keep server-authored rate/coupon text; no inferred coupon buff icon.
        if(message.rfind("#eEXP RATE",0)==0 || message.rfind("#eCHARACTER RATES",0)==0)
            SocialState::get().rates_hint=message;
		UI::get().show_hint(message, width, height);
		HeadlessState::event("hint_shown", ",\"text\":\"" + HeadlessState::escape(message) + "\",\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height));
	}

	// Modes:
	// 0 - Item(0) or Meso(1) 
	// 3 - Exp gain
	// 4 - Fame
	// 5 - Mesos
	// 6 - Guild points
	void ShowStatusInfoHandler::handle(InPacket& recv) const
	{
		int8_t mode = recv.read_byte();

		if (mode == 0)
		{
			int8_t mode2 = recv.read_byte();

			if (mode2 == -1)
			{
				show_status(Color::Name::WHITE, "You can't get anymore items.");
			}
			else if (mode2 == 0)
			{
				int32_t itemid = recv.read_int();
				int32_t qty = recv.read_int();

				const ItemData& idata = ItemData::get(itemid);

				if (!idata.is_valid())
					return;

				std::string name = idata.get_name();

				if (name.length() > 21)
				{
					name.substr(0, 21);
					name += "..";
				}

				InventoryType::Id type = InventoryType::by_item_id(itemid);

				std::string tab = "";

				switch (type)
				{
				case InventoryType::Id::EQUIP:
					tab = "Eqp";
					break;
				case InventoryType::Id::USE:
					tab = "Use";
					break;
				case InventoryType::Id::SETUP:
					tab = "Setup";
					break;
				case InventoryType::Id::ETC:
					tab = "Etc";
					break;
				case InventoryType::Id::CASH:
					tab = "Cash";
					break;
				default:
					tab = "UNKNOWN";
					break;
				}

				// TODO: show_status(Color::Name::WHITE, "You have lost items in the " + tab + " tab (" + name + " " + std::to_string(qty) + ")");

				if (qty < 0)
					show_status(Color::Name::WHITE, "You have lost an item in the " + tab + " tab (" + name + ")");
				else if (qty == 1)
					show_status(Color::Name::WHITE, "You have gained an item in the " + tab + " tab (" + name + ")");
				else
					show_status(Color::Name::WHITE, "You have gained items in the " + tab + " tab (" + name + " " + std::to_string(qty) + ")");
			}
			else if (mode2 == 1)
			{
				recv.skip(1);

				int32_t gain = recv.read_int();
				std::string sign = (gain < 0) ? "-" : "+";

				show_status(Color::Name::WHITE, "You have gained mesos (" + sign + std::to_string(gain) + ")");
			}
			else
			{
				show_status(Color::Name::RED, "Mode: 0, Mode 2: " + std::to_string(mode2) + " is not handled.");
			}
		}
		else if (mode == 1)
		{
			int16_t id = recv.read_short();
			int status = recv.read_byte();
			auto &quests = Stage::get().get_player().get_quests();
			if (status == 1) quests.add_started(id, recv.read_string());
			else if (status == 2) quests.add_completed(id, recv.read_long());
			else if (status == 0) quests.remove(id);
		}
		else if (mode == 3)
		{
			bool white = recv.read_bool();
			int32_t gain = recv.read_int();
			bool inchat = recv.read_bool();
			int32_t bonus1 = recv.read_int();

			recv.read_short();
			recv.read_int();	// bonus 2
			recv.read_bool();	// 'event or party'
			recv.read_int();	// bonus 3
			recv.read_int();	// bonus 4
			recv.read_int();	// bonus 5

			std::string message = "You have gained experience (+" + std::to_string(gain) + ")";

			if (inchat)
			{
				show_status(Color::Name::RED, "Mode: 3, inchat is not handled.");
			}
			else
			{
				show_status(white ? Color::Name::WHITE : Color::Name::YELLOW, message);

				if (bonus1 > 0)
					show_status(Color::Name::YELLOW, "+ Bonus EXP (+" + std::to_string(bonus1) + ")");
			}
		}
		else if (mode == 4)
		{
			int32_t gain = recv.read_int();
			std::string sign = (gain < 0) ? "-" : "+";

			// TODO: Lose fame?
			show_status(Color::Name::WHITE, "You have gained fame. (" + sign + std::to_string(gain) + ")");
		}
		else
		{
			show_status(Color::Name::RED, "Mode: " + std::to_string(mode) + " is not handled.");
		}
	}

	void ShowStatusInfoHandler::show_status(Color::Name color, const std::string& message) const
	{
		if (auto messenger = UI::get().get_element<UIStatusMessenger>())
			messenger->show_status(color, message);
	}

	void ServerMessageHandler::handle(InPacket& recv) const
	{
		int type = recv.read_byte();
		// Only scrolling messages have a leading enable byte, never infer it
		// from a message length (an odd length previously ate a byte).
		if (type == 4 && !recv.read_bool()) { UI::get().set_scrollnotice(""); return; }
		BroadcastDisplay display; display.family = "notice"; display.id = type;
		display.message = recv.read_string();
		auto separator = display.message.find(" : ");
		if (separator != std::string::npos) display.owner = display.message.substr(0, separator);
		if (type == 3 || type == 8) { display.channel = recv.read_byte() + 1; display.ear = recv.read_bool(); }
		if (type == 8 && recv.read_byte() != 0)
		{
			Inventory preview;
			int kind = recv.read_byte(); int iid = recv.read_int();
			if (kind == 1) ItemParser::add_equip(recv, InventoryType::EQUIP, 1, iid, preview);
			else if (kind == 3) ItemParser::add_pet(recv, InventoryType::CASH, 1, iid, preview);
			else ItemParser::add_item(recv, InventoryType::USE, 1, iid, preview);
			display.item = iid;
		}
		if (type == 10)
		{
			int count = recv.read_byte();
			if (count < 1 || count > 3) throw std::runtime_error("Invalid multi-megaphone line count");
			for (int i = 1; i < count; ++i) display.message += "\n" + recv.read_string();
			display.channel = recv.read_byte() + 1; recv.skip(9); display.ear = recv.read_bool(); recv.read_byte();
		}
		if ((type == 6 || type == 7) && recv.length() >= 4) recv.read_int();
		if (type == 4) UI::get().set_scrollnotice(display.message);
		HeadlessState::event("notice", ",\"kind\":" + std::to_string(type) + ",\"text\":\"" + HeadlessState::escape(display.message) + "\"");
		BroadcastState::get().notice(std::move(display));
	}

	void WeekEventMessageHandler::handle(InPacket& recv) const
	{
		recv.read_byte(); // TODO: Always 0xFF, Check this!

		std::string message = recv.read_string();

		static const std::string MAPLETIP = "[MapleTip]";

		if (message.substr(0, MAPLETIP.length()).compare("[MapleTip]"))
			message = "[Notice] " + message;

		UI::get().get_element<UIChatBar>()->send_chatline(message, UIChatBar::LineType::YELLOW);
	}

	void SocialChatHandler::handle(InPacket &recv) const
	{
		std::string line;
		if (opcode == 0x86) {
			int mode = recv.read_byte();
			static const char *families[] = { "Buddy", "Party", "Guild", "Alliance" };
			auto name = recv.read_string(); auto text = recv.read_string();
			line = "[" + std::string(mode >= 0 && mode < 4 ? families[mode] : "Group") + "] " + name + ": " + text;
		} else if (opcode == 0x87) {
			int mode = recv.read_byte(); auto name = recv.read_string();
			if (mode == 0x12) {
				int channel = recv.read_short() + 1;
				line = "[Whisper CH" + std::to_string(channel) + "] " + name + ": " + recv.read_string();
			} else if (mode == 0x0a) {
				line = "[Whisper] " + name + (recv.read_bool() ? ": delivered" : ": unavailable");
			} else if (mode == 9 || mode == 72) {
				int where = recv.read_byte(); int location = recv.read_int();
				if (where == 1) recv.skip(8);
				line = "[Find] " + name + (where == 3 ? " is on channel " + std::to_string(location + 1) : where == 1 ? " is in map " + std::to_string(location) : " is unavailable");
			} else throw std::runtime_error("Unsupported whisper mode " + std::to_string(mode));
		} else {
			int mode = recv.read_byte();
			auto name = mode == 5 ? recv.read_string() : std::string();
			recv.read_byte();
			line = "[Spouse] " + (name.empty() ? "" : name + ": ") + recv.read_string();
		}
		if (auto chat = UI::get().get_element<UIChatBar>()) chat->send_chatline(line, UIChatBar::BLUE);
	}

	void ChatReceivedHandler::handle(InPacket& recv) const
	{
		int32_t charid = recv.read_int();

		recv.read_bool(); // 'gm'

		std::string message = recv.read_string();
		int8_t type = recv.read_byte();

		if (auto character = Stage::get().get_character(charid))
		{
			message = character->get_name() + ": " + message;
			character->speak(message);
		}

		auto linetype = static_cast<UIChatBar::LineType>(type);

		if (auto chatbar = UI::get().get_element<UIChatBar>())
			chatbar->send_chatline(message, linetype);
	}

	void ScrollResultHandler::handle(InPacket& recv) const
	{
		int32_t cid = recv.read_int();
		bool success = recv.read_bool();
		bool destroyed = recv.read_bool();

		recv.read_short(); // Legendary spirit if 1

		CharEffect::Id effect;
		Messages::Type message;

		if (success)
		{
			effect = CharEffect::Id::SCROLL_SUCCESS;
			message = Messages::Type::SCROLL_SUCCESS;
		}
		else
		{
			effect = CharEffect::Id::SCROLL_FAILURE;

			if (destroyed)
				message = Messages::Type::SCROLL_DESTROYED;
			else
				message = Messages::Type::SCROLL_FAILURE;
		}

		Stage::get().show_character_effect(cid, effect);

		if (Stage::get().is_player(cid))
		{
			if (auto chatbar = UI::get().get_element<UIChatBar>())
				chatbar->display_message(message, UIChatBar::LineType::RED);

			UI::get().enable();
		}
	}

	void ShowItemGainInChatHandler::handle(InPacket& recv) const
	{
		int8_t mode1 = recv.read_byte();

		if (mode1 == 3)
		{
			int8_t mode2 = recv.read_byte();

			if (mode2 == 1) // This is actually 'item gain in chat'
			{
				int32_t itemid = recv.read_int();
				int32_t qty = recv.read_int();

				const ItemData& idata = ItemData::get(itemid);

				if (!idata.is_valid())
					return;

				std::string name = idata.get_name();
				std::string sign = (qty < 0) ? "-" : "+";
				std::string message = "Gained an item: " + name + " (" + sign + std::to_string(qty) + ")";

				if (auto chatbar = UI::get().get_element<UIChatBar>())
					chatbar->send_chatline(message, UIChatBar::LineType::BLUE);
			}
		}
		else if (mode1 == 13) // card effect
		{
			Stage::get().get_player().show_effect_id(CharEffect::Id::MONSTER_CARD);
		}
		else if (mode1 == 18) // intro effect
		{
			recv.read_string(); // path
		}
		else if (mode1 == 23) // info
		{
			recv.read_string();	// path
			recv.read_int();	// some int
		}
		else // Buff effect
		{
			int32_t skillid = recv.read_int();

			// More bytes, but we don't need them.
			Stage::get().get_combat().show_player_buff(skillid);
		}
	}
}
