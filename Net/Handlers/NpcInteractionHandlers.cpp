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
#include "NpcInteractionHandlers.h"
#include <stdexcept>

#include "../IO/UI.h"
#include "../IO/UITypes/UINpcTalk.h"
#include "../IO/UITypes/UIShop.h"

namespace ms
{
	void NpcDialogueHandler::handle(InPacket& recv) const
	{
		recv.skip(1);

		int32_t npcid = recv.read_int();
		int8_t msgtype = recv.read_byte(); // 0 - textonly, 1 - yes/no, 4 - selection, 12 - accept/decline
		int8_t speaker = recv.read_byte();
		std::string text = recv.read_string();

		int16_t style = 0;

		if (msgtype == 0 && recv.length() > 0)
			style = recv.read_short();

		std::string initial;
        int32_t minimum = 0, maximum = 0;
        std::vector<int32_t> styles;
        if (msgtype == 2) { initial = recv.read_string(); recv.read_int(); }
        else if (msgtype == 3) {
            initial = std::to_string(recv.read_int());
            minimum = recv.read_int(); maximum = recv.read_int(); recv.read_int();
            if (minimum > maximum) throw std::runtime_error("Invalid NPC number range");
        } else if (msgtype == 7) {
            auto count = static_cast<uint8_t>(recv.read_byte());
            for (unsigned i = 0; i < count; ++i) styles.push_back(recv.read_int());
            initial = "0";
        }
        if ((msgtype == 2 || msgtype == 3 || msgtype == 7) && recv.length())
            throw std::runtime_error("Trailing NPC input payload");

        UI::get().emplace<UINpcTalk>();
		UI::get().enable();

		if (auto npctalk = UI::get().get_element<UINpcTalk>())
			{
            npctalk->change_text(npcid, msgtype, style, speaker, text);
            if (msgtype == 2 || msgtype == 3 || msgtype == 7)
                npctalk->configure_input(initial, minimum, maximum, styles);
        }
	}

	void OpenNpcShopHandler::handle(InPacket& recv) const
	{
		int32_t npcid = recv.read_int();
		auto oshop = UI::get().get_element<UIShop>();

		if (!oshop)
			return;

		UIShop& shop = *oshop;

		shop.reset(npcid);

		int16_t size = recv.read_short();

		for (int16_t i = 0; i < size; i++)
		{
			int32_t itemid = recv.read_int();
			int32_t price = recv.read_int();
			int32_t pitch = recv.read_int();
			int32_t time = recv.read_int();

			recv.skip(4);

			bool norecharge = recv.read_short() == 1;

			if (norecharge)
			{
				int16_t buyable = recv.read_short();

				shop.add_item(itemid, price, pitch, time, buyable);
			}
			else
			{
				recv.skip(4);

				int16_t rechargeprice = recv.read_short();
				int16_t slotmax = recv.read_short();

				shop.add_rechargable(itemid, price, pitch, time, rechargeprice, slotmax);
			}
		}
	}
}