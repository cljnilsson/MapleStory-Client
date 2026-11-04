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
#include "UINotice.h"
#include "../../Net/Handlers/InventoryHandlers.h"
#include "../../HeadlessState.h"

#include "../UI.h"

#include "../Components/MapleButton.h"
#include "../Audio/Audio.h"

#include <nlnx/nx.hpp>
#include <sstream>
#include "../Components/AreaButton.h"
#include "../../Graphics/Geometry.h"
#include "../../Gameplay/Stage.h"
#include "../../Net/Packets/InventoryPackets.h"

namespace ms
{
	UINotice::UINotice(std::string message, NoticeType t, Text::Alignment a)
			: UIDragElement<PosNOTICE>(), type(t), alignment(a)
	{
		nl::node src = nl::nx::ui["Basic.img"]["Notice6"];

		top = src["t"];
		center = src["c"];
		centerbox = src["c_box"];
		box = src["box"];
		box2 = src["box2"];
		bottom = src["s"];
		bottombox = src["s_box"];

		if (type == NoticeType::YESNO)
		{
			position.shift_y(-8);
			question = Text(Text::Font::A11M, alignment, Color::Name::WHITE, message, 200);
		} else if (type == NoticeType::ENTERNUMBER)
		{
			position.shift_y(-16);
			question = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE, message, 200);
		} else if (type == NoticeType::OK)
		{
			uint16_t maxwidth = top.width() - 6;

			position.shift_y(-8);
			question = Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::WHITE, message, maxwidth);
		}

		height = question.height();
		dimension = Point<int16_t>(top.width(), top.height() + height + bottom.height());
		position = Point<int16_t>(position.x() - dimension.x() / 2, position.y() - dimension.y() / 2);
		dragarea = Point<int16_t>(dimension.x(), 20);

		if (type != NoticeType::ENTERNUMBER)
			Sound(Sound::Name::DLGNOTICE).play();
	}

	UINotice::UINotice(std::string message, NoticeType t) : UINotice(message, t, Text::Alignment::CENTER)
	{}

	void UINotice::draw(bool textfield) const
	{
		Point<int16_t> start = position;

		top.draw(start);
		start.shift_y(top.height());

		if (textfield)
		{
			center.draw(start);
			start.shift_y(center.height());
			centerbox.draw(start);
			start.shift_y(centerbox.height() - 1);
			box2.draw(start);
			start.shift_y(box2.height());
			box.draw(DrawArgument(start, Point<int16_t>(0, 29)));
			start.shift_y(29);

			question.draw(position + Point<int16_t>(13, 13));
		} else
		{
			int16_t pos_y = height >= 32 ? height : 32;

			center.draw(DrawArgument(start, Point<int16_t>(0, pos_y)));
			start.shift_y(pos_y);
			centerbox.draw(start);
			start.shift_y(centerbox.height());
			box.draw(start);
			start.shift_y(box.height());

			if (type == NoticeType::YESNO && alignment == Text::Alignment::LEFT)
				question.draw(position + Point<int16_t>(31, 14));
			else
				question.draw(position + Point<int16_t>(131, 14));
		}

		bottombox.draw(start);
	}

	int16_t UINotice::box2offset(bool textfield) const
	{
		int16_t offset = top.height() + centerbox.height() + box.height() + height - (textfield ? 0 : 16);

		if (type == NoticeType::OK)
			if (height < 34)
				offset += 15;

		return offset;
	}

	UIYesNo::UIYesNo(std::string message, std::function<void(bool yes)> yh, Text::Alignment alignment) : UINotice(
			message, NoticeType::YESNO, alignment)
	{
		yesnohandler = yh;

		int16_t belowtext = box2offset(false);

		nl::node src = nl::nx::ui["Basic.img"];

		buttons[Buttons::YES] = std::make_unique<MapleButton>(src["BtOK4"], Point<int16_t>(156, belowtext));
		buttons[Buttons::NO] = std::make_unique<MapleButton>(src["BtCancel4"], Point<int16_t>(198, belowtext));
	}

	UIYesNo::UIYesNo(std::string message, std::function<void(bool yes)> yesnohandler) : UIYesNo(message, yesnohandler,
																								Text::Alignment::CENTER)
	{}

	void UIYesNo::draw(float alpha) const
	{
		UINotice::draw(false);
		UIElement::draw(alpha);
	}

	void UIYesNo::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (keycode == KeyAction::Id::RETURN)
		{
			yesnohandler(true);
			deactivate();
		} else if (escape)
		{
			yesnohandler(false);
			deactivate();
		}
	}

	UIElement::Type UIYesNo::get_type() const
	{
		return TYPE;
	}

	Button::State UIYesNo::button_pressed(uint16_t buttonid)
	{
		deactivate();

		switch (buttonid)
		{
			case Buttons::YES:
				yesnohandler(true);
				break;
			case Buttons::NO:
				yesnohandler(false);
				break;
		}

		return Button::State::PRESSED;
	}

	UIEnterNumber::UIEnterNumber(std::string message, std::function<void(int32_t)> nh, int32_t m, int32_t quantity)
			: UINotice(message, NoticeType::ENTERNUMBER)
	{
		numhandler = nh;
		max = m;

		int16_t belowtext = box2offset(true) - 21;
		int16_t pos_y = belowtext + 35;

		nl::node src = nl::nx::ui["Basic.img"];

		buttons[Buttons::OK] = std::make_unique<MapleButton>(src["BtOK4"], 156, pos_y);
		buttons[Buttons::CANCEL] = std::make_unique<MapleButton>(src["BtCancel4"], 198, pos_y);

		numfield = Textfield(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::LIGHTGREY,
							 Rectangle<int16_t>(24, 232, belowtext, belowtext + 20), 10);
		numfield.change_text(std::to_string(quantity));

		numfield.set_enter_callback(
				[&](std::string numstr)
				{
					handlestring(numstr);
				}
		);

		numfield.set_key_callback(
				KeyAction::Id::ESCAPE,
				[&]()
				{
					deactivate();
				}
		);

		numfield.set_state(Textfield::State::FOCUSED);
	}

	void UIEnterNumber::draw(float alpha) const
	{
		UINotice::draw(true);
		UIElement::draw(alpha);

		numfield.draw(position);
	}

	void UIEnterNumber::update()
	{
		UIElement::update();

		numfield.update(position);
	}

	Cursor::State UIEnterNumber::send_cursor(bool clicked, Point<int16_t> cursorpos)
	{
		if (numfield.get_state() == Textfield::State::NORMAL)
		{
			Cursor::State nstate = numfield.send_cursor(cursorpos, clicked);

			if (nstate != Cursor::State::IDLE)
				return nstate;
		}

		return UIElement::send_cursor(clicked, cursorpos);
	}

	void UIEnterNumber::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (keycode == KeyAction::Id::RETURN)
		{
			handlestring(numfield.get_text());
			deactivate();
		} else if (escape)
		{
			deactivate();
		}
	}

	UIElement::Type UIEnterNumber::get_type() const
	{
		return TYPE;
	}

	Button::State UIEnterNumber::button_pressed(uint16_t buttonid)
	{
		switch (buttonid)
		{
			case Buttons::OK:
				handlestring(numfield.get_text());
				break;
			case Buttons::CANCEL:
				deactivate();
				break;
		}

		return Button::State::NORMAL;
	}

	void UIEnterNumber::handlestring(std::string numstr)
	{
		int num = -1;
		bool has_only_digits = (numstr.find_first_not_of("0123456789") == std::string::npos);

		auto okhandler = [&](bool)
		{
			numfield.set_state(Textfield::State::FOCUSED);
			buttons[Buttons::OK]->set_state(Button::State::NORMAL);
		};

		if (!has_only_digits)
		{
			numfield.set_state(Textfield::State::DISABLED);
			UI::get().emplace<UIOk>("Only numbers are allowed.", okhandler);
			return;
		} else
		{
			num = std::stoi(numstr);
		}

		if (num < 1)
		{
			numfield.set_state(Textfield::State::DISABLED);
			UI::get().emplace<UIOk>("You may only enter a number equal to or higher than 1.", okhandler);
			return;
		} else if (num > max)
		{
			numfield.set_state(Textfield::State::DISABLED);
			UI::get().emplace<UIOk>("You may only enter a number equal to or lower than " + std::to_string(max) + ".",
									okhandler);
			return;
		} else
		{
			numhandler(num);
			deactivate();
		}

		buttons[Buttons::OK]->set_state(Button::State::NORMAL);
	}

	bool UICashItemDialog::supports(int32_t item)
	{
		return item / 10000 == 507 || item / 10000 == 508 || item / 10000 == 512 || item / 10000 == 539 || item / 10000 == 505 || item == 5060000 || item == 5060001
			|| (item >= 5061000 && item <= 5061003) || item / 10000 == 552 || item / 10000 == 557 || item == 5610000 || item == 5610001 || item == 5230000 || item == 5431000 || item == 5432000;
	}

	UICashItemDialog::UICashItemDialog(int16_t s, int32_t i)
		: UINotice(i == 5050000
			? "AP Reset: enter destination then source.\\nChoose str dex int luk hp mp."
			: "SP Reset: enter destination skill ID then source skill ID.", NoticeType::ENTERNUMBER), slot(s), item(i)
	{
		// The v83 asset set may lack Notice6. This panel is also the fallback skin.
		position = Point<int16_t>(240, 190);
		dimension = Point<int16_t>(320, 180);
		dragarea = Point<int16_t>(320, 24);
		prompt = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE,
			i == 5050000 ? "AP Reset: destination then source\\nstr dex int luk hp mp"
				: "SP Reset: destination skill ID\\nthen source skill ID", 290);
		if (i / 10000 != 505)
		{
			std::string title = i == 5060000 ? "Item Tag" : i / 10000 == 552 ? "Scissors of Karma"
				: i / 10000 == 557 ? "Vicious' Hammer" : "Sealing Lock";
			prompt = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE,
				title + "\\nEnter equip or equipped, then target slot.", 290);
		}
		if (i == 5610000 || i == 5610001)
			prompt = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE,
				"Vega's Spell: equip slot then scroll slot\\nBoth must be in your bag (slots 1-127).", 290);
		if (i == 5230000)
			prompt = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE, "Owl of Minerva: enter searched item ID.", 290);
		if (i / 10000 == 543)
			prompt = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE,
				"Maple Life: name face hair color skin\\ngender job(0-4) improveSP(0-10)", 290);
		if (i / 10000 == 507 || i / 10000 == 508 || i / 10000 == 512 || i / 10000 == 539)
		{
			std::string help = "Enter message. Separate lines with |.";
			if (i / 10000 == 539) help = "Avatar: ear(0/1) then four lines.\\nSeparate lines with |.";
			else if (i / 1000 == 5075) help = "Maple TV: ear(0/1) partner(- for none)\\nthen five lines separated with |.";
			else if (i / 1000 == 5076) help = "Item mega: ear type slot then message\\ntype0=no item, 1=Equip,2=Use,3=Setup,4=Etc,5=Cash";
			else if (i / 10000 == 507 && i / 1000 != 5071) help = "Mega: ear(0/1) then message\\nTriple mega: up to three lines with |.";
			prompt = Text(Text::A12M, Text::LEFT, Color::Name::WHITE, help, 290);
		}
		confirm_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Confirm");
		cancel_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Cancel");
		values = Textfield(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::LIGHTGREY,
			Rectangle<int16_t>(20, 300, 65, 88), 240);
		values.set_enter_callback([this](std::string) { submit(); });
		values.set_key_callback(KeyAction::Id::ESCAPE, [this]() { values.set_state(Textfield::State::DISABLED); deactivate(); });
		values.set_state(Textfield::State::FOCUSED);
		buttons[0] = std::make_unique<AreaButton>(Point<int16_t>(130, 145), Point<int16_t>(80, 25));
		buttons[1] = std::make_unique<AreaButton>(Point<int16_t>(220, 145), Point<int16_t>(80, 25));
	}

	std::string UICashItemDialog::request(int16_t slot, int32_t item, const std::string &text)
	{
		auto &player = Stage::get().get_player();
		if (slot < 1 || player.get_inventory().get_item_id(InventoryType::CASH, slot) != item)
			return "Cash item moved or missing.";
		if (player.get_stats().get_stat(MapleStat::HP) == 0) return "Player must be alive.";
		if (!supports(item)) return "Unsupported cash item.";
		if (VegaSpellState::get().pending && VegaSpellState::get().elapsed_ms() < 10000)
			return "Wait for the pending Vega result.";
		int family = item / 10000;
		if (family == 507 || family == 508 || family == 512 || family == 539)
		{
			int kind = (item / 1000) % 10, ear = 0, type = 0, target = 0; std::string partner, message;
			std::istringstream initial(text); std::getline(initial >> std::ws, message);
			if (family == 507 && kind != 1 && kind != 2 && kind != 5 && kind != 6 && kind != 7) return "Unsupported megaphone family.";
			if (family == 507 && kind == 5 && item % 10 > 5) return "Unsupported TV type.";
			if (family == 539 || (family == 507 && kind != 1))
			{
				std::istringstream input(text);
				if (!(input >> ear) || ear < 0 || ear > 1) return "Enter ear 0 or 1 before message.";
				if (kind == 5 && family == 507 && !(input >> partner)) return "Enter partner name, or - for none.";
				if (kind == 6 && family == 507)
				{
					if (!(input >> type >> target) || type < 0 || type > 5) return "Enter inventory type (0-5) and item slot.";
					if ((type && (target < 1 || target > 127)) || (!type && target != 0)) return "Use bag slot 1-127, or 0 with no linked item.";
					if (type && player.get_inventory().get_item_id(static_cast<InventoryType::Id>(type), target) == 0) return "Linked item missing.";
				}
				std::getline(input >> std::ws, message);
			}
			std::vector<std::string> lines; std::istringstream split(message); std::string line;
			while (std::getline(split, line, '|')) lines.push_back(line);
			int count = family == 539 ? 4 : family == 507 && kind == 5 ? 5 : family == 507 && kind == 7 ? static_cast<int>(lines.size()) : 1;
			if (lines.empty() || (int)lines.size() != count || count > 5 || (family == 507 && kind == 7 && count > 3)) return "Wrong number of message lines.";
			for (const auto &value : lines) if (value.empty() || value.size() > 80) return "Use 1-80 characters per line.";
			CashBroadcastPacket(slot, item, lines, ear != 0, type, target, partner).dispatch(); return "";
		}
		if (item == 5230000)
		{
			std::istringstream input(text); int32_t searched; std::string extra;
			if (!(input >> searched) || input >> extra || searched < 1000000 || searched > 5999999)
				return "Enter a valid searched item ID.";
			OwlSearchState::get() = OwlSearchState{};
			OwlSearchPacket(slot, item, searched).dispatch(); return "";
		}
		if (item == 5431000 || item == 5432000)
		{
			std::istringstream input(text); std::string name, extra; std::vector<int32_t> values(7);
			if (!(input >> name)) return "Enter name and seven character values.";
			for (auto &value : values) if (!(input >> value)) return "Enter name and seven character values.";
			if (input >> extra || name.size() < 4 || name.size() > 12 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos)
				return "Use a 4-12 letter/digit name.";
			const auto face = values[0], hair = values[1], color = values[2], skin = values[3], gender = values[4], job = values[5], sp = values[6];
			if (face < 20000 || face > 29999 || hair < 30000 || hair > 39999 || color < 0 || color > 7
				|| skin < 0 || skin > 4 || gender < 0 || gender > 1 || job < 0 || job > 4 || sp < 0 || sp > 10)
				return "Character values are outside supported ranges.";
			if (!nl::nx::character["Face"]["000" + std::to_string(face) + ".img"]
				|| !nl::nx::character["Hair"]["000" + std::to_string(hair + color) + ".img"])
				return "Face or hair asset is unavailable.";
			auto &state = MapleLifeState::get(); state = MapleLifeState{}; state.name = name;
			MapleLifePacket(slot, item, name, values).dispatch(); return "";
		}
		if (item == 5610000 || item == 5610001)
		{
			std::istringstream input(text); int32_t equipment, scroll; std::string extra;
			if (!(input >> equipment >> scroll) || input >> extra || equipment < 1 || equipment > 127 || scroll < 1 || scroll > 127)
				return "Enter equip slot then scroll slot (1-127).";
			auto equip = player.get_inventory().get_equip(InventoryType::EQUIP, equipment);
			int32_t scroll_id = player.get_inventory().get_item_id(InventoryType::USE, scroll);
			if (!equip || equip->get_slots() < 1) return "Equipment needs an upgrade slot.";
			if (scroll_id / 10000 != 204) return "A scroll in the Use inventory is required.";
			auto info = nl::nx::item["Consume"]["0204.img"]["0" + std::to_string(scroll_id)]["info"];
			int success = info["success"];
			if (success != (item == 5610000 ? 10 : 60)) return "Scroll chance does not match this Vega spell.";
			if ((scroll_id / 100) % 100 != (equip->get_item_id() / 10000) % 100)
				return "Scroll does not match the equipment type.";
			auto &state = VegaSpellState::get(); state = VegaSpellState{};
			state.pending = true; state.item = item; state.cash_slot = slot;
			state.equip_slot = equipment; state.equip_id = equip->get_item_id(); state.scroll_slot = scroll;
			state.requested = std::chrono::steady_clock::now();
			VegaSpellPacket(slot, item, equipment, scroll).dispatch();
			return "";
		}
		if (item / 10000 != 505)
		{
			std::istringstream input(text);
			std::string tab, extra; int32_t target;
			if (!(input >> tab >> target) || input >> extra || target == 0 || target < -32768 || target > 32767)
				return "Enter equip/equipped and target slot.";
			auto type = tab == "equip" ? InventoryType::EQUIP : tab == "equipped" ? InventoryType::EQUIPPED : InventoryType::NONE;
			if (type == InventoryType::NONE || (type == InventoryType::EQUIP ? target < 1 : target > -1))
				return "Use positive equip or negative equipped slot.";
			auto equip = player.get_inventory().get_equip(type, type == InventoryType::EQUIPPED ? -target : target);
			if (!equip) return "Target equipment missing.";
			if (item == 5060000 && type != InventoryType::EQUIPPED) return "Tag requires equipped target.";
			if (item / 10000 == 557 && (type != InventoryType::EQUIP || equip->get_vicious() >= 2))
				return "Hammer requires bag equipment below limit two.";
			if (item / 10000 == 506 && item != 5060000 && (equip->get_flags() & 1))
				return "Equipment is already sealed.";
			if (item / 10000 == 552 && (equip->get_flags() & 0x10)) return "Karma is already applied.";
			CashEquipmentPacket(slot, item, type == InventoryType::EQUIPPED ? -1 : 1, target).dispatch();
			return "";
		}
		std::istringstream input(text);
		std::string to, from, extra;
		if (!(input >> to >> from) || input >> extra) return "Enter destination then source.";
		int32_t dest = 0, source = 0;
		if (item == 5050000)
		{
			auto stat = [](const std::string &s) -> int32_t {
				if (s == "str") return 0x40;
				if (s == "dex") return 0x80;
				if (s == "int") return 0x100;
				if (s == "luk") return 0x200;
				if (s == "hp") return 0x800;
				if (s == "mp") return 0x2000;
				return 0;
			};
			dest = stat(to); source = stat(from);
		}
		else if (item > 5050000 && item / 10000 == 505)
		{
			auto skill = [](const std::string &s, int32_t &n) {
				std::istringstream input(s); char tail;
				return bool(input >> n) && !(input >> tail) && n > 0;
			};
			if (!skill(to, dest) || !skill(from, source)) return "Enter positive skill IDs.";
			// The server consumes a reset with an unknown source: refuse that locally.
			if (player.get_skilllevel(source) <= 0) return "Source skill has no learned levels.";
			std::string skillid = std::to_string(dest);
			if (skillid.size() < 7) skillid.insert(0, 7 - skillid.size(), '0');
			if (!nl::nx::skill[std::to_string(dest / 10000) + ".img"]["skill"][skillid])
				return "Destination skill is unavailable.";
		}
		if (!dest || !source || dest == source) return "Choose two different valid targets.";
		CashResetPacket(slot, item, dest, source).dispatch();
		return "";
	}

	void UICashItemDialog::submit()
	{
		auto message = request(slot, item, values.get_text());
		last_error = message;
		if (message.empty()) { values.set_state(Textfield::State::DISABLED); deactivate(); }
		else error = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::WHITE, message, 230);
	}

	void UICashItemDialog::draw(float alpha) const
	{
		ColorBox(320, 180, Color::Name::WHITE, 1.0f).draw(position);
		ColorBox(318, 178, Color::Name::BLACK, 0.95f).draw(position + Point<int16_t>(1, 1));
		ColorBox(282, 25, Color::Name::WHITE, 1.0f).draw(position + Point<int16_t>(19, 64));
		ColorBox(280, 23, Color::Name::BLACK, 1.0f).draw(position + Point<int16_t>(20, 65));
		ColorBox(80, 25, Color::Name::WHITE, 1.0f).draw(position + Point<int16_t>(130, 145));
		ColorBox(80, 25, Color::Name::WHITE, 1.0f).draw(position + Point<int16_t>(220, 145));
		prompt.draw(position + Point<int16_t>(15, 12));
		confirm_label.draw(position + Point<int16_t>(140, 150));
		cancel_label.draw(position + Point<int16_t>(230, 150));
		UIElement::draw(alpha); values.draw(position);
		error.draw(position + Point<int16_t>(15, 100));
	}
	void UICashItemDialog::update() { UIElement::update(); values.update(position); }
	Cursor::State UICashItemDialog::send_cursor(bool pressed, Point<int16_t> p)
	{
		auto state = values.send_cursor(p, pressed);
		if (state != Cursor::State::IDLE) return state;
		// New dialogs must accept a click even when the pointer has not moved.
		if (pressed) UIElement::send_cursor(false, p);
		return UIElement::send_cursor(pressed, p);
	}
	void UICashItemDialog::send_key(int32_t key, bool pressed, bool escape)
	{
		if (!pressed) return;
		if (escape) { values.set_state(Textfield::State::DISABLED); deactivate(); }
		else if (key == KeyAction::Id::RETURN) submit();
	}
	Button::State UICashItemDialog::button_pressed(uint16_t button)
	{
		if (button == 0) submit();
		else { values.set_state(Textfield::State::DISABLED); deactivate(); }
		return Button::State::NORMAL;
	}

	UICashResult::UICashResult(std::string t, std::vector<std::string> r, std::string k)
		: UINotice(t, NoticeType::OK), title(std::move(t)), kind(std::move(k)), rows(std::move(r))
	{
		position = Point<int16_t>(200, 100); dimension = Point<int16_t>(400, 380); dragarea = Point<int16_t>(400, 24);
		buttons.clear();
		for (int i = 0; i < 3; ++i) buttons[i] = std::make_unique<AreaButton>(Point<int16_t>(20 + 125 * i, 340), Point<int16_t>(100, 25));
		close_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Close");
		prev_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Previous");
		next_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Next");
		refresh();
	}
	void UICashResult::refresh()
	{
		std::string text = title + " (" + std::to_string(page + 1) + "/" + std::to_string(std::max(size_t(1), (rows.size() + 3) / 4)) + ")\\n\\n";
		for (size_t i = page * 4; i < rows.size() && i < page * 4 + 4; ++i) text += rows[i] + "\\n\\n";
		body = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE, text, 370);
	}
	void UICashResult::draw(float) const
	{
		ColorBox(400, 380, Color::Name::WHITE, 1.0f).draw(position);
		ColorBox(398, 378, Color::Name::BLACK, 0.95f).draw(position + Point<int16_t>(1, 1));
		body.draw(position + Point<int16_t>(15, 12));
		for (int i = 0; i < 3; ++i) ColorBox(100, 25, Color::Name::WHITE, 1.0f).draw(position + Point<int16_t>(20 + 125 * i, 340));
		close_label.draw(position + Point<int16_t>(30, 345)); prev_label.draw(position + Point<int16_t>(155, 345)); next_label.draw(position + Point<int16_t>(280, 345));
		if (kind == "owl") ++OwlSearchState::get().draw_calls;
		else ++MapleLifeState::get().draw_calls;
	}
	Cursor::State UICashResult::send_cursor(bool pressed, Point<int16_t> cursor)
	{
		if (pressed) UIElement::send_cursor(false, cursor);
		return UIElement::send_cursor(pressed, cursor);
	}
	void UICashResult::send_key(int32_t key, bool pressed, bool escape)
	{
		if (!pressed) return;
		if (escape) deactivate();
		else if (key == KeyAction::RETURN) button_pressed(2);
	}
	Button::State UICashResult::button_pressed(uint16_t id)
	{
		if (id == 0) deactivate();
		else if (id == 1 && page > 0) { --page; refresh(); }
		else if (id == 2 && (page + 1) * 4 < rows.size()) { ++page; refresh(); }
		return Button::State::NORMAL;
	}

	UIVegaResult::UIVegaResult() : UINotice("Vega's Spell", NoticeType::OK)
	{
		position = Point<int16_t>(240, 160); dimension = Point<int16_t>(320, 250);
		dragarea = Point<int16_t>(320, 24);
		buttons.clear();
		buttons[0] = std::make_unique<AreaButton>(Point<int16_t>(220, 215), Point<int16_t>(80, 25));
		close_label = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::BLACK, "Close");
		update();
	}
	void UIVegaResult::update()
	{
		auto &state = VegaSpellState::get();
		int next = state.model_updated ? state.code : (state.code == 0x42 || state.code == 0x44) ? state.code : 0;
		if (phase != next)
		{
			phase = next;
			const char *path = phase == 0x41 ? "EffectSuccess" : phase == 0x43 ? "EffectFail" : "EffectSpelling";
			animation = Animation(nl::nx::ui["UIWindow.img"]["VegaSpell"][path]);
			state.asset = animation.get_dimensions().x() > 0;
			std::string message = phase == 0x41 ? "Vega succeeded. Equipment updated."
				: phase == 0x43 ? "Vega failed. Equipment updated."
				: phase ? "This item cannot be used." : "Vega's Spell: waiting for equipment update...";
			status = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::WHITE, message, 290);
		}
		animation.update(); UIElement::update();
	}
	void UIVegaResult::draw(float alpha) const
	{
		ColorBox(320, 250, Color::Name::WHITE, 1.0f).draw(position);
		ColorBox(318, 248, Color::Name::BLACK, 0.95f).draw(position + Point<int16_t>(1, 1));
		status.draw(position + Point<int16_t>(15, 12));
		animation.draw(position + Point<int16_t>(160, 125), alpha);
		ColorBox(80, 25, Color::Name::WHITE, 1.0f).draw(position + Point<int16_t>(220, 215));
		close_label.draw(position + Point<int16_t>(230, 220));
		auto &state = VegaSpellState::get(); ++state.draw_calls;
		if (drawn_phase != phase)
		{
			drawn_phase = phase;
			HeadlessState::presentation("draw", "vega", phase);
			HeadlessState::event("vega_draw", ",\"phase\":" + std::to_string(phase)
				+ ",\"asset\":" + (state.asset ? "true" : "false"));
		}
	}
	void UIVegaResult::send_key(int32_t, bool pressed, bool escape) { if (pressed && escape) deactivate(); }
	Button::State UIVegaResult::button_pressed(uint16_t) { deactivate(); return Button::State::NORMAL; }

	UIOk::UIOk(std::string message, std::function<void(bool ok)> oh) : UINotice(message, NoticeType::OK)
	{
		okhandler = oh;

		nl::node src = nl::nx::ui["Basic.img"];

		buttons[Buttons::OK] = std::make_unique<MapleButton>(src["BtOK4"], 197, box2offset(false));
	}

	void UIOk::draw(float alpha) const
	{
		UINotice::draw(false);
		UIElement::draw(alpha);
	}

	void UIOk::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (pressed)
		{
			if (keycode == KeyAction::Id::RETURN)
			{
				okhandler(true);
				deactivate();
			} else if (escape)
			{
				okhandler(false);
				deactivate();
			}
		}
	}

	UIElement::Type UIOk::get_type() const
	{
		return TYPE;
	}

	Button::State UIOk::button_pressed(uint16_t buttonid)
	{
		deactivate();

		switch (buttonid)
		{
			case Buttons::OK:
				okhandler(true);
				break;
		}

		return Button::State::NORMAL;
	}
}