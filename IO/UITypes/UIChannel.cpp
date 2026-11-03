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
#include "V83UIWindowCompat.h"
#include "UIChannel.h"
#include "../../Configuration.h"
#include "../../Net/Session.h"
#include "../../Net/Packets/PlayerPackets.h"
#include "../../Net/Handlers/Helpers/LoginParser.h"

#include <algorithm>

#include "../KeyAction.h"

#include "../Components/MapleButton.h"
#include "../Components/AreaButton.h"
#include "../Audio/Audio.h"

#include <nlnx/nx.hpp>

namespace ms
{
	int UIChannel::pending_channel = -1;

	UIChannel::UIChannel() : UIDragElement<PosCHANNEL>()
	{
		uint8_t selected_world = Configuration::get().get_worldid();
		current_channel = Configuration::get().get_channelid();
		selected_channel = current_channel;
		channel_count = std::min<uint8_t>(20, LoginParser::channel_count(selected_world));
		current_channel_x = selected_channel_x = 11;
		current_channel_y = selected_channel_y = 55;

		nl::node Channel = v83ui::window("Channel");

		nl::node backgrnd = Channel["backgrnd"];
		Texture bg = backgrnd;

		sprites.emplace_back(backgrnd, Point<int16_t>(1, 0));
		sprites.emplace_back(Channel["backgrnd2"]);
		sprites.emplace_back(Channel["backgrnd3"]);
		sprites.emplace_back(Channel["world"][selected_world], Point<int16_t>(16, 30));

		buttons[Buttons::CANCEL] = std::make_unique<MapleButton>(Channel["BtCancel"]);
		buttons[Buttons::CHANGE] = std::make_unique<MapleButton>(Channel["BtChange"], Point<int16_t>(-20, 0));

		channel[true] = Channel["channel1"];
		channel[false] = Channel["channel0"];

		size_t x = 0;
		size_t y = 0;

		for (size_t i = 0; i < channel_count; i++)
		{
			if (x >= 5)
			{
				x = 0;
				y++;
			}

			ch.emplace_back(Channel["ch"][i], Point<int16_t>(19 + 70 * x, 60 + 20 * y));
			buttons[Buttons::CH + i] = std::make_unique<AreaButton>(Point<int16_t>(11 + 70 * x, 55 + 20 * y),
																	channel[true].get_dimensions());

			if (i == selected_channel)
			{
				current_channel_x = 11 + 70 * x;
				current_channel_y = 55 + 20 * y;
				selected_channel_x = current_channel_x;
				selected_channel_y = current_channel_y;
			}

			x++;
		}

		dimension = bg.get_dimensions();
		dragarea = Point<int16_t>(dimension.x(), 20);
	}

	void UIChannel::draw(float inter) const
	{
		UIElement::draw(inter);

		if (current_channel == selected_channel)
		{
			channel[true].draw(DrawArgument(position.x() + selected_channel_x, position.y() + selected_channel_y));
		} else
		{
			channel[true].draw(DrawArgument(position.x() + selected_channel_x, position.y() + selected_channel_y));
			channel[false].draw(DrawArgument(position.x() + current_channel_x, position.y() + current_channel_y));
		}


		for (auto &sprite : ch)
			sprite.draw(position, inter);
	}

	void UIChannel::update()
	{
		UIElement::update();

		for (auto &sprite : ch)
			sprite.update();
	}

	void UIChannel::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (pressed)
		{
			if (escape)
			{
				cancel();
			} else if (keycode == KeyAction::Id::RETURN)
			{
				change_channel();
			} else if (channel_count > 1)
			{
				int step = keycode == KeyAction::Id::UP ? -5 :
					keycode == KeyAction::Id::DOWN ? 5 :
					keycode == KeyAction::Id::LEFT ? -1 :
					keycode == KeyAction::Id::RIGHT ? 1 : 0;
				if (step == 0)
					return;
				int target = (selected_channel + step + channel_count * 5) % channel_count;
				if (target == current_channel)
					target = (target + (step < 0 ? -1 : 1) + channel_count) % channel_count;
				selected_channel = target;
				update_selected_channel_position();
			}
		}
	}

	Cursor::State UIChannel::send_cursor(bool clicked, Point<int16_t> cursorpos)
	{
		Cursor::State dstate = UIDragElement::send_cursor(clicked, cursorpos);

		if (dragged)
			return dstate;

		Cursor::State ret = clicked ? Cursor::State::CLICKING : Cursor::State::IDLE;

		for (size_t i = 0; i < channel_count + Buttons::CH; i++)
		{
			if (buttons[i]->is_active() && buttons[i]->bounds(position).contains(cursorpos))
			{
				if (buttons[i]->get_state() == Button::State::NORMAL)
				{
					if (i < Buttons::CH)
					{
						Sound(Sound::Name::BUTTONOVER).play();

						buttons[i]->set_state(Button::State::MOUSEOVER);
						ret = Cursor::State::CANCLICK;
					} else
					{
						buttons[i]->set_state(Button::State::MOUSEOVER);
						ret = Cursor::State::IDLE;
					}
				} else if (buttons[i]->get_state() == Button::State::MOUSEOVER)
				{
					if (clicked)
					{
						if (i < Buttons::CH)
							Sound(Sound::Name::BUTTONCLICK).play();

						buttons[i]->set_state(button_pressed(i));

						ret = Cursor::State::IDLE;
					} else
					{
						if (i < Buttons::CH)
							ret = Cursor::State::CANCLICK;
						else
							ret = Cursor::State::IDLE;
					}
				}
			} else if (buttons[i]->get_state() == Button::State::MOUSEOVER)
			{
				buttons[i]->set_state(Button::State::NORMAL);
			}
		}

		return ret;
	}

	UIElement::Type UIChannel::get_type() const
	{
		return TYPE;
	}

	Button::State UIChannel::button_pressed(uint16_t buttonid)
	{
		if (buttonid < Buttons::CH)
		{
			switch (buttonid)
			{
				case Buttons::CANCEL:
					cancel();
					break;
				case Buttons::CHANGE:
					change_channel();
					break;
				default:
					break;
			}
		} else
		{
			if (buttonid - Buttons::CH == current_channel)
				return Button::State::NORMAL;

			selected_channel = buttonid - Buttons::CH;
			update_selected_channel_position();
		}

		return Button::State::NORMAL;
	}

	void UIChannel::cancel()
	{
		deactivate();

		current_channel = Configuration::get().get_channelid();
		selected_channel = current_channel;
		selected_channel_x = current_channel_x;
		selected_channel_y = current_channel_y;
	}

	void UIChannel::change_channel()
	{
		if (request_channel(selected_channel + 1)) cancel();
	}

	bool UIChannel::request_channel(int channel)
	{
		auto &config = Configuration::get();
		const int count = std::min<int>(20, LoginParser::channel_count(config.get_worldid()));
		if (!Session::get().is_connected()
			|| channel < 1 || channel > count || channel - 1 == config.get_channelid())
			return false;
		pending_channel = channel - 1;
		ChangeChannelPacket(static_cast<uint8_t>(pending_channel)).dispatch();
		return true;
	}

	int UIChannel::get_pending_channel()
	{
		return pending_channel < 0 ? 0 : pending_channel + 1;
	}

	void UIChannel::confirm_change(bool channel_transfer)
	{
		if (channel_transfer && pending_channel >= 0)
		{
			Configuration::get().set_channelid(static_cast<uint8_t>(pending_channel));
		}
		pending_channel = -1;
	}

	void UIChannel::toggle_active()
	{
		UIElement::toggle_active();
		current_channel = Configuration::get().get_channelid();
		selected_channel = current_channel;
		update_selected_channel_position();
		current_channel_x = selected_channel_x;
		current_channel_y = selected_channel_y;
	}

	void UIChannel::update_selected_channel_position()
	{
		size_t x = 0;
		size_t y = 0;

		for (size_t i = 0; i < channel_count; i++)
		{
			if (x >= 5)
			{
				x = 0;
				y++;
			}

			if (i == selected_channel)
			{
				selected_channel_x = 11 + 70 * x;
				selected_channel_y = 55 + 20 * y;
				break;
			}

			x++;
		}
	}
}