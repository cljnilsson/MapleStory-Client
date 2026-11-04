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
#pragma once

#include "../PacketHandler.h"

#include "../../Graphics/Text.h"
#include "../../Character/Look/CharLook.h"
#include <chrono>
#include <deque>
#include <ostream>
#include <memory>
#include "../../Graphics/Animation.h"

namespace ms
{
	// Received server broadcasts. Lifetimes end on server removal packets.
	struct BroadcastDisplay
	{
		std::string family, owner, message;
		int32_t id = 0, item = 0, channel = 0, x = 0, y = 0, foothold = 0;
		bool active = false, ear = false;
		uint64_t draws = 0;
		int64_t cleared_ms = -1;
		std::chrono::steady_clock::time_point received = std::chrono::steady_clock::now();
		std::shared_ptr<CharLook> look, partner;
		std::string partner_name;
		Text title_text;
		std::vector<Text> lines;
		std::string layout_title;
		int layout_width = 0;
		Animation effect;
		std::string asset_path;
		bool asset = false;
		int64_t age_ms() const;
	};
	class BroadcastState
	{
	public:
		static BroadcastState &get();
		std::deque<BroadcastDisplay> notices;
		BroadcastDisplay avatar, tv, weather;
		std::map<int32_t, BroadcastDisplay> kites;
		void notice(BroadcastDisplay display);
		void draw(Point<int16_t> view) ;
		void update();
		void clear_field();
		void snapshot(std::ostream &out) const;
	};
	class BroadcastEffectHandler : public PacketHandler
	{
	public:
		explicit BroadcastEffectHandler(int opcode) : opcode(opcode) {}
		void handle(InPacket &recv) const override;
	private:
		int opcode;
	};

	template<int Opcode> class BroadcastHandler : public BroadcastEffectHandler
	{ public: BroadcastHandler() : BroadcastEffectHandler(Opcode) {} };

	class SocialChatHandler : public PacketHandler
	{
	public:
		explicit SocialChatHandler(int opcode) : opcode(opcode) {}
		void handle(InPacket &recv) const override;
	private:
		int opcode;
	};
	template<int Opcode> class SocialHandler : public SocialChatHandler
	{ public: SocialHandler() : SocialChatHandler(Opcode) {} };

	// Show a status message
	// Opcode: SHOW_STATUS_INFO(39)
	class ShowStatusInfoHandler : public PacketHandler
	{
	public:
		void handle(InPacket& recv) const override;
	private:
		void show_status(Color::Name color, const std::string& message) const;
	};

	// Display a timed player hint.
	// Opcode: PLAYER_HINT(214)
	class PlayerHintHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Show a server message
	// Opcode: SERVER_MESSAGE(68)
	class ServerMessageHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Show another type of server message
	// Opcode: WEEK_EVENT_MESSAGE(77)
	class WeekEventMessageHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Show a chat message
	// CHAT_RECEIVED(162)
	class ChatReceivedHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Shows the effect of a scroll
	// Opcode: SCROLL_RESULT(167)
	class ScrollResultHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};

	// Can contain numerous different effects and messages
	// Opcode: SHOW_ITEM_GAIN_INCHAT(206)
	class ShowItemGainInChatHandler : public PacketHandler
	{
		void handle(InPacket& recv) const override;
	};
}
