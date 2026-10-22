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
#include "UILogin.h"
#include "../../Graphics/GraphicsGL.h"

#include "UILoginNotice.h"
#include "UILoginWait.h"

#include "../UI.h"

#include "../Components/MapleButton.h"

#include "../../Audio/Audio.h"

#include "../../Net/Packets/LoginPackets.h"

#ifdef WIN32
#include <windows.h>
#endif

#include <nlnx/nx.hpp>

namespace ms
{
	UILogin::UILogin() : UIElement(Point<int16_t>(0, 0), Point<int16_t>(800, 600))
	{
		LoginStartPacket().dispatch();

		Music("BgmUI.img/Title").play();

		std::string version_text = Configuration::get().get_version();
		version = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::LEMONGRASS, "Ver. " + version_text);

		nl::node map = nl::nx::map["Back"]["login.img"];
		nl::node back = map["back"];
		nl::node ani = map["ani"];

		nl::node obj = nl::nx::map["Obj"]["login.img"];
		nl::node title = nl::nx::ui["Login.img"]["Title"];
		nl::node common = nl::nx::ui["Login.img"]["Common"];

		nl::node prettyLogo = nl::nx::map["Obj"]["login.img"]["Title"]["logo"]["0"];

		sprites.emplace_back(back["11"], Point<int16_t>(400, 300));
		sprites.emplace_back(ani["17"], Point<int16_t>(129, 283));
		sprites.emplace_back(ani["18"], Point<int16_t>(306, 252));
		sprites.emplace_back(ani["19"], Point<int16_t>(379, 207));
		sprites.emplace_back(back["35"], Point<int16_t>(399, 260));
		sprites.emplace_back(prettyLogo, Point<int16_t>(409, 144));
		sprites.emplace_back(obj["Title"]["signboard"]["0"]["0"], Point<int16_t>(510, 330));
		sprites.emplace_back(common["frame"], Point<int16_t>(400, 300));

		buttons[Buttons::BT_LOGIN] = std::make_unique<MapleButton>(title["BtLogin"], Point<int16_t>(593, 250));
		buttons[Buttons::BT_SAVEID] = std::make_unique<MapleButton>(title["BtLoginIDSave"], Point<int16_t>(410, 305));
		buttons[Buttons::BT_IDLOST] = std::make_unique<MapleButton>(title["BtLoginIDLost"], Point<int16_t>(490, 305));
		buttons[Buttons::BT_PASSLOST] = std::make_unique<MapleButton>(title["BtPasswdLost"], Point<int16_t>(580, 305));
		buttons[Buttons::BT_REGISTER] = std::make_unique<MapleButton>(title["BtNew"], Point<int16_t>(390, 350));
		buttons[Buttons::BT_HOMEPAGE] = std::make_unique<MapleButton>(title["BtHomePage"], Point<int16_t>(490, 350));
		buttons[Buttons::BT_QUIT] = std::make_unique<MapleButton>(title["BtQuit"], Point<int16_t>(590, 350));

		checkbox[false] = title["check"]["0"];
		checkbox[true] = title["check"]["1"];

		account = Textfield(Text::Font::A13M, Text::Alignment::LEFT, Color::Name::WHITE,
							Rectangle<int16_t>(Point<int16_t>(455, 235), Point<int16_t>(605, 263)), 12);

		account.set_key_callback
				(
						KeyAction::Id::TAB, [&]
						{
							account.set_state(Textfield::State::NORMAL);
							password.set_state(Textfield::State::FOCUSED);
						}
				);

		account.set_enter_callback
				(
						[&](std::string msg)
						{
							login();
						}
				);

		
		accountbg = title["ID"];

		password = Textfield(Text::Font::A13M, Text::Alignment::LEFT, Color::Name::WHITE,
							 Rectangle<int16_t>(Point<int16_t>(455, 265), Point<int16_t>(605, 293)), 12);

		password.set_key_callback
				(
						KeyAction::Id::TAB, [&]
						{
							account.set_state(Textfield::State::FOCUSED);
							password.set_state(Textfield::State::NORMAL);
						}
				);

		password.set_enter_callback
				(
						[&](std::string msg)
						{
							login();
						}
				);

		password.set_cryptchar('*');
		passwordbg = title["PW"];

		saveid = Setting<SaveLogin>::get().load();

		if (saveid)
		{
			account.change_text(Setting<DefaultAccount>::get().load());
			password.set_state(Textfield::State::FOCUSED);
		} else
		{
			account.set_state(Textfield::State::FOCUSED);
		}

		if (Configuration::get().get_auto_login())
		{
			UI::get().emplace<UILoginWait>([]() {});

			auto loginwait = UI::get().get_element<UILoginWait>();

			if (loginwait && loginwait->is_active())
				LoginPacket(
					Configuration::get().get_auto_acc(),
					Configuration::get().get_auto_pass()
					).dispatch();
		}
	}

	void UILogin::draw(float alpha) const
	{
		UIElement::draw(alpha);

        

		version.draw(position + Point<int16_t>(707, 1));
		accountbg.draw(DrawArgument(position + Point<int16_t>(450, 235)));
        passwordbg.draw(DrawArgument(position + Point<int16_t>(450, 265)));
        account.draw(position);
        password.draw(position);

		checkbox[saveid].draw(DrawArgument(position + Point<int16_t>(390, 305)));
	}

	void UILogin::update()
	{
		UIElement::update();

		account.update(position);
		password.update(position);
	}

	void UILogin::login()
	{
		account.set_state(Textfield::State::DISABLED);
		password.set_state(Textfield::State::DISABLED);

		std::string account_text = account.get_text();
		std::string password_text = password.get_text();

		std::function<void()> okhandler = [&, password_text]()
		{
			account.set_state(Textfield::State::NORMAL);
			password.set_state(Textfield::State::NORMAL);

			if (!password_text.empty())
				password.set_state(Textfield::State::FOCUSED);
			else
				account.set_state(Textfield::State::FOCUSED);
		};

		if (account_text.empty())
		{
			UI::get().emplace<UILoginNotice>(UILoginNotice::Message::NOT_REGISTERED, okhandler);
			return;
		}

		if (password_text.length() <= 4)
		{
			UI::get().emplace<UILoginNotice>(UILoginNotice::Message::WRONG_PASSWORD, okhandler);
			return;
		}

		UI::get().emplace<UILoginWait>(okhandler);

		auto loginwait = UI::get().get_element<UILoginWait>();

		if (loginwait && loginwait->is_active())
			LoginPacket(account_text, password_text).dispatch();
	}

	void UILogin::open_url(uint16_t id)
	{
		std::string url;

		switch (id)
		{
			case Buttons::BT_REGISTER:
				url = Configuration::get().get_joinlink();
				break;
			case Buttons::BT_HOMEPAGE:
				url = Configuration::get().get_website();
				break;
			case Buttons::BT_PASSLOST:
				url = Configuration::get().get_findpass();
				break;
			case Buttons::BT_IDLOST:
				url = Configuration::get().get_findid();
				break;
			default:
				return;
		}

		// TODO: (rich) fix
		//ShellExecute(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
	}

	Button::State UILogin::button_pressed(uint16_t id)
	{
		switch (id)
		{
			case Buttons::BT_LOGIN:
				login();

				return Button::State::NORMAL;
			case Buttons::BT_REGISTER:
			case Buttons::BT_HOMEPAGE:
			case Buttons::BT_PASSLOST:
			case Buttons::BT_IDLOST:
				open_url(id);

				return Button::State::NORMAL;
			case Buttons::BT_SAVEID:
				saveid = !saveid;
				Setting<SaveLogin>::get().save(saveid);

				return Button::State::MOUSEOVER;
			case Buttons::BT_QUIT:
				UI::get().quit();

				return Button::State::PRESSED;
			default:
				return Button::State::NORMAL;
		}
	}

	Cursor::State UILogin::send_cursor(bool clicked, Point<int16_t> cursorpos)
	{
		if (Cursor::State new_state = account.send_cursor(cursorpos, clicked))
			return new_state;

		if (Cursor::State new_state = password.send_cursor(cursorpos, clicked))
			return new_state;

		return UIElement::send_cursor(clicked, cursorpos);
	}

	UIElement::Type UILogin::get_type() const
	{
		return TYPE;
	}
}