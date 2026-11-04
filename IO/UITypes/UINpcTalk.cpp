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
#include "UINpcTalk.h"

#include "../UI.h"

#include "../Components/MapleButton.h"
#include "../Gameplay/Stage.h"

#include "../Net/Packets/NpcInteractionPackets.h"

#include "../../HeadlessState.h"

#include <nlnx/nx.hpp>
#include <charconv>
#include "../../Graphics/Geometry.h"
#include "../Components/AreaButton.h"

namespace ms
{
	UINpcTalk::UINpcTalk() : offset(0), unitrows(0), rowmax(0), show_slider(false), draw_text(false),
							 formatted_text(""), formatted_text_pos(0), timestep(0)
	{
		nl::node UIWindow2 = nl::nx::ui["UIWindow2.img"];
		nl::node UtilDlgEx = UIWindow2["UtilDlgEx"];

		top = UtilDlgEx["t"];
		fill = UtilDlgEx["c"];
		bottom = UtilDlgEx["s"];
		nametag = UtilDlgEx["bar"];

		min_height = 8 * fill.height() + 14;

		buttons[Buttons::ALLLEVEL] = std::make_unique<MapleButton>(UtilDlgEx["BtAllLevel"]);
		buttons[Buttons::CLOSE] = std::make_unique<MapleButton>(UtilDlgEx["BtClose"]);
		buttons[Buttons::MYLEVEL] = std::make_unique<MapleButton>(UtilDlgEx["BtMyLevel"]);
		buttons[Buttons::NEXT] = std::make_unique<MapleButton>(UtilDlgEx["BtNext"]);

		// TODO: Replace when _inlink is fixed
		//buttons[Buttons::NO] = std::make_unique<MapleButton>(UtilDlgEx["BtNo"]);

		nl::node Quest = UIWindow2["Quest"];

		buttons[Buttons::NO] = std::make_unique<MapleButton>(Quest["BtNo"]);
		buttons[Buttons::OK] = std::make_unique<MapleButton>(UtilDlgEx["BtOK"]);
		buttons[Buttons::PREV] = std::make_unique<MapleButton>(UtilDlgEx["BtPrev"]);
		buttons[Buttons::QAFTER] = std::make_unique<MapleButton>(UtilDlgEx["BtQAfter"]);
		buttons[Buttons::QCNO] = std::make_unique<MapleButton>(UtilDlgEx["BtQCNo"]);
		buttons[Buttons::QCYES] = std::make_unique<MapleButton>(UtilDlgEx["BtQCYes"]);
		buttons[Buttons::QGIVEUP] = std::make_unique<MapleButton>(UtilDlgEx["BtQGiveup"]);
		buttons[Buttons::QNO] = std::make_unique<MapleButton>(UtilDlgEx["BtQNo"]);
		buttons[Buttons::QSTART] = std::make_unique<MapleButton>(UtilDlgEx["BtQStart"]);
		buttons[Buttons::QYES] = std::make_unique<MapleButton>(UtilDlgEx["BtQYes"]);
		buttons[Buttons::YES] = std::make_unique<MapleButton>(UtilDlgEx["BtYes"]);

		name = Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::WHITE);

		onmoved = [&](bool upwards)
		{
			int16_t shift = upwards ? -unitrows : unitrows;
			bool above = offset + shift >= 0;
			bool below = offset + shift <= rowmax - unitrows;

			if (above && below)
				offset += shift;
		};

		UI::get().remove_textfield();
	}

	void UINpcTalk::draw(float inter) const
	{
		if (has_input()) {
            ColorBox(520, 320, Color::Name::BLACK, 0.96f).draw(position);
            text.draw(position + Point<int16_t>(20, 20), Range<int16_t>(position.y() + 20, position.y() + 198));
            input_hint.draw(position + Point<int16_t>(20, 208));
            ColorBox(475, 23, Color::Name::WHITE, 1).draw(position + Point<int16_t>(20, 235));
            input.draw(position);
            ColorBox(100, 24, Color::Name::WHITE, 1).draw(position + Point<int16_t>(20, 278));
            ColorBox(100, 24, Color::Name::WHITE, 1).draw(position + Point<int16_t>(395, 278));
            Text(Text::A11M, Text::LEFT, Color::Name::BLACK, "Cancel").draw(position + Point<int16_t>(25, 282));
            Text(Text::A11M, Text::LEFT, Color::Name::BLACK, "Submit").draw(position + Point<int16_t>(400, 282));
            return;
        }
        Point<int16_t> drawpos = position;
		top.draw(drawpos);
		drawpos.shift_y(top.height());
		fill.draw(DrawArgument(drawpos, Point<int16_t>(0, height)));
		drawpos.shift_y(height);
		bottom.draw(drawpos);
		drawpos.shift_y(bottom.height());

		UIElement::draw(inter);

		int16_t speaker_y = (top.height() + height + bottom.height()) / 2;
		Point<int16_t> speaker_pos = position + Point<int16_t>(22, 11 + speaker_y);
		Point<int16_t> center_pos = speaker_pos + Point<int16_t>(nametag.width() / 2, 0);

		speaker.draw(DrawArgument(center_pos, true));
		nametag.draw(speaker_pos);
		name.draw(center_pos + Point<int16_t>(0, -4));

		if (show_slider)
		{
			int16_t text_min_height = position.y() + top.height() - 1;
			text.draw(position + Point<int16_t>(162, 19 - offset * 400),
					  Range<int16_t>(text_min_height, text_min_height + height - 18));
			slider.draw(position);
		} else
		{
			int16_t y_adj = height - min_height;
			text.draw(position + Point<int16_t>(166, 48 - y_adj));
		}
    }

    void UINpcTalk::update()
	{
		UIElement::update();
        if (has_input()) input.update(position);

		if (draw_text)
		{
			if (timestep > 4)
			{
				if (formatted_text_pos < formatted_text.size())
				{
					std::string t = text.get_text();
					char c = formatted_text[formatted_text_pos];

					text.change_text(t + c);

					formatted_text_pos++;
					timestep = 0;
				} else
				{
					draw_text = false;
				}
			} else
			{
				timestep++;
			}
		}
	}

	Button::State UINpcTalk::button_pressed(uint16_t buttonid)
	{
        if (has_input()) {
            if (buttonid == Buttons::CLOSE) headless_respond(-1);
            else if (buttonid == Buttons::OK) submit_input(input.get_text());
            return Button::State::NORMAL;
        }
		deactivate();
		HeadlessState::clear_dialog();

		switch (type)
		{
			case TalkType::SENDNEXT:
			case TalkType::SENDOK:
				// Type = 0
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, -1).dispatch();
						break;
					case Buttons::NEXT:
					case Buttons::OK:
						NpcTalkMorePacket(type, 1).dispatch();
						break;
				}
				break;
			case TalkType::SENDNEXTPREV:
				// Type = 0
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, -1).dispatch();
						break;
					case Buttons::NEXT:
						NpcTalkMorePacket(type, 1).dispatch();
						break;
					case Buttons::PREV:
						NpcTalkMorePacket(type, 0).dispatch();
						break;
				}
				break;
			case TalkType::SENDYESNO:
				// Type = 1
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, -1).dispatch();
						break;
					case Buttons::NO:
						NpcTalkMorePacket(type, 0).dispatch();
						break;
					case Buttons::YES:
						NpcTalkMorePacket(type, 1).dispatch();
						break;
				}
				break;
			case TalkType::SENDACCEPTDECLINE:
				// Type = 1
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, -1).dispatch();
						break;
					case Buttons::QNO:
						NpcTalkMorePacket(type, 0).dispatch();
						break;
					case Buttons::QYES:
						NpcTalkMorePacket(type, 1).dispatch();
						break;
				}
				break;
			case TalkType::SENDGETTEXT:
				// TODO: What is this?
				break;
			case TalkType::SENDGETNUMBER:
				// Type = 3
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, 0).dispatch();
						break;
					case Buttons::OK:
						NpcTalkMorePacket(type, 1).dispatch();
						break;
				}
				break;
			case TalkType::SENDSIMPLE:
				// Type = 4
				switch (buttonid)
				{
					case Buttons::CLOSE:
						NpcTalkMorePacket(type, 0).dispatch();
						break;
					default:
						NpcTalkMorePacket(0).dispatch(); // TODO: Selection
						break;
				}
				break;
			default:
				break;
		}

		return Button::State::NORMAL;
	}

	Cursor::State UINpcTalk::send_cursor(bool clicked, Point<int16_t> cursorpos)
	{
		if (has_input()) {
            auto state = input.send_cursor(cursorpos, clicked);
            if (state != Cursor::State::IDLE) return state;
        }
        Point<int16_t> cursor_relative = cursorpos - position;

		if (show_slider && slider.isenabled())
			if (Cursor::State sstate = slider.send_cursor(cursor_relative, clicked))
				return sstate;

		Cursor::State estate = UIElement::send_cursor(clicked, cursorpos);

		if (estate == Cursor::State::CLICKING && clicked && draw_text)
		{
			draw_text = false;
			text.change_text(formatted_text);
		}

		return estate;
	}

	void UINpcTalk::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (pressed && escape)
		{
			deactivate();
			HeadlessState::clear_dialog();

			headless_respond(-1);
		}
	}

	bool UINpcTalk::headless_respond(int32_t value)
	{
        if (has_input()) {
            if (value != -1) return false;
            NpcTalkMorePacket(raw_type, int8_t(0)).dispatch();
            input.set_state(Textfield::DISABLED);
            deactivate(); HeadlessState::clear_dialog(); return true;
        }
		// Dispatch on the server's raw message type: the TalkType enum above is
		// numbered differently (its SENDSIMPLE is 7, the server's menu is 4).
		switch (raw_type)
		{
			case 0: // ok / next / prev
			case 1: // yes / no
			case 12: // accept / decline
				NpcTalkMorePacket(raw_type, static_cast<int8_t>(value)).dispatch();
				break;
			case 4: // selection menu
				if (value < 0)
					NpcTalkMorePacket(raw_type, 0).dispatch();
				else
					NpcTalkMorePacket(value).dispatch();
				break;
			default:
				return false;
		}

		deactivate();
		HeadlessState::clear_dialog();
		return true;
	}

    void UINpcTalk::configure_input(const std::string &initial, int32_t minimum,
                                    int32_t maximum, const std::vector<int32_t> &styles) {
        input_min = minimum; input_max = raw_type == 7 ? int32_t(styles.size()) - 1 : maximum; input_styles = styles;
        position = Point<int16_t>(140, 120); dimension = Point<int16_t>(520, 320);
        draw_text = false;
        std::string body = formatted_text;
        if (raw_type == 7) {
            body += "\nStyle index / asset ID (no preview):";
            for (size_t i = 0; i < styles.size() && i < 12; ++i)
                body += " " + std::to_string(i) + ":" + std::to_string(styles[i]);
            if (styles.size() > 12) body += " ...";
        }
        text = Text(Text::A12M, Text::LEFT, Color::Name::WHITE, body, 475);
        input = Textfield(Text::A12M, Text::LEFT, Color::Name::DARKGREY,
            Rectangle<int16_t>(Point<int16_t>(20, 235), Point<int16_t>(495, 258)), raw_type == 2 ? 255 : 11);
        input.set_horizontal_scroll(true);
        input.change_text(initial);
        input.set_enter_callback([this](std::string value) { submit_input(value); });
        input.set_state(Textfield::FOCUSED);
        input_hint = Text(Text::A11M, Text::LEFT, Color::Name::WHITE,
            raw_type == 2 ? "Text (Enter submits)" : raw_type == 3
            ? "Number: " + std::to_string(minimum) + " to " + std::to_string(maximum)
            : "Style index: 0 to " + std::to_string(int(styles.size()) - 1));
        buttons.clear();
        buttons[Buttons::OK] = std::make_unique<AreaButton>(Point<int16_t>(395, 278), Point<int16_t>(100, 24));
        buttons[Buttons::CLOSE] = std::make_unique<AreaButton>(Point<int16_t>(20, 278), Point<int16_t>(100, 24));
    }

    bool UINpcTalk::submit_input(const std::string &value) {
        if (!is_active() || !has_input()) return false;
        auto invalid = [this]() {
            input_hint.change_text(raw_type == 2 ? "Use at most 255 characters on one line."
                : "Enter a whole number from " + std::to_string(input_min) + " to " + std::to_string(input_max) + ".");
            return false;
        };
        if (raw_type == 2) {
            if (value.size() > 255 || value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) return invalid();
            NpcTalkMorePacket(value).dispatch();
        } else {
            int32_t number = 0;
            auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) return invalid();
            if (raw_type == 3 ? number < input_min || number > input_max
                             : number < 0 || size_t(number) >= input_styles.size()) return invalid();
            NpcTalkMorePacket(raw_type == 3 ? NpcTalkMorePacket::SelectionType::NUMBER : NpcTalkMorePacket::SelectionType::STYLE, number).dispatch();
        }
        input.set_state(Textfield::DISABLED);
        deactivate(); HeadlessState::clear_dialog(); return true;
    }

    void UINpcTalk::write_input_state(std::ostream &out) const {
        if (!has_input()) return;
        out << ",\"input\":{\"kind\":\"" << (raw_type == 2 ? "text" : raw_type == 3 ? "number" : "style")
            << "\",\"minimum\":" << input_min << ",\"maximum\":" << input_max
            << ",\"text_limit\":255,\"styles\":[";
        for (size_t i = 0; i < input_styles.size(); ++i) out << (i ? "," : "") << input_styles[i];
        out << "],\"field\":{\"x\":" << position.x() + 25 << ",\"y\":" << position.y() + 245
            << "},\"submit\":{\"x\":" << position.x() + 405 << ",\"y\":" << position.y() + 290 << "},\"cancel\":{\"x\":" << position.x() + 30 << ",\"y\":" << position.y() + 290 << "}}";
    }

	UIElement::Type UINpcTalk::get_type() const
	{
		return TYPE;
	}

	UINpcTalk::TalkType UINpcTalk::get_by_value(int8_t value)
	{
		if (value > TalkType::NONE && value < TalkType::LENGTH)
			return static_cast<TalkType>(value);

		return TalkType::NONE;
	}

	// TODO: Move this to GraphicsGL?
	std::string UINpcTalk::format_text(const std::string &tx, const int32_t &npcid)
	{
		std::string formatted_text = tx;
		size_t begin = formatted_text.find("#p");

		if (begin != std::string::npos)
		{
			size_t end = formatted_text.find("#", begin + 1);

			if (end != std::string::npos)
			{
				std::string namestr = nl::nx::string["Npc.img"][std::to_string(npcid)]["name"];
				formatted_text.replace(begin, end - begin, namestr);
			}
		}

		begin = formatted_text.find("#h");

		if (begin != std::string::npos)
		{
			size_t end = formatted_text.find("#", begin + 1);

			if (end != std::string::npos)
			{
				std::string charstr = Stage::get().get_player().get_name();
				formatted_text.replace(begin, end - begin, charstr);
			}
		}

		begin = formatted_text.find("#t");

		if (begin != std::string::npos)
		{
			size_t end = formatted_text.find("#", begin + 1);

			if (end != std::string::npos)
			{
				size_t b = begin + 2;
				int32_t itemid = std::stoi(formatted_text.substr(b, end - b));
				std::string itemname = nl::nx::string["Consume.img"][itemid]["name"];

				formatted_text.replace(begin, end - begin, itemname);
			}
		}

		return formatted_text;
	}

	void UINpcTalk::change_text(int32_t npcid, int8_t msgtype, int16_t, int8_t speakerbyte, const std::string &tx)
	{
		type = get_by_value(msgtype);
		raw_type = msgtype;

		timestep = 0;
		draw_text = true;
		formatted_text_pos = 0;
		formatted_text = format_text(tx, npcid);

		HeadlessState::set_dialog(npcid, msgtype, formatted_text);

		text = Text(Text::Font::A12M, Text::Alignment::LEFT, Color::Name::DARKGREY, formatted_text, 320);

		int16_t text_height = text.height();

		text.change_text("");

		if (speakerbyte == 0)
		{
			std::string strid = std::to_string(npcid);
			strid.insert(0, 7 - strid.size(), '0');
			strid.append(".img");

			speaker = nl::nx::npc[strid]["stand"]["0"];

			std::string namestr = nl::nx::string["Npc.img"][std::to_string(npcid)]["name"];
			name.change_text(namestr);
		} else
		{
			speaker = Texture();
			name.change_text("");
		}

		height = min_height;
		show_slider = false;

		if (text_height > height)
		{
			if (text_height > MAX_HEIGHT)
			{
				height = MAX_HEIGHT;
				show_slider = true;
				rowmax = text_height / 400 + 1;
				unitrows = 1;

				int16_t slider_y = top.height() - 7;
				slider = Slider(Slider::Type::DEFAULT_SILVER, Range<int16_t>(slider_y, slider_y + height - 20),
								top.width() - 26, unitrows, rowmax, onmoved);
			} else
			{
				height = text_height;
			}
		}

		for (auto &button : buttons)
		{
			button.second->set_active(false);
			button.second->set_state(Button::State::NORMAL);
		}

		int16_t y_cord = height + 48;

		buttons[Buttons::CLOSE]->set_position(Point<int16_t>(9, y_cord));
		buttons[Buttons::CLOSE]->set_active(true);

		switch (type)
		{
			case TalkType::SENDOK:
				buttons[Buttons::OK]->set_position(Point<int16_t>(471, y_cord));
				buttons[Buttons::OK]->set_active(true);
				break;
			case TalkType::SENDYESNO:
			{
				Point<int16_t> yes_position = Point<int16_t>(389, y_cord);

				buttons[Buttons::YES]->set_position(yes_position);
				buttons[Buttons::YES]->set_active(true);

				buttons[Buttons::NO]->set_position(yes_position + Point<int16_t>(65, 0));
				buttons[Buttons::NO]->set_active(true);
				break;
			}
			case TalkType::SENDNEXT:
			case TalkType::SENDNEXTPREV:
			case TalkType::SENDACCEPTDECLINE:
			case TalkType::SENDGETTEXT:
			case TalkType::SENDGETNUMBER:
			case TalkType::SENDSIMPLE:
			default:
				break;
		}

		position = Point<int16_t>(400 - top.width() / 2, 240 - height / 2);
		dimension = Point<int16_t>(top.width(), height + 120);
	}
}