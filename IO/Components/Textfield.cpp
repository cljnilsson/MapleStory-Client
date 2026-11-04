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
#include "Textfield.h"

#include "../UI.h"

#include <sstream>

namespace ms
{
	Textfield::Textfield()
	{
		text = "";
	}

	Textfield::Textfield(Text::Font font, Text::Alignment alignment, Color::Name color, Rectangle<int16_t> bounds, size_t limit) : bounds(bounds), limit(limit)
	{
		textlabel = Text(font, alignment, color, "", 0, false);
		marker = Text(font, alignment, color, "|");

		text = "";
		markerpos = 0;
		crypt = 0;
		state = State::NORMAL;
	}

	void Textfield::draw(Point<int16_t> posiiton) const
	{
		if (state == State::DISABLED)
			return;

		Point<int16_t> absp = bounds.get_left_top() + posiiton;

		const Text *label = &textlabel;
		size_t marker_index = markerpos;
		if (horizontal_scroll)
		{
			if (visible_source != text || visible_marker != markerpos)
			{
				visible_source = text; visible_marker = markerpos;
				const auto width = std::max<int16_t>(1, bounds.get_horizontal().length() - 10);
				visible_start = 0;
				while (visible_start < markerpos && textlabel.advance(markerpos) - textlabel.advance(visible_start) > width)
					++visible_start;
				size_t end = visible_start;
				while (end < text.size() && textlabel.advance(end + 1) - textlabel.advance(visible_start) <= width) ++end;
				visible_label = textlabel;
				visible_label.change_text(crypt > 0 ? std::string(end - visible_start, crypt) : text.substr(visible_start, end - visible_start));
			}
			label = &visible_label;
			marker_index -= visible_start;
		}
		if (!text.empty()) label->draw(absp);
		if (state == State::FOCUSED && showmarker)
			marker.draw(absp + Point<int16_t>(label->advance(marker_index), -1));

	}

	void Textfield::set_horizontal_scroll(bool enabled)
	{
		horizontal_scroll = enabled;
		textlabel.change_maxwidth(enabled ? 32767 : 0);
		visible_marker = static_cast<size_t>(-1);
	}

	void Textfield::update(Point<int16_t> parent)
	{
		if (state == State::DISABLED)
			return;

		parentpos = parent;
		elapsed += Constants::TIMESTEP;

		if (elapsed > 256)
		{
			showmarker = !showmarker;
			elapsed = 0;
		}
	}

	void Textfield::set_state(State st)
	{
		if (state != st)
		{
			state = st;

			if (state != State::DISABLED)
			{
				elapsed = 0;
				showmarker = true;
			}
			else
			{
				UI::get().remove_textfield();
			}

			if (state == State::FOCUSED)
				UI::get().focus_textfield(this);
		}
	}

	void Textfield::set_enter_callback(std::function<void(std::string)> onr)
	{
		onreturn = onr;
	}

	void Textfield::set_key_callback(KeyAction::Id key, std::function<void(void)> action)
	{
		callbacks[key] = action;
	}

	void Textfield::send_key(KeyType::Id type, int32_t key, bool pressed)
	{
		if (pressed)
		{
			if (type == KeyType::Id::ACTION)
			{
				switch (key)
				{
				case KeyAction::Id::LEFT:
					if (markerpos > 0)
						markerpos--;

					break;
				case KeyAction::Id::RIGHT:
					if (markerpos < text.size())
						markerpos++;

					break;
				case KeyAction::Id::BACK:
					if (text.size() > 0 && markerpos > 0)
					{
						text.erase(markerpos - 1, 1);

						markerpos--;

						modifytext(text);
					}

					break;
				case KeyAction::Id::RETURN:
					if (onreturn)
						onreturn(text);

					break;
				case KeyAction::Id::SPACE:
					add_string(" ");
					break;
				case KeyAction::Id::HOME:
					markerpos = 0;
					break;
				case KeyAction::Id::END:
					markerpos = text.size();
					break;
				case KeyAction::Id::DELETE:
					if (text.size() > 0 && markerpos < text.size())
					{
						text.erase(markerpos, 1);

						modifytext(text);
					}

					break;
				default:
					if (callbacks.count(key))
						callbacks.at(key)();

					break;
				}
			}
			else if (type == KeyType::Id::TEXT)
			{
				std::stringstream ss;
				char a = static_cast<int8_t>(key);

				ss << a;

				add_string(ss.str());
			}
		}
	}

	void Textfield::add_string(const std::string& str)
	{
		for (char c : str)
		{
			if (belowlimit())
			{
				text.insert(markerpos, 1, c);

				markerpos++;

				modifytext(text);
			}
		}
	}

	void Textfield::modifytext(const std::string& t)
	{
		if (crypt > 0)
		{
			std::string crypted;
			crypted.insert(0, t.size(), crypt);

			textlabel.change_text(crypted);
		}
		else
		{
			textlabel.change_text(t);
		}

		text = t;
	}

	Cursor::State Textfield::send_cursor(Point<int16_t> cursorpos, bool clicked)
	{
		if (state == State::DISABLED)
			return Cursor::State::IDLE;

		if (get_bounds().contains(cursorpos))
		{
			if (clicked)
			{
				if (state == State::NORMAL)
					set_state(State::FOCUSED);

				return Cursor::State::CLICKING;
			}
			else
			{
				return Cursor::State::CANCLICK;
			}
		}
		else
		{
			if (clicked && state == State::FOCUSED)
				set_state(State::NORMAL);

			return Cursor::State::IDLE;
		}
	}

	void Textfield::change_text(const std::string& t)
	{
		modifytext(t);

		markerpos = text.size();
	}

	void Textfield::set_cryptchar(int8_t character)
	{
		crypt = character;
	}

	bool Textfield::belowlimit() const
	{
		if (limit > 0)
		{
			return text.size() < limit;
		}
		else
		{
			uint16_t advance = textlabel.advance(text.size());

			return (advance + 50) < bounds.get_horizontal().length();
		}
	}

	const std::string& Textfield::get_text() const
	{
		return text;
	}

	bool Textfield::empty() const
	{
		return text.empty();
	}

	Textfield::State Textfield::get_state() const
	{
		return state;
	}

	Rectangle<int16_t> Textfield::get_bounds() const
	{
		return Rectangle<int16_t>(
			bounds.get_left_top() + parentpos,
			bounds.get_right_bottom() + parentpos
			);
	}
}