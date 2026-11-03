#include "../../HeadlessState.h"
#include <iostream>
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
#include "LoginHandlers.h"

#include "Helpers/LoginParser.h"

#include "../Packets/LoginPackets.h"

#include "../../IO/UI.h"

#include "../../IO/UITypes/UICharSelect.h"
#include "../../IO/UITypes/UIExplorerCreation.h"
#include "../../IO/UITypes/UICygnusCreation.h"
#include "../../IO/UITypes/UIAranCreation.h"
#include "../../IO/UITypes/UIGender.h"
#include "../../IO/UITypes/UILoginNotice.h"
#include "../../IO/UITypes/UILoginWait.h"
#include "../../IO/UITypes/UIRaceSelect.h"
#include "../../IO/UITypes/UITermsOfService.h"
#include "../../IO/UITypes/UIWorldSelect.h"

namespace ms
{
	void LoginResultHandler::handle(InPacket& recv) const
	{
		auto loginwait = UI::get().get_element<UILoginWait>();
		HeadlessState::event("login_result_handler", ",\"wait_present\":" + std::string(loginwait ? "true" : "false") +
			",\"wait_active\":" + std::string(loginwait && loginwait->is_active() ? "true" : "false"));

		if (loginwait && loginwait->is_active())
		{
			std::function<void()> okhandler = loginwait->get_handler();
			int32_t reason = recv.read_int();
			HeadlessState::event("login_result", ",\"reason\":" + std::to_string(reason));

			// Remove previous UIs
			UI::get().remove(UIElement::Type::LOGINNOTICE);
			UI::get().remove(UIElement::Type::LOGINWAIT);
			UI::get().remove(UIElement::Type::TOS);
			UI::get().remove(UIElement::Type::GENDER);

			// The packet should contain a 'reason' integer which can signify various things
			if (reason)
			{
				std::cout << "HEADLESS_ERROR login reason=" << reason << std::endl;
				HeadlessState::event("error", ",\"where\":\"login\",\"reason\":" + std::to_string(reason));
				// Login unsuccessful
				// The LoginNotice displayed will contain the specific information
				switch (reason)
				{
				case 2:
					UI::get().emplace<UILoginNotice>(UILoginNotice::Message::BLOCKED_ID, okhandler);
					break;
				case 5:
					UI::get().emplace<UILoginNotice>(UILoginNotice::Message::NOT_REGISTERED, okhandler);
					break;
				case 7:
					UI::get().emplace<UILoginNotice>(UILoginNotice::Message::ALREADY_LOGGED_IN, okhandler);
					break;
				case 13:
					UI::get().emplace<UILoginNotice>(UILoginNotice::Message::UNABLE_TO_LOGIN_WITH_IP, okhandler);
					break;
				case 23:
					UI::get().emplace<UITermsOfService>(okhandler);
					break;
				default:
					// Other reasons
					if (reason > 0)
					{
						auto reasonbyte = static_cast<int8_t>(reason - 1);

						UI::get().emplace<UILoginNotice>(reasonbyte, okhandler);
					}

					break;
				}
			}
			else
			{
				// Login successful
				// The packet contains information on the account, so we initialize the account with it.
				Account account = LoginParser::parse_account(recv);
				HeadlessState::event("login_account_parsed", ",\"gender\":" + std::to_string(account.female));

				Configuration::get().set_admin(account.admin);

				if (account.female == 10)
				{
					UI::get().emplace<UIGender>(okhandler);
				}
				else
				{
					// Save the "Login ID" if the box for it on the login screen is checked
					if (Setting<SaveLogin>::get().load())
						Setting<DefaultAccount>::get().save(account.name);

					//AfterLoginPacket("1111").dispatch();

					// Request the list of worlds and channels online.
					ServerRequestPacket().dispatch();
					HeadlessState::event("serverlist_request_sent");
				}
			}
		}
	}

	void ServerStatusHandler::handle(InPacket& recv) const
	{
		// Possible values for status:
		// 0 - Normal
		// 1 - Highly populated
		// 2 - Full
		recv.read_short(); // status

		// TODO: I believe it shows a warning message if it's 1 and blocks enter into the world if it's 2. Need to find those messages.
	}

	void ServerlistHandler::handle(InPacket& recv) const
	{
		auto worldselect = UI::get().get_element<UIWorldSelect>();

		if (!worldselect)
			worldselect = UI::get().emplace<UIWorldSelect>();

		// Parse all worlds
		while (recv.available())
		{
			World world = LoginParser::parse_world(recv);

			if (world.wid != -1)
			{
				worldselect->add_world(world);
			}
			else
			{
				// Remove previous UIs
				UI::get().remove(UIElement::Type::LOGIN);

				// Add the world selection screen to the UI
				worldselect->draw_world();

				// End of packet
				return;
			}
		}
	}

	void CharlistHandler::handle(InPacket& recv) const
	{
		auto loginwait = UI::get().get_element<UILoginWait>();
        std::cerr << "[CHARLIST DEBUG] Handler reached, wait exists: "
                  << static_cast<bool>(loginwait)
                  << ", active: "
                  << (loginwait && loginwait->is_active())
                  << std::endl;

		if (loginwait && loginwait->is_active())
		{
			uint8_t channel_id = recv.read_byte();

			// Parse all characters
			std::vector<CharEntry> characters;
			int8_t charcount = recv.read_byte();
            std::cerr << "[CHARLIST DEBUG] Character count: "
                      << static_cast<int>(charcount) << std::endl;

			for (uint8_t i = 0; i < charcount; ++i)
				characters.emplace_back(LoginParser::parse_charentry(recv));

			int8_t pic = recv.read_byte();
			int32_t slots = recv.read_int();

			// Remove previous UIs
			UI::get().remove(UIElement::Type::LOGINNOTICE);
			UI::get().remove(UIElement::Type::LOGINWAIT);

			// Remove the world selection screen
			if (auto worldselect = UI::get().get_element<UIWorldSelect>())
				worldselect->remove_selected();

			// Add the character selection screen
			std::cerr << "[CHARLIST DEBUG] Creating character selection" << std::endl;
            UI::get().emplace<UICharSelect>(characters, charcount, slots, pic);
            std::cerr << "[CHARLIST DEBUG] Character selection created" << std::endl;
			std::cout << "HEADLESS_STATE character_select count="
			          << static_cast<int>(charcount) << " pic="
			          << static_cast<int>(pic) << std::endl;
			HeadlessState::set_screen("character_select");
		}
	}

	namespace
	{
		bool all_requested = false;
		int all_expected = -1, all_received = 0;
		int8_t all_pic = 2;
		std::vector<CharEntry> all_characters;
		std::map<int32_t, uint8_t> all_worlds;
	}

	void ViewAllCharHandler::reset()
	{
		all_requested = false;
		all_expected = -1;
		all_received = 0;
		all_characters.clear();
		all_worlds.clear();
	}

	bool ViewAllCharHandler::request()
	{
		auto worldselect = UI::get().get_element<UIWorldSelect>();
		if (UI::get().is_element_active(UIElement::Type::SOFTKEYBOARD)
			|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE)
			|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM)) return false;
		auto charselect = UI::get().get_element<UICharSelect>();
		bool from_characters = charselect && charselect->is_active() && !charselect->is_view_all();
		if (!worldselect || (!worldselect->is_active() && !from_characters) || all_requested) return false;
		if (from_characters) charselect->deactivate();
		all_requested = true;
		all_pic = LoginParser::account_pic_mode();
		all_expected = -1;
		all_received = 0;
		all_characters.clear();
		all_worlds.clear();
		worldselect->deactivate();
		OutPacket(OutPacket::Opcode::VIEW_ALL_CHAR).dispatch();
		HeadlessState::set_screen("view_all_pending");
		return true;
	}

	void ViewAllCharHandler::handle(InPacket& recv) const
	{
		if (!all_requested) return;
		int status = static_cast<uint8_t>(recv.read_byte());
		if (status == 1 || status == 5)
		{
			all_expected = recv.read_int();
			recv.read_int(); // Display slots, not another character count.
			if (all_expected < 0 || all_expected > 127 || (status == 5 && all_expected != 0))
			{
				all_requested = false;
				HeadlessState::event("view_all_error", ",\"reason\":\"unsupported character count\"");
				if (auto worldselect = UI::get().get_element<UIWorldSelect>()) worldselect->makeactive();
				HeadlessState::set_screen("world_select");
				return;
			}
		}
		else if (status == 0 && all_expected >= 0)
		{
			uint8_t world = recv.read_byte();
			unsigned count = static_cast<uint8_t>(recv.read_byte());
			if (all_received + count > all_expected) throw PacketError("View All count overflow");
			for (unsigned i = 0; i < count; ++i)
			{
				auto character = LoginParser::parse_charentry(recv, true);
				++all_received;
				// The server pads its last row with duplicate entries.
				if (all_worlds.emplace(character.id, world).second)
					all_characters.push_back(std::move(character));
			}
			int8_t pic = recv.read_byte();
			if (pic != 1 && pic != 2) throw PacketError("Invalid View All PIC mode");
			all_pic = pic == 2 ? 2 : (LoginParser::account_pic_mode() == 0 ? 0 : 1);
		}
		else return;
		if (all_expected < 0 || all_received != all_expected) return;
		all_requested = false;
		UI::get().remove(UIElement::Type::LOGINWAIT);
		UI::get().remove(UIElement::Type::LOGINNOTICE);
		if (auto worldselect = UI::get().get_element<UIWorldSelect>()) worldselect->remove_selected();
		UI::get().emplace<UICharSelect>(all_characters, static_cast<int8_t>(all_characters.size()),
			std::max(8, static_cast<int>(all_characters.size())), all_pic, all_worlds, true);
		HeadlessState::event("view_all_result", ",\"count\":" + std::to_string(all_characters.size()));
		HeadlessState::set_screen("character_select");
	}

	void ServerIPHandler::handle(InPacket& recv) const
	{
		recv.skip_byte();

		LoginParser::parse_login(recv);

		int32_t cid = recv.read_int();
		PlayerLoginPacket(cid).dispatch();
	}

	void CharnameResponseHandler::handle(InPacket& recv) const
	{
		// Read the name and if it is already in use
		std::string name = recv.read_string();
		bool used = recv.read_bool();

		std::cerr << "[NAME DEBUG] name="" << name << "", used=" << used << std::endl;

		// Notify whichever creation UI is currently active.
		if (auto explorercreation = UI::get().get_element<UIExplorerCreation>())
		{
			explorercreation->send_naming_result(used);
		}
		else if (auto cygnuscreation = UI::get().get_element<UICygnusCreation>())
		{
			cygnuscreation->send_naming_result(used);
		}
		else if (auto arancreation = UI::get().get_element<UIAranCreation>())
		{
			arancreation->send_naming_result(used);
		}
		else if (auto raceselect = UI::get().get_element<UIRaceSelect>())
		{
			raceselect->send_naming_result(used);
		}
		else
		{
			std::cerr << "[NAME DEBUG] No creation UI found for naming response." << std::endl;
		}
	}

	void AddNewCharEntryHandler::handle(InPacket& recv) const
	{
		recv.skip(1);

		// Parse info on the new character
		CharEntry character = LoginParser::parse_charentry(recv);

		// Read the updated character selection
		if (auto charselect = UI::get().get_element<UICharSelect>())
			{
            charselect->add_character(std::move(character));

            // Return to character selection after the server confirms creation.
            UI::get().remove(UIElement::Type::SOFTKEYBOARD);
            UI::get().remove(UIElement::Type::LOGINNOTICE_CONFIRM);
            UI::get().remove(UIElement::Type::LOGINNOTICE);
            UI::get().remove(UIElement::Type::CLASSCREATION);
            UI::get().remove(UIElement::Type::RACESELECT);

            charselect->post_add_character();

            std::cerr << "[CREATE DEBUG] Returned to character selection."
                      << std::endl;
        }
	}

	void DeleteCharResponseHandler::handle(InPacket& recv) const
	{
		// Read the character id and if deletion was successful (PIC was correct)
		int32_t cid = recv.read_int();
		uint8_t state = recv.read_byte();
		HeadlessState::event("character_delete_result", ",\"character_id\":" + std::to_string(cid)
			+ ",\"status\":" + std::to_string(state) + ",\"ok\":" + (state == 0 ? "true" : "false"));

		// Extract information from the state byte
		if (state)
		{
			UILoginNotice::Message message;

			switch (state)
			{
			case 10:
				message = UILoginNotice::Message::BIRTHDAY_INCORRECT;
				break;
			case 20:
				message = UILoginNotice::Message::INCORRECT_PIC;
				break;
			default:
				message = UILoginNotice::Message::UNKNOWN_ERROR;
			}

			UI::get().emplace<UILoginNotice>(message);
		}
		else
		{
			if (auto charselect = UI::get().get_element<UICharSelect>())
				charselect->remove_character(cid);
		}
	}

	void RecommendedWorldsHandler::handle(InPacket& recv) const
	{
		if (auto worldselect = UI::get().get_element<UIWorldSelect>())
		{
			int16_t count = recv.read_byte();

			for (size_t i = 0; i < count; i++)
			{
				RecommendedWorld world = LoginParser::parse_recommended_world(recv);

				if (world.wid != -1 && !world.message.empty())
					worldselect->add_recommended_world(world);
			}
		}
	}
}
