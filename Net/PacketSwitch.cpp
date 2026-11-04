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
#include "../IO/UITypes/UISocial.h"
#include "PacketSwitch.h"

#include "Handlers/AttackHandlers.h"
#include "Handlers/MapObjectHandlers.h"
#include "Handlers/CashShopHandlers.h"
#include "Handlers/CommonHandlers.h"
#include "Handlers/InventoryHandlers.h"
#include "Handlers/LoginHandlers.h"
#include "Handlers/MapObjectHandlers.h"
#include "Handlers/MessagingHandlers.h"
#include "Handlers/NpcInteractionHandlers.h"
#include "Handlers/PlayerHandlers.h"
#include "Handlers/PlayerInteractionHandlers.h"
#include "Handlers/SetFieldHandlers.h"
#include "Handlers/TestingHandlers.h"
#include "../HeadlessState.h"
#include "../IO/UITypes/UIMarket.h"
#include "../IO/UITypes/UIMTS.h"

#include "../Configuration.h"

namespace ms
{
	// Opcodes for InPackets
	enum Opcode : uint16_t
	{
		/// Login 1
		LOGIN_RESULT = 0,
		SERVERSTATUS = 3,
		VIEW_ALL_CHAR = 8,
		SERVERLIST = 10,
		CHARLIST = 11,
		SERVER_IP = 12,
		CHARNAME_RESPONSE = 13,
		ADD_NEWCHAR_ENTRY = 14,
		DELCHAR_RESPONSE = 15,
		PING = 17,

		/// Login 2
		RECOMMENDED_WORLDS = 27,
		CHECK_SPW_RESULT = 28,

		/// Inventory 1
		MODIFY_INVENTORY = 29,

		/// Player 2
		CHANGE_CHANNEL = 16,
		CHANGE_STATS = 31,
		GIVE_FOREIGN_BUFF = 199, CANCEL_FOREIGN_BUFF = 200,
		GIVE_BUFF = 32,
		CANCEL_BUFF = 33,
		RECALCULATE_STATS = 35,
		UPDATE_SKILL = 36,

		/// Messaging 1
		SHOW_STATUS_INFO = 39,

		/// Inventory 2
		GATHER_RESULT = 52,
		SORT_RESULT = 53,

		/// Player 3

		/// Messaging 2
		SERVER_MESSAGE = 68,
		WEEK_EVENT_MESSAGE = 77,

		SKILL_MACROS = 124,
		SET_FIELD = 125,
		FIELD_EFFECT = 138,

		/// MapObject
		SPAWN_CHAR = 160,
		REMOVE_CHAR = 161,

		/// Messaging
		CHAT_RECEIVED = 162,
		SCROLL_RESULT = 167,

		/// MapObject
		SPAWN_PET = 168,
		CHAR_MOVED = 185,

		/// Attack
		ATTACKED_CLOSE = 186,
		ATTACKED_RANGED = 187,
		ATTACKED_MAGIC = 188,

		SHOW_CHAIR = 196,
		CANCEL_CHAIR = 205,
		SPAWN_SUMMON = 175, REMOVE_SUMMON = 176, SUMMON_ATTACK = 178, SKILL_EFFECT = 190, CANCEL_SKILL_EFFECT = 191,
		UPDATE_CHARLOOK = 197,
		SHOW_FOREIGN_EFFECT = 198,
		SHOW_ITEM_GAIN_INCHAT = 206, // TODO: Rename this (Terribly named)

		/// Player
		ADD_COOLDOWN = 234,

		/// MapObject
		SPAWN_MOB = 236,
		KILL_MOB = 237,
		SPAWN_MOB_C = 238,
		MOB_MOVED = 239,
		MOB_MOVE_RESPONSE = 240,
		SHOW_MOB_HP = 250,
		SPAWN_NPC = 257,
		SPAWN_NPC_C = 259,
		REMOVE_NPC = 0x102,
		IMITATED_NPC_DATA = 81,
		PLAYER_HINT = 214,
		DROP_LOOT = 268,
		REMOVE_LOOT = 269,
		HIT_REACTOR = 277,
		SPAWN_REACTOR = 279,
		REMOVE_REACTOR = 280,

		/// NPC Interaction
		NPC_DIALOGUE = 304,
		OPEN_NPC_SHOP = 305,
		CONFIRM_SHOP_TRANSACTION = 306,
		KEYMAP = 335,

		/// Player Interaction
		CHAR_INFO = 61,
		PARTY_OPERATION = 62,

		/// Cash Shop
		SET_CASH_SHOP = 127
	};

	PacketSwitch::PacketSwitch()
	{
		// Common handlers
		emplace<PING, PingHandler>();

		// Login handlers
		emplace<LOGIN_RESULT, LoginResultHandler>();
		emplace<SERVERSTATUS, ServerStatusHandler>();
		emplace<SERVERLIST, ServerlistHandler>();
		emplace<CHARLIST, CharlistHandler>();
		emplace<VIEW_ALL_CHAR, ViewAllCharHandler>();
		emplace<SERVER_IP, ServerIPHandler>();
		emplace<CHARNAME_RESPONSE, CharnameResponseHandler>();
		emplace<ADD_NEWCHAR_ENTRY, AddNewCharEntryHandler>();
		emplace<DELCHAR_RESPONSE, DeleteCharResponseHandler>();
		emplace<RECOMMENDED_WORLDS, RecommendedWorldsHandler>();

		// SetField handlers
		emplace<SET_FIELD, SetFieldHandler>();

		// MapObject handlers
		emplace<SPAWN_CHAR, SpawnCharHandler>();
		emplace<CHAR_MOVED, CharMovedHandler>();
		emplace<UPDATE_CHARLOOK, UpdateCharLookHandler>();
		emplace<SHOW_CHAIR, ShowChairHandler>();
		emplace<CANCEL_CHAIR, CancelChairHandler>();
		emplace<SHOW_FOREIGN_EFFECT, ShowForeignEffectHandler>();
		emplace<REMOVE_CHAR, RemoveCharHandler>();
		emplace<SPAWN_PET, SpawnPetHandler>();
		emplace<SPAWN_NPC, SpawnNpcHandler>();
		emplace<SPAWN_NPC_C, SpawnNpcControllerHandler>();
		emplace<REMOVE_NPC, RemoveNpcHandler>();
		emplace<IMITATED_NPC_DATA, RemoveImitatedNpcHandler>();
		emplace<SPAWN_MOB, SpawnMobHandler>();
		emplace<SPAWN_MOB_C, SpawnMobControllerHandler>();
		emplace<MOB_MOVED, MobMovedHandler>();
		emplace<MOB_MOVE_RESPONSE, MobMoveResponseHandler>();
		emplace<SHOW_MOB_HP, ShowMobHpHandler>();
		emplace<KILL_MOB, KillMobHandler>();
		emplace<DROP_LOOT, DropLootHandler>();
		emplace<REMOVE_LOOT, RemoveLootHandler>();
		emplace<HIT_REACTOR, HitReactorHandler>();
		emplace<SPAWN_REACTOR, SpawnReactorHandler>();
		emplace<REMOVE_REACTOR, RemoveReactorHandler>();

		// Attack handlers
		emplace<ATTACKED_CLOSE, CloseAttackHandler>();
		emplace<ATTACKED_RANGED, RangedAttackHandler>();
		emplace<ATTACKED_MAGIC, MagicAttackHandler>();

		// Player handlers
		emplace<CHANGE_CHANNEL, ChangeChannelHandler>();
		emplace<KEYMAP, KeymapHandler>();
		emplace<SKILL_MACROS, SkillMacrosHandler>();
		emplace<CHANGE_STATS, ChangeStatsHandler>();
		emplace<SPAWN_SUMMON, SpawnSummonHandler>();
		emplace<REMOVE_SUMMON, RemoveSummonHandler>();
		emplace<SUMMON_ATTACK, SummonAttackHandler>();
		emplace<SKILL_EFFECT, SkillEffectHandler>();
		emplace<CANCEL_SKILL_EFFECT, CancelSkillEffectHandler>();
		emplace<GIVE_FOREIGN_BUFF, ForeignBuffHandler>();
		emplace<CANCEL_FOREIGN_BUFF, CancelForeignBuffHandler>();
		emplace<GIVE_BUFF, ApplyBuffHandler>();
		emplace<CANCEL_BUFF, CancelBuffHandler>();
		emplace<RECALCULATE_STATS, RecalculateStatsHandler>();
		emplace<UPDATE_SKILL, UpdateSkillHandler>();
		emplace<ADD_COOLDOWN, AddCooldownHandler>();

		// Messaging handlers
		emplace<0x6f, BroadcastHandler<0x6f>>();
		emplace<0x70, BroadcastHandler<0x70>>();
		emplace<0x8e, BroadcastHandler<0x8e>>();
		emplace<0x10e, BroadcastHandler<0x10e>>();
		emplace<0x10f, BroadcastHandler<0x10f>>();
		emplace<0x110, BroadcastHandler<0x110>>();
		emplace<0x155, BroadcastHandler<0x155>>();
		emplace<0x156, BroadcastHandler<0x156>>();
		emplace<0x157, BroadcastHandler<0x157>>();
		emplace<SHOW_STATUS_INFO, ShowStatusInfoHandler>();
		emplace<PLAYER_HINT, PlayerHintHandler>();
		emplace<CHAT_RECEIVED, ChatReceivedHandler>();
		emplace<0x5e, FamilyWindowPacket<0x5e>>();
		emplace<0x5f, FamilyWindowPacket<0x5f>>();
		emplace<0x60, FamilyWindowPacket<0x60>>();
		emplace<0x61, FamilyWindowPacket<0x61>>();
		emplace<0x62, FamilyWindowPacket<0x62>>();
		emplace<0x63, FamilyWindowPacket<0x63>>();
		emplace<0x64, FamilyWindowPacket<0x64>>();
		emplace<0x65, FamilyWindowPacket<0x65>>();
		emplace<0x66, FamilyWindowPacket<0x66>>();
		emplace<0x68, FamilyWindowPacket<0x68>>();
		emplace<0x3f, SocialWindowPacket<0x3f>>();
		emplace<0x139, SocialWindowPacket<0x139>>();
		emplace<0x29, SocialWindowPacket<0x29>>();
		emplace<0x76, SocialWindowPacket<0x76>>();
		emplace<0x4a, SocialWindowPacket<0x4a>>();
		emplace<0x48, SocialWindowPacket<0x48>>();
		emplace<0x49, SocialWindowPacket<0x49>>();
		emplace<0x4b, SocialWindowPacket<0x4b>>();
		emplace<0x140, SocialWindowPacket<0x140>>();
		emplace<0x141, SocialWindowPacket<0x141>>();
		emplace<0x7e, MTSPacket<0x7e>>();
		emplace<0x15b, MTSPacket<0x15b>>();
		emplace<0x15c, MTSPacket<0x15c>>();
		emplace<0x86, SocialHandler<0x86>>();
		emplace<0x87, SocialHandler<0x87>>();
		emplace<0x88, SocialHandler<0x88>>();
		emplace<SCROLL_RESULT, ScrollResultHandler>();
		emplace<0x166, VegaSpellHandler>();
		emplace<0x47, MarketOwlResultHandler>();
		emplace<0x13a, MarketHandler>();
		emplace<0xa5, MarketBoxHandler<0xa5>>();
		emplace<0x109, MarketBoxHandler<0x109>>();
		emplace<0x10a, MarketBoxHandler<0x10a>>();
		emplace<0x10b, MarketBoxHandler<0x10b>>();
		emplace<0x46, OwlSearchHandler>();
		emplace<0x15d, MapleLifeResultHandler>();
		emplace<0x15e, MapleLifeErrorHandler>();
		emplace<SERVER_MESSAGE, ServerMessageHandler>();
		emplace<WEEK_EVENT_MESSAGE, WeekEventMessageHandler>();
		emplace<SHOW_ITEM_GAIN_INCHAT, ShowItemGainInChatHandler>();

		// Inventory Handlers
		emplace<MODIFY_INVENTORY, ModifyInventoryHandler>();
		emplace<GATHER_RESULT, GatherResultHandler>();
		emplace<SORT_RESULT, SortResultHandler>();

		// NPC Interaction Handlers
		emplace<NPC_DIALOGUE, NpcDialogueHandler>();
		emplace<OPEN_NPC_SHOP, OpenNpcShopHandler>();

		// Player Interaction
		emplace<CHAR_INFO, CharInfoHandler>();
		emplace<PARTY_OPERATION, PartyOperationHandler>();

		// Cash Shop
		emplace<SET_CASH_SHOP, SetCashShopHandler>();
		emplace<0x144, CashBalanceHandler>();
		emplace<0x145, CashOperationHandler>();
		emplace<0x149, NameRulesHandler>();
		emplace<0x14b, WorldRulesHandler>();

		// TODO: New handlers, they need coded and moved to a proper file.
		emplace<CHECK_SPW_RESULT, CheckSpwResultHandler>();
		emplace<FIELD_EFFECT, FieldEffectHandler>();
	}

	void PacketSwitch::forward(const int8_t* bytes, size_t length) const
	{
		// Wrap the bytes with a parser
		InPacket recv = { bytes, length };
		if (length < sizeof(uint16_t))
		{
			warn("Packet too short to contain opcode", 0);
			HeadlessState::packet_error(-1, length, 0, "Packet too short to contain opcode", true);
			return;
		}

		// Read the opcode to determine handler responsible
		uint16_t opcode = recv.read_short();

		if (Configuration::get().get_show_packets())
		{
			if (opcode == PING)
				std::cout << "Received Packet: PING" << std::endl;
			else
				std::cout << "Received Packet: " << std::to_string(opcode) << std::endl;
		}

		if (opcode == 9)
        {
            std::cout << "[PACKET 9 DEBUG] Length: " << length
                      << " bytes, data:";

            size_t count = length < 24 ? length : 24;

            for (size_t i = 0; i < count; ++i)
            {
                unsigned int value =
                    static_cast<unsigned char>(bytes[i]);

                const char* digits = "0123456789ABCDEF";

                std::cout << " "
                          << digits[(value >> 4) & 15]
                          << digits[value & 15];
            }

            std::cout << std::endl;
        }

        if (opcode < NUM_HANDLERS)
		{
			if (auto& handler = handlers[opcode])
			{
				// Handler is good, packet is passed on

				try
				{
					handler->handle(recv);
				}
				catch (const PacketError& err)
				{
					// Notice about an error
					warn(err.what(), opcode);
					HeadlessState::packet_error(opcode, length, recv.position(), err.what(), true);
				}
			}
			else
			{
				// Warn about an unhandled packet
				warn(MSG_UNHANDLED, opcode);
				HeadlessState::packet_error(opcode, length, recv.position(), MSG_UNHANDLED, false);
			}
		}
		else
		{
			// Warn about a packet with opcode out of bounds
			warn(MSG_OUTOFBOUNDS, opcode);
			HeadlessState::packet_error(opcode, length, recv.position(), MSG_OUTOFBOUNDS, false);
		}
	}

	void PacketSwitch::warn(const std::string& message, size_t opcode) const
	{
		std::cout << "Opcode [" << opcode << "] Error: " << message << std::endl;
	}
}
