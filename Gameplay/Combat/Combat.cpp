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
#include "Combat.h"
#include "../../Data/SkillData.h"
#include <nlnx/nx.hpp>
#include "../../HeadlessState.h"

#include "../../Character/SkillId.h"
#include "../../IO/Messages.h"

#include "../../Net/Packets/AttackAndSkillPackets.h"
#include "../../Net/Packets/GameplayPackets.h"

namespace ms
{
	Combat::Combat(Player &in_player, MapChars &in_chars, MapMobs &in_mobs, MapReactors &in_reactors) : player(
			in_player), chars(in_chars), mobs(in_mobs), reactors(in_reactors),
																										attackresults(
																												[&](const AttackResult &attack)
																												{
																													apply_attack(
																															attack);
																												}),
																										bulleteffects(
																												[&](const BulletEffect &effect)
																												{
																													apply_bullet_effect(
																															effect);
																												}),
																										damageeffects(
																												[&](const DamageEffect &effect)
																												{
																													apply_damage_effect(
																															effect);
																												})
	{}

	void Combat::draw(double viewx, double viewy, float alpha) const
	{
		for (const auto &entry : summons)
		{
			const auto &s = entry.second;
			if (!s.asset) continue;
			(s.attacking ? s.attack : s.idle).draw(DrawArgument(s.position + Point<int16_t>(viewx, viewy), !s.left), alpha);
			if (!s.drawn && HeadlessState::observing())
			{ s.drawn = true; HeadlessState::presentation("render", s.attacking ? "summon_attack" : "summon", s.skill, s.oid); }
		}
		for (auto &be : bullets)
			be.bullet.draw(viewx, viewy, alpha);

		for (auto &dn : damagenumbers)
			dn.draw(viewx, viewy, alpha);
	}

	void Combat::update()
	{
		if (held)
		{
			held_ms += Constants::TIMESTEP;
			hold_tick += Constants::TIMESTEP;
			if (held_ms >= 30000 || player.get_stats().get_stat(MapleStat::HP) == 0) cancel_skill();
			else if ((held == 3121004 || held == 13111002 || held == 5221004) && hold_tick >= 120)
			{ hold_tick = 0; if (!use_move(held) && !player.is_attacking()) cancel_skill(); }
		}
		update_summons();
		attackresults.update();
		bulleteffects.update();
		damageeffects.update();

		bullets.remove_if(
				[&](BulletEffect &mb)
				{
					int32_t target_oid = mb.damageeffect.target_oid;

					if (mobs.contains(target_oid))
					{
						mb.target = mobs.get_mob_head_position(target_oid);
						bool apply = mb.bullet.update(mb.target);

						if (apply)
							apply_damage_effect(mb.damageeffect);

						return apply;
					} else
					{
						return mb.bullet.update(mb.target);
					}
				}
		);

		damagenumbers.remove_if(
				[](DamageNumber &dn)
				{
					return dn.update();
				}
		);
	}

	bool Combat::use_move(int32_t move_id)
	{
		if (!player.can_attack() || (move_id && (player.get_skilllevel(move_id) <= 0 || SkillData::get(move_id).is_passive()))) return false;
		const SpecialMove &move = get_move(move_id);
		if (player.can_use(move) != SpecialMove::FBR_NONE) return false;
		apply_move(move);
		return true;
	}

	bool Combat::skill_key(int32_t skill, bool down)
	{
		if (!down)
		{
			if (held != skill) return false;
			bool release = held != 3121004 && held != 13111002 && held != 5221004;
			int elapsed = held_ms;
			cancel_skill();
			if (release && player.can_attack() && player.can_use(get_move(skill)) == SpecialMove::FBR_NONE)
				apply_move(get_move(skill), elapsed);
			return true;
		}
		if (held) return false;
		bool keydown = skill == 3121004 || skill == 13111002 || skill == 5221004 || skill == 3221001
			|| skill == 2121001 || skill == 2221001 || skill == 2321001;
		if (!keydown) return use_move(skill);
		if (!player.can_attack() || player.get_skilllevel(skill) <= 0 || player.can_use(get_move(skill)) != SpecialMove::FBR_NONE) return false;
		held = skill; held_ms = hold_tick = 0;
		player.set_charge(skill);
		SkillEffectPacket(skill, player.get_skilllevel(skill), player.get_integer_attackspeed(), player.getflip()).dispatch();
		HeadlessState::presentation("model", "skill_hold", skill, player.get_oid());
		return true;
	}

	bool Combat::cancel_skill()
	{
		if (!held) return false;
		int skill = held; held = held_ms = hold_tick = 0;
		player.set_charge(0);
		CancelSkillPacket(skill).dispatch();
		HeadlessState::presentation("end", "skill_hold", skill, player.get_oid());
		return true;
	}

	void Combat::reset_field()
	{
		held = held_ms = hold_tick = 0;
		player.set_charge(0);
		summons.clear();
		attackresults.clear(); bulleteffects.clear(); damageeffects.clear(); bullets.clear(); damagenumbers.clear();
	}

	void Combat::skill_effect(int32_t cid, int32_t skill, int level, bool left)
	{
		if (auto target = chars.get_char(cid))
		{ target->update_skill(skill, level); target->set_direction(!left); target->set_charge(skill); }
	}
	void Combat::cancel_effect(int32_t cid, int32_t skill)
	{
		if (auto target = chars.get_char(cid); target && target->get_charge() == skill) target->set_charge(0);
	}

	void Combat::apply_move(const SpecialMove &move, int32_t charge)
	{
		HeadlessState::presentation("model", "skill", move.get_id(), player.get_oid());
		if (move.is_attack())
		{
			Attack attack = player.prepare_attack(move.is_skill());

			move.apply_useeffects(player);
			move.apply_actions(player, attack.type);

			player.set_afterimage(move.get_id());

			move.apply_stats(player, attack);

			Point<int16_t> origin = attack.origin;
			Rectangle<int16_t> range = attack.range;
			int16_t hrange = static_cast<int16_t>(range.left() * attack.hrange);

			if (attack.toleft)
			{
				range = {
						origin.x() + hrange,
						origin.x() + range.right(),
						origin.y() + range.top(),
						origin.y() + range.bottom()
				};
			} else
			{
				range = {
						origin.x() - range.right(),
						origin.x() - hrange,
						origin.y() + range.top(),
						origin.y() + range.bottom()
				};
			}

			// This approach should also make it easier to implement PvP
			uint8_t mobcount = attack.mobcount;
			AttackResult result = attack;
			result.charge = charge;
			result.level = player.get_skilllevel(move.get_id());

			MapObjects *mob_objs = mobs.get_mobs();
			MapObjects *reactor_objs = reactors.get_reactors();

			std::vector<int32_t> mob_targets = find_closest(mob_objs, range, origin, mobcount, true);
			std::vector<int32_t> reactor_targets = find_closest(reactor_objs, range, origin, mobcount, false);

			mobs.send_attack(result, attack, mob_targets, mobcount);
			result.attacker = player.get_oid();
			extract_effects(player, move, result);

			apply_use_movement(move);
			apply_result_movement(move, result);

			AttackPacket(result).dispatch();

			if (reactor_targets.size())
				if (Optional<Reactor> reactor = reactor_objs->get(reactor_targets.at(0)))
					DamageReactorPacket(reactor->get_oid(), player.get_position(), 0, 0).dispatch();
		} else
		{
			move.apply_useeffects(player);
			move.apply_actions(player, Attack::Type::MAGIC);

			int32_t moveid = move.get_id();
			int32_t level = player.get_skills().get_level(moveid);
			UseSkillPacket(moveid, level, player.get_position()).dispatch();
		}
	}

	std::vector<int32_t>
	Combat::find_closest(MapObjects *objs, Rectangle<int16_t> range, Point<int16_t> origin, uint8_t objcount,
						 bool use_mobs) const
	{
		std::multimap<uint16_t, int32_t> distances;

		for (auto &mmo : *objs)
		{
			if (use_mobs)
			{
				const Mob *mob = static_cast<const Mob *>(mmo.second.get());

				if (mob && mob->is_alive() && mob->is_in_range(range))
				{
					int32_t oid = mob->get_oid();
					uint16_t distance = mob->get_position().distance(origin);
					distances.emplace(distance, oid);
				}
			} else
			{
				// Assume Reactor
				const Reactor *reactor = static_cast<const Reactor *>(mmo.second.get());

				if (reactor && reactor->is_hittable() && reactor->is_in_range(range))
				{
					int32_t oid = reactor->get_oid();
					uint16_t distance = reactor->get_position().distance(origin);
					distances.emplace(distance, oid);
				}
			}
		}

		std::vector<int32_t> targets;

		for (auto &iter : distances)
		{
			if (targets.size() >= objcount)
				break;

			targets.push_back(iter.second);
		}

		return targets;
	}

	void Combat::apply_use_movement(const SpecialMove &move)
	{
		switch (move.get_id())
		{
			case SkillId::Id::TELEPORT_FP:
			case SkillId::Id::IL_TELEPORT:
			case SkillId::Id::PRIEST_TELEPORT:
			case SkillId::Id::FLASH_JUMP:
			default:
				break;
		}
	}

	void Combat::apply_result_movement(const SpecialMove &move, const AttackResult &result)
	{
		switch (move.get_id())
		{
			case SkillId::Id::RUSH_HERO:
			case SkillId::Id::RUSH_PALADIN:
			case SkillId::Id::RUSH_DK:
				apply_rush(result);
				break;
			default:
				break;
		}
	}

	void Combat::apply_rush(const AttackResult &result)
	{
		if (result.mobcount == 0)
			return;

		Point<int16_t> mob_position = mobs.get_mob_position(result.last_oid);
		int16_t targetx = mob_position.x();
		player.rush(targetx);
	}

	void Combat::apply_bullet_effect(const BulletEffect &effect)
	{
		bullets.push_back(effect);

		if (bullets.back().bullet.settarget(effect.target))
		{
			apply_damage_effect(effect.damageeffect);
			bullets.pop_back();
		}
	}

	void Combat::apply_damage_effect(const DamageEffect &effect)
	{
		Point<int16_t> head_position = mobs.get_mob_head_position(effect.target_oid);
		damagenumbers.push_back(effect.number);
		damagenumbers.back().set_x(head_position.x());

		const SpecialMove &move = get_move(effect.move_id);
		mobs.apply_damage(effect.target_oid, effect.damage, effect.toleft, effect.user, move);
	}

	void Combat::push_attack(const AttackResult &attack)
	{
		attackresults.push(400, attack);
	}

	void Combat::apply_attack(const AttackResult &attack)
	{
		if (Optional<OtherChar> ouser = chars.get_char(attack.attacker))
		{
			OtherChar &user = *ouser;
			HeadlessState::presentation("model", "attack", attack.skill, attack.attacker);
			user.update_skill(attack.skill, attack.level);
			user.update_speed(attack.speed);

			const SpecialMove &move = get_move(attack.skill);
			move.apply_useeffects(user);

			if (Stance::Id stance = Stance::by_id(attack.stance))
				user.attack(stance);
			else
				move.apply_actions(user, attack.type);

			user.set_afterimage(attack.skill);

			extract_effects(user, move, attack);
		}
	}

	void Combat::extract_effects(const Char &user, const SpecialMove &move, const AttackResult &result)
	{
		AttackUser attackuser = {
				user.get_skilllevel(move.get_id()),
				user.get_level(),
				user.is_twohanded(),
				!result.toleft
		};

		if (result.bullet)
		{
			Bullet bullet{
					move.get_bullet(user, result.bullet),
					user.get_position(),
					result.toleft
			};

			for (auto &line : result.damagelines)
			{
				int32_t oid = line.first;

				if (mobs.contains(oid))
				{
					std::vector<DamageNumber> numbers = place_numbers(oid, line.second);
					Point<int16_t> head = mobs.get_mob_head_position(oid);

					size_t i = 0;

					for (auto &number : numbers)
					{
						DamageEffect effect{
								attackuser,
								number,
								line.second[i].first,
								result.toleft,
								oid,
								move.get_id()
						};

						bulleteffects.emplace(user.get_attackdelay(i), std::move(effect), bullet, head);
						i++;
					}
				}
			}

			if (result.damagelines.empty())
			{
				int16_t xshift = result.toleft ? -400 : 400;
				Point<int16_t> target = user.get_position() + Point<int16_t>(xshift, -26);

				for (uint8_t i = 0; i < result.hitcount; i++)
				{
					DamageEffect effect{attackuser, {}, 0, false, 0, 0};
					bulleteffects.emplace(user.get_attackdelay(i), std::move(effect), bullet, target);
				}
			}
		} else
		{
			for (auto &line : result.damagelines)
			{
				int32_t oid = line.first;

				if (mobs.contains(oid))
				{
					std::vector<DamageNumber> numbers = place_numbers(oid, line.second);

					size_t i = 0;

					for (auto &number : numbers)
					{
						damageeffects.emplace(
								user.get_attackdelay(i),
								attackuser,
								number,
								line.second[i].first,
								result.toleft,
								oid,
								move.get_id()
						);

						i++;
					}
				}
			}
		}
	}

	std::vector<DamageNumber>
	Combat::place_numbers(int32_t oid, const std::vector<std::pair<int32_t, bool>> &damagelines)
	{
		std::vector<DamageNumber> numbers;
		int16_t head = mobs.get_mob_head_position(oid).y();

		for (auto &line : damagelines)
		{
			int32_t amount = line.first;
			bool critical = line.second;
			DamageNumber::Type type = critical ? DamageNumber::Type::CRITICAL : DamageNumber::Type::NORMAL;
			numbers.emplace_back(type, amount, head, 0, oid);

			head -= DamageNumber::rowheight(critical);
		}

		return numbers;
	}

	void Combat::show_buff(int32_t cid, int32_t skillid, int8_t level)
	{
		if (Optional<OtherChar> ouser = chars.get_char(cid))
		{
			OtherChar &user = *ouser;
			HeadlessState::presentation("model", "buff_effect", skillid, cid);
			user.update_skill(skillid, level);

			const SpecialMove &move = get_move(skillid);
			move.apply_useeffects(user);
			move.apply_actions(user, Attack::Type::MAGIC);
		}
	}

	void Combat::show_player_buff(int32_t skillid)
	{
		HeadlessState::presentation("model", "buff_effect", skillid, player.get_oid());
		get_move(skillid).apply_useeffects(player);
	}

	void Combat::spawn_summon(Summon s)
	{
		std::string id = std::to_string(s.skill);
		while (id.size() < 7) id = "0" + id;
		auto source = nl::nx::skill[id.substr(0, id.size() - 4) + ".img"]["skill"][id]["summon"];
		auto idle = source["stand"]["0"] ? source["stand"] : source["fly"];
		s.asset = bool(idle);
		s.idle = Animation(idle); s.attack = Animation(source["attack1"]);
		s.interval = int(source["attack1"]["info"]["attackAfter"]);
		for (auto frame : source["attack1"]) if (frame.data_type() == nl::node::type::bitmap) s.interval += int(frame["delay"]);
		s.interval = std::max(1, s.interval);
		s.radius = int(source["attack1"]["info"]["range"]["r"]);
		HeadlessState::presentation("model", "summon", s.skill, s.oid);
		if (summons.size() < 128 || summons.count(s.oid)) summons.insert_or_assign(s.oid, std::move(s));
	}
	void Combat::remove_summon(int32_t owner, int32_t oid)
	{
		auto it = summons.find(oid);
		if (it == summons.end() || it->second.owner != owner) return;
		HeadlessState::presentation("end", "summon", it->second.skill, oid);
		summons.erase(it);
	}
	void Combat::summon_attack(int32_t owner, int32_t oid, bool left, const std::vector<std::pair<int32_t, int32_t>> &damage)
	{
		auto it = summons.find(oid);
		if (it == summons.end() || it->second.owner != owner) return;
		auto &s = it->second;
		s.left = left; s.attacking = true; s.attack.reset(); s.drawn = false;
		HeadlessState::presentation("model", "summon_attack", s.skill, oid);
		for (auto line : damage)
		{
			auto head = mobs.get_mob_head_position(line.first);
			damagenumbers.emplace_back(DamageNumber::NORMAL, line.second, head.y(), 0, line.first);
			damagenumbers.back().set_x(head.x());
		}
	}
	void Combat::update_summons()
	{
		for (auto &entry : summons)
		{
			auto &s = entry.second;
			if (s.attacking) { if (s.attack.update()) s.attacking = false; }
			else s.idle.update();
			// First-job Cygnus summon attacks; follow movement is not yet implemented.
			if (s.owner != player.get_oid() || !s.can_attack || s.radius <= 0 || (s.skill != 11001004 && s.skill != 12001004 && s.skill != 13001004 && s.skill != 14001005 && s.skill != 15001004) || player.get_stats().get_stat(MapleStat::HP) == 0) continue;
			s.elapsed += Constants::TIMESTEP;
			if (s.elapsed < s.interval) continue;
			s.elapsed = 0;
			std::string id = std::to_string(s.skill);
			auto level = nl::nx::skill[id.substr(0, id.size() - 4) + ".img"]["skill"][id]["level"][std::to_string(s.level)];
			Attack attack = player.prepare_attack(true);
			attack.skill = s.skill; attack.origin = s.position; attack.hitcount = 1;
			attack.mobcount = std::max(1, int(level["mobCount"]));
			attack.matk = int(level["mad"]);
			if (attack.matk) attack.damagetype = Attack::DMG_MAGIC;
			else { attack.mindamage *= int(level["pad"]) * 0.05; attack.maxdamage *= int(level["pad"]) * 0.05; }
			auto targets = find_closest(mobs.get_mobs(), Rectangle<int16_t>(s.position.x()-s.radius, s.position.x()+s.radius, s.position.y()-s.radius, s.position.y()+s.radius), s.position, attack.mobcount, true);
			if (targets.empty()) continue;
			s.left = mobs.get_mob_position(targets.front()).x() < s.position.x();
			attack.toleft = s.left;
			AttackResult result(attack);
			mobs.send_attack(result, attack, targets, attack.mobcount);
			SummonAttackPacket(s.oid, s.left, s.position, result).dispatch();
			std::vector<std::pair<int32_t, int32_t>> damage;
			for (const auto &line : result.damagelines) damage.emplace_back(line.first, line.second.front().first);
			summon_attack(s.owner, s.oid, s.left, damage);
		}
	}

	const SpecialMove &Combat::get_move(int32_t move_id)
	{
		if (move_id == 0)
			return regularattack;

		auto iter = skills.find(move_id);

		if (iter == skills.end())
			iter = skills.emplace(move_id, move_id).first;

		return iter->second;
	}
}