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
#include "AttackHandlers.h"
#include "../../HeadlessState.h"

#include "../Character/SkillId.h"
#include "../Gameplay/Stage.h"

#include <unordered_map>
#include <vector>

namespace ms
{
	AttackHandler::AttackHandler(Attack::Type t)
	{
		type = t;
	}

	void AttackHandler::handle(InPacket& recv) const
	{
		int32_t cid = recv.read_int();
		uint8_t count = recv.read_byte();

		recv.skip(1);

		AttackResult attack;
		attack.type = type;
		attack.attacker = cid;

		attack.level = recv.read_byte();
		attack.skill = (attack.level > 0) ? recv.read_int() : 0;

		attack.display = recv.read_byte();
		attack.toleft = recv.read_bool();
		attack.stance = recv.read_byte();
		attack.speed = recv.read_byte();

		recv.skip(1);

		attack.bullet = recv.read_int();

		attack.mobcount = (count >> 4) & 0xF;
		attack.hitcount = count & 0xF;

		for (uint8_t i = 0; i < attack.mobcount; i++)
		{
			int32_t oid = recv.read_int();

			recv.skip(1);

			uint8_t length = (attack.skill == SkillId::Id::MESO_EXPLOSION) ? recv.read_byte() : attack.hitcount;

			for (uint8_t j = 0; j < length; j++)
			{
				int32_t damage = recv.read_int();
				bool critical = false; // TODO: ?
				auto singledamage = std::make_pair(damage, critical);
				attack.damagelines[oid].push_back(singledamage);
			}
		}

		HeadlessState::presentation("packet", "attack", attack.skill, cid);
		Stage::get().get_combat().push_attack(attack);
	}

	void SkillEffectHandler::handle(InPacket &recv) const
	{
		int cid = recv.read_int(), skill = recv.read_int();
		int level = recv.read_byte(); recv.skip(2); bool left = recv.read_bool();
		HeadlessState::presentation("packet", "skill_hold", skill, cid);
		Stage::get().get_combat().skill_effect(cid, skill, level, left);
	}
	void CancelSkillEffectHandler::handle(InPacket &recv) const
	{
		int cid = recv.read_int(), skill = recv.read_int();
		HeadlessState::presentation("packet", "skill_cancel", skill, cid);
		Stage::get().get_combat().cancel_effect(cid, skill);
	}
	void SpawnSummonHandler::handle(InPacket &recv) const
	{
		Combat::Summon s;
		s.owner = recv.read_int(); s.oid = recv.read_int(); s.skill = recv.read_int();
		recv.skip(1); s.level = recv.read_byte();
		int16_t x = recv.read_short(), y = recv.read_short(); s.position = {x,y};
		s.left = (recv.read_byte() & 1) != 0; recv.skip(2);
		s.movement = recv.read_byte(); s.can_attack = recv.read_bool(); recv.skip(1);
		HeadlessState::presentation("packet", "summon", s.skill, s.oid);
		Stage::get().get_combat().spawn_summon(std::move(s));
	}
	void RemoveSummonHandler::handle(InPacket &recv) const
	{
		int owner = recv.read_int(), oid = recv.read_int(); recv.skip(1);
		Stage::get().get_combat().remove_summon(owner, oid);
	}
	void SummonAttackHandler::handle(InPacket &recv) const
	{
		int owner = recv.read_int(), oid = recv.read_int(); recv.skip(1);
		bool left = (recv.read_byte() & 1) != 0;
		uint8_t count = recv.read_byte();
		std::vector<std::pair<int32_t, int32_t>> damage;
		for (int i = 0; i < count; ++i) { int target = recv.read_int(); recv.skip(1); int amount = recv.read_int(); damage.emplace_back(target, amount); }
		HeadlessState::presentation("packet", "summon_attack", oid, owner);
		Stage::get().get_combat().summon_attack(owner, oid, left, damage);
	}

	void ForeignBuffHandler::handle(InPacket &recv) const
	{
		int cid = recv.read_int();
		uint64_t masks[2] = {static_cast<uint64_t>(recv.read_long()), static_cast<uint64_t>(recv.read_long())};
		auto target = Stage::get().get_chars().get_char(cid);
		size_t count = 0;
		for (auto mask : masks) for (; mask; mask &= mask-1) ++count;
		if ((!cancel && recv.length() != count * 2 + 6) || (cancel && recv.length() != 0))
		{ HeadlessState::presentation("packet", "foreign_buff_unsupported", 0, cid); return; }
		// Generic foreign buffs carry short values; specialized layouts remain outside this slice.
		for (int word = 0; word < 2; ++word) for (int bit = 0; bit < 64; ++bit)
			if (masks[word] & (uint64_t(1) << bit))
			{
				int16_t value = cancel ? 0 : recv.read_short();
				if (target) { if (cancel) target->foreign_buffs.erase(word*64+bit); else target->foreign_buffs[word*64+bit] = value; }
			}
		HeadlessState::presentation("packet", cancel ? "foreign_buff_cancel" : "foreign_buff", 0, cid);
	}
}