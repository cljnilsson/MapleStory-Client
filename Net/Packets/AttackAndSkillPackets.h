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

#include "../OutPacket.h"

#include "../Gameplay/Combat/Attack.h"

namespace ms
{
	// Notifies the server of an attack. The opcode is determined by the attack type.
	// Attack::CLOSE = CLOSE_ATTACK(44)
	// Attack::RANGED = RANGED_ATTACK(45)
	// Attack::MAGIC = MAGIC_ATTACK(46)
	class AttackPacket : public OutPacket
	{
	public:
		AttackPacket(const AttackResult& attack) : OutPacket(opcodefor(attack.type))
		{
			skip(1);

			write_byte((attack.mobcount << 4) | attack.hitcount);
			write_int(attack.skill);

			// Java AbstractDealDamageHandler: presence depends on skill, even at zero charge.
			if (attack.skill == 2121001 || attack.skill == 2221001 || attack.skill == 2321001
				|| attack.skill == 5101004 || attack.skill == 15101003 || attack.skill == 5201002
				|| attack.skill == 14111006 || attack.skill == 22121000 || attack.skill == 22151001)
				write_int(attack.charge);

			skip(8);

			write_byte(attack.display);
			write_byte(attack.toleft);
			write_byte(attack.stance);

			skip(1);

			write_byte(attack.speed);

			if (attack.type == Attack::Type::RANGED)
			{
				skip(1);
				write_byte(attack.toleft);
				skip(7);
				if (attack.skill == 3121004 || attack.skill == 3221001 || attack.skill == 5221004 || attack.skill == 13111002) skip(4);
			}
			else
			{
				skip(4);
			}

			for (auto& damagetomob : attack.damagelines)
			{
				write_int(damagetomob.first);

				skip(14);

				for (auto& singledamage : damagetomob.second)
				{
					write_int(singledamage.first);
					// TODO: Add critical here
				}

				if (attack.skill != 5221004)
					skip(4);
			}
		}

	private:
		static OutPacket::Opcode opcodefor(Attack::Type type)
		{
			switch (type)
			{
			case Attack::Type::CLOSE:
				return OutPacket::Opcode::CLOSE_ATTACK;
			case Attack::Type::RANGED:
				return OutPacket::Opcode::RANGED_ATTACK;
			default:
				return OutPacket::Opcode::MAGIC_ATTACK;
			}
		}
	};

	// Tells the server that the player took damage.
	// Opcode: TAKE_DAMAGE(48)
	class TakeDamagePacket : public OutPacket
	{
	public:
		enum From : int8_t
		{
			TOUCH = -1
		};

		TakeDamagePacket(int8_t from, uint8_t element, int32_t damage, int32_t mobid, int32_t oid, uint8_t direction) : OutPacket(OutPacket::Opcode::TAKE_DAMAGE)
		{
			write_time();
			write_byte(from);
			write_byte(element);
			write_int(damage);
			write_int(mobid);
			write_int(oid);
			write_byte(direction);
		}

		// From mob attack result.
		TakeDamagePacket(const MobAttackResult& result, From from) : TakeDamagePacket(from, 0, result.damage, result.mobid, result.oid, result.direction) {}
	};

	// Packet which notifies the server of a skill usage.
	// Opcode: USE_SKILL(91)
	class UseSkillPacket : public OutPacket
	{
	public:
		UseSkillPacket(int32_t skillid, int32_t level, Point<int16_t> pos = {}) : OutPacket(OutPacket::Opcode::USE_SKILL)
		{
			write_time();
			write_int(skillid);
			write_byte(static_cast<uint8_t>(level));

			// If monster magnet : some more bytes

			if (skillid % 10000000 == 1004)
				skip(2); // TODO: No idea what this could be

			write_short(pos.x());
			write_short(pos.y());
			write_byte(0); // Java checks available()==5 before reading the optional position.
		}
	};

	// Java SkillEffectHandler / CancelBuffHandler wire layouts.
	class SkillEffectPacket : public OutPacket
	{
	public:
		SkillEffectPacket(int32_t skill, uint8_t level, uint8_t speed, bool left) : OutPacket(Opcode::SKILL_EFFECT)
		{ write_int(skill); write_byte(level); write_byte(0); write_byte(speed); write_byte(left); }
	};
	class CancelSkillPacket : public OutPacket
	{
	public:
		explicit CancelSkillPacket(int32_t skill) : OutPacket(Opcode::CANCEL_SKILL) { write_int(skill); }
	};
	class SummonAttackPacket : public OutPacket
	{
	public:
		SummonAttackPacket(int32_t oid, bool left, Point<int16_t> pos, const AttackResult &attack) : OutPacket(Opcode::SUMMON_ATTACK)
		{
			write_int(oid); write_time(); write_byte(left); write_byte(attack.mobcount);
			write_short(pos.x()); write_short(pos.y()); write_short(pos.x()); write_short(pos.y());
			for (const auto &line : attack.damagelines)
			{ write_int(line.first); skip(18); write_int(line.second.front().first); }
		}
	};
}
