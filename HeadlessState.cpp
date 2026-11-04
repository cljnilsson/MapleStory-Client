#include <algorithm>
#include "IO/UITypes/UISocial.h"
#include "IO/UITypes/UIMarket.h"
#include "IO/UITypes/UIMTS.h"
#include "HeadlessState.h"
#include "Net/Handlers/MessagingHandlers.h"
#include "Net/Handlers/InventoryHandlers.h"
#include "Data/SkillData.h"
#include "Net/Packets/AttackAndSkillPackets.h"
#include "Net/Packets/PlayerPackets.h"
#include "Configuration.h"
#include "Data/ItemData.h"
#include "IO/UITypes/UIChannel.h"
#include "IO/UITypes/UIChatBar.h"
#include "IO/UITypes/UIQuit.h"
#include "IO/UITypes/UICashShop.h"

#include "Gameplay/Stage.h"
#include "Gameplay/MapleMap/Npc.h"
#include "Gameplay/MapleMap/Mob.h"
#include "Gameplay/MapleMap/ItemDrop.h"
#include "Gameplay/MapleMap/Reactor.h"
#include "IO/UI.h"
#include "IO/KeyAction.h"
#include "IO/UITypes/UIEquipInventory.h"
#include "IO/UITypes/UIItemInventory.h"
#include "IO/UITypes/UINpcTalk.h"
#include "IO/UITypes/UIStatsInfo.h"
#include "IO/UITypes/UINotice.h"
#include "IO/UITypes/UICharSelect.h"
#include "IO/UITypes/UIWorldSelect.h"
#include "IO/UITypes/UISoftKey.h"
#include "IO/UITypes/UILoginNotice.h"
#include "Net/Handlers/LoginHandlers.h"
#include "Net/Handlers/CashShopHandlers.h"
#include "Net/Packets/InventoryPackets.h"
#include "Net/Packets/GameplayPackets.h"
#include "Net/Packets/NpcInteractionPackets.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>

namespace ms
{
	namespace HeadlessState
	{
		namespace
		{
			std::string screen = "starting";
			bool dirty = true;
			std::chrono::steady_clock::time_point last;
			uint64_t snapshot_seq = 0;
			uint64_t field_entry_seq = 0;
			uint64_t progress = 0;
			bool trace = false, export_paused = false;
			std::map<int, int64_t> buff_expiry;
			std::map<int32_t, std::string> mob_statuses;
			std::map<int32_t, std::tuple<int, int, bool>> motion_states;
			uint64_t render_frame = 0, observation_id = 0, observations = 0, dropped = 0;
			uint64_t export_us = 0, export_max_us = 0, export_count = 0;
			const auto process_start = std::chrono::steady_clock::now();
			const std::string session_id = [] {
				if (const char *id = std::getenv("MS_HEADLESS_SESSION_ID"); id && *id) return std::string(id);
				std::random_device random;
				std::ostringstream out;
				out << std::hex << random() << random();
				return out.str();
			}();

			const char *dir()
			{
				static const char *d = std::getenv("MS_HEADLESS_DIR");
				return (d && *d) ? d : nullptr;
			}

			std::string esc(const std::string &s)
			{
				std::string o;
				for (unsigned char c : s)
				{
					if (c == '"' || c == '\\')
						o += std::string("\\") + char(c);
					else if (c < 0x20)
					{
						char b[8];
						std::snprintf(b, sizeof b, "\\u%04x", c);
						o += b;
					}
					else
						o += char(c);
				}
				return o;
			}

			void look(std::ostringstream &o, const CharLook &l);

			void motion(std::ostringstream &o, const MapObject &obj)
			{
				const auto &p = obj.get_physics();
				const auto *remote = dynamic_cast<const OtherChar *>(&obj);
				o << ",\"foothold\":" << p.fhid << ",\"grounded\":" << (remote ? "null" : (p.onground ? "true" : "false"))
				  << ",\"physics_grounded\":" << (p.onground ? "true" : "false")
				  << ",\"vx\":" << p.hspeed << ",\"vy\":" << p.vspeed;
				if (remote)
				{
					const auto &m = remote->get_observed_movement();
					o << ",\"movement\":{\"stance_byte\":" << int(m.newstate) << ",\"foothold\":" << m.fh
					  << ",\"x\":" << m.xpos << ",\"y\":" << m.ypos << ",\"duration_ms\":" << m.duration << '}';
				}
				if (const auto *c = dynamic_cast<const Char *>(&obj))
				{
					o << ",\"action\":" << int(c->get_look().get_stance()) << ",\"stance\":" << int(c->get_observed_state())
					  << ",\"charge_skill\":" << c->get_charge() << ",\"charge_asset\":" << (c->has_charge_asset() ? "true" : "false")
					  << ",\"attacking\":" << (c->is_attacking() ? "true" : "false")
					  << ",\"climbing\":" << (c->is_climbing() ? "true" : "false") << ",\"chair\":" << c->get_chair() << ",\"chair_asset\":" << (c->has_chair_asset() ? "true" : "false");
					o << ",\"foreign_buffs\":[";
					bool comma = false;
					for (const auto &b : c->foreign_buffs) { if (comma) o << ','; comma = true; o << "{\"mask_bit\":" << b.first << ",\"value\":" << b.second << '}'; }
					o << ']';
				}
			}

			void objects(std::ostringstream &o, MapObjects *objs, const char *kind)
			{
				o << "\"" << kind << "\":[";
				bool first = true, truncated = false;
				size_t count = 0;
				for (auto &entry : *objs)
				{
					if (count++ == 512) { truncated = true; break; }
					auto &obj = entry.second;
					if (!first)
						o << ",";
					first = false;
					auto pos = obj->get_position();
					o << "{\"oid\":" << obj->get_oid() << ",\"x\":" << pos.x() << ",\"y\":" << pos.y();
					motion(o, *obj);
					if (auto *c = dynamic_cast<Char *>(obj.get()))
						{
						o << ",\"name\":\"" << esc(c->get_name()) << "\",\"level\":" << c->get_level() << ",\"look\":";
						look(o, c->get_look());
						}
					else if (auto *n = dynamic_cast<Npc *>(obj.get()))
						o << ",\"id\":" << n->get_npcid();
					else if (auto *m = dynamic_cast<Mob *>(obj.get()))
						{
						o << ",\"id\":" << m->get_mobid() << ",\"hp_percent\":";
						if (m->hp_is_known()) o << m->get_hp_percent(); else o << "null";
						o << ",\"status\":";
						auto status = mob_statuses.find(m->get_oid());
						if (status == mob_statuses.end()) o << "null"; else o << status->second;
						o << ",\"status_support\":\"spawn_records_only\",\"stance\":" << m->get_stance()
						  << ",\"dying\":" << (m->is_dying() ? "true" : "false");
					}
					else if (auto *drop = dynamic_cast<Drop *>(obj.get()))
					{
						o << ",\"owner\":" << drop->get_owner() << ",\"pickup_type\":" << drop->get_pickup_type()
						  << ",\"state\":" << drop->get_state() << ",\"quantity\":null";
						if (auto *item = dynamic_cast<ItemDrop *>(drop)) o << ",\"item_id\":" << item->get_itemid();
						else o << ",\"item_id\":null,\"mesos\":" << drop->get_amount();
					}
					else if (auto *reactor = dynamic_cast<Reactor *>(obj.get()))
						o << ",\"id\":" << reactor->get_id() << ",\"state\":" << reactor->get_state()
						  << ",\"event_type\":" << reactor->get_event_type()
						  << ",\"hittable\":" << (reactor->is_hittable() ? "true" : "false");
					o << "}";
				}
				o << "],\"" << kind << "_truncated\":" << (truncated ? "true" : "false");
			}
		}

		namespace
		{
			int32_t last_map = -1;
			std::map<std::string, std::set<int32_t>> known;

			void diff(const char *kind, MapObjects *objs)
			{
				std::set<int32_t> now;
				for (auto &entry : *objs)
					now.insert(entry.first);
				auto &prev = known[kind];
				for (int32_t oid : now)
					if (!prev.count(oid))
					{
						auto pos = objs->get(oid)->get_position();
						event("spawn", std::string(",\"kind\":\"") + kind + "\",\"oid\":" + std::to_string(oid)
										+ ",\"x\":" + std::to_string(pos.x()) + ",\"y\":" + std::to_string(pos.y()));
					}
				for (int32_t oid : prev)
					if (!now.count(oid))
					{
						if (std::string(kind) == "mob") mob_statuses.erase(oid);
						if (std::string(kind) == "char") motion_states.erase(oid);
						event("despawn", std::string(",\"kind\":\"") + kind + "\",\"oid\":" + std::to_string(oid));
					}
				prev = now;
			}

			void track_motion(const Char &c)
			{
				if (!trace || observations >= 4096 || (motion_states.size() >= 512 && !motion_states.count(c.get_oid()))) return;
				const auto &p = c.get_physics();
				auto state = std::make_tuple(int(c.get_observed_state()), int(p.fhid), p.onground);
				auto previous = motion_states.find(c.get_oid());
				if (previous != motion_states.end() && previous->second == state) return;
				motion_states[c.get_oid()] = state;
				++observations;
				std::ostringstream fields;
				fields << ",\"boundary\":\"model\",\"actor\":" << c.get_oid()
				  << ",\"map\":" << Stage::get().get_mapid() << ",\"x\":" << p.get_x() << ",\"y\":" << p.get_y();
				motion(fields, c);
				event("motion", fields.str());
			}

			void track_changes()
			{
				int32_t map = Stage::get().get_mapid();
				if (map != last_map)
				{
					last_map = map;
					known.clear();
					motion_states.clear();
					event("map", ",\"map\":" + std::to_string(map));
				}
				if (trace)
				{
					track_motion(Stage::get().get_player());
					for (const auto &entry : *Stage::get().get_chars().get_chars())
						if (auto *c = dynamic_cast<Char *>(entry.second.get())) track_motion(*c);
				}
				diff("char", Stage::get().get_chars().get_chars());
				diff("npc", Stage::get().get_npcs().get_npcs());
				diff("mob", Stage::get().get_mobs().get_mobs());
				diff("drop", Stage::get().get_drops().get_drops());
			}
		}

		namespace
		{
			bool dialog_open = false;
			int32_t dialog_npc = 0;
			int8_t dialog_type = -1;
			std::string dialog_text;

			struct PartyMember { int32_t id, job, level, channel, map; std::string name; };
			int32_t party_id = 0, party_leader = 0, invite_party = 0;
			std::string invite_from;
			std::vector<PartyMember> party_members;

			void party_json(std::ostringstream &o)
			{
				if (!party_id) { o << "null"; return; }
				o << "{\"id\":" << party_id << ",\"leader\":" << party_leader << ",\"members\":[";
				for (size_t i = 0; i < party_members.size(); i++)
					o << (i ? "," : "") << "{\"id\":" << party_members[i].id << ",\"name\":\"" << esc(party_members[i].name)
					  << "\",\"job\":" << party_members[i].job << ",\"level\":" << party_members[i].level
					  << ",\"channel\":" << party_members[i].channel << ",\"map\":" << party_members[i].map << "}";
				o << "]}";
			}

			void look(std::ostringstream &o, const CharLook &l)
			{
				o << "{\"skin\":" << l.get_skin_id() << ",\"hair\":" << l.get_hair_id()
				  << ",\"face\":" << l.get_face_id() << ",\"equips\":{";
				bool first = true;
				for (int16_t s = 1; s < EquipSlot::Id::LENGTH; s++)
				{
					int32_t id = l.get_equips().get_equip(static_cast<EquipSlot::Id>(s));
					if (id <= 0)
						continue;
					o << (first ? "" : ",") << "\"" << s << "\":" << id;
					first = false;
				}
				o << "}}";
			}

			void inventory(std::ostringstream &o, const Inventory &inv)
			{
				static const std::pair<const char *, InventoryType::Id> tabs[] = {
					{"equip", InventoryType::Id::EQUIP}, {"use", InventoryType::Id::USE},
					{"setup", InventoryType::Id::SETUP}, {"etc", InventoryType::Id::ETC},
					{"cash", InventoryType::Id::CASH}, {"equipped", InventoryType::Id::EQUIPPED}};
				o << "{\"meso\":" << inv.get_meso();
				for (auto &tab : tabs)
				{
					o << ",\"" << tab.first << "\":[";
					bool first = true;
					for (auto &s : inv.list_slots(tab.second))
					{
						o << (first ? "" : ",") << "{\"slot\":" << s[0] << ",\"id\":" << s[1] << ",\"count\":" << s[2];
						const auto &data = ItemData::get(s[1]);
						o << ",\"tooltip\":{\"name\":\"" << esc(data.get_name()) << "\",\"description\":\"" << esc(data.get_desc())
						  << "\",\"valid\":" << (data.is_valid() ? "true" : "false") << ",\"cash\":" << (data.is_cashitem() ? "true" : "false")
						  << ",\"untradable\":" << (data.is_untradable() ? "true" : "false") << "}";
						if (auto item = inv.get_item(tab.second, s[0]))
							o << ",\"flags\":" << item->get_flags() << ",\"expiry_raw\":" << item->get_expiration() << ",\"owner\":\"" << esc(item->get_owner()) << "\"";
						else if (auto eq = inv.get_equip(tab.second, s[0]))
						{
							o << ",\"flags\":" << eq->get_flags() << ",\"expiry_raw\":" << eq->get_expiration() << ",\"owner\":\"" << esc(eq->get_owner())
							  << "\",\"upgrade_slots\":" << int(eq->get_slots()) << ",\"upgrade_level\":" << int(eq->get_level()) << ",\"hammer\":" << eq->get_vicious() << ",\"stats\":{";
							bool comma = false;
							for (auto stat : EquipStat::values)
							{ o << (comma ? "," : "") << "\"" << EquipStat::names[stat] << "\":" << eq->get_stat(stat); comma = true; }
							o << "}";
						}
						else o << ",\"flags\":null,\"expiry_raw\":null,\"owner\":null"; // pet metadata is separate
						o << "}";
						first = false;
					}
					o << "]";
				}
				o << "}";
			}

			void dialog(std::ostringstream &o)
			{
				o << "{\"npc\":" << dialog_npc << ",\"msgtype\":" << int(dialog_type) << ",\"text\":\"" << esc(dialog_text)
				  << "\",\"choices\":[";
				// Selection menus mark options as #L<index>#label#l
				bool first = true;
				size_t pos = 0;
				while ((pos = dialog_text.find("#L", pos)) != std::string::npos)
				{
					size_t close = dialog_text.find('#', pos + 2);
					if (close == std::string::npos)
						break;
					std::string index = dialog_text.substr(pos + 2, close - pos - 2);
					if (index.empty() || index.size() > 9 || index.find_first_not_of("0123456789") != std::string::npos)
					{
						pos += 2;
						continue;
					}
					size_t end = dialog_text.find("#l", close + 1);
					std::string label = dialog_text.substr(close + 1, end == std::string::npos ? std::string::npos : end - close - 1);
					o << (first ? "" : ",") << "{\"index\":" << std::stoi(index) << ",\"label\":\"" << esc(label) << "\"}";
					first = false;
					pos = close;
				}
				o << "]";
                if (auto talk = UI::get().get_element<UINpcTalk>()) talk->write_input_state(o);
                o << "}";
            }

            void act_result(const std::string &id, const std::string &name, bool ok, const std::string &error, const std::string &fields = "")
			{
				event("act", ",\"id\":\"" + esc(id) + "\",\"act\":\"" + esc(name) + "\",\"ok\":" + (ok ? "true" : "false")
							 + (ok ? "" : ",\"error\":\"" + esc(error) + "\"") + fields);
			}

			struct Hold
			{
				bool active = false;
				int32_t action = 0;
				std::string id, name;
				std::chrono::steady_clock::time_point until;
				bool jump_pending = false;
				std::chrono::steady_clock::time_point jump_at;
			} hold;
			std::string canceled_id;

			void key_tap(int32_t action)
			{
				Stage::get().send_key(KeyType::Id::ACTION, action, true);
				Stage::get().send_key(KeyType::Id::ACTION, action, false);
			}

			void poll_hold()
			{
				if (hold.active && std::chrono::steady_clock::now() >= hold.until)
				{
					Stage::get().send_key(KeyType::Id::ACTION, hold.action, false);
					hold.active = false;
					act_result(hold.id, hold.name, true, "");
				}
				else if (hold.active && hold.jump_pending && std::chrono::steady_clock::now() >= hold.jump_at)
				{
					hold.jump_pending = false;
					key_tap(KeyAction::Id::JUMP);
				}
			}

			void cancel_hold(const std::string &reason)
			{
				if (!hold.active) return;
				Stage::get().send_key(KeyType::Id::ACTION, hold.action, false);
				act_result(hold.id, hold.name, false, reason);
				hold.active = false;
			}

			void poll_cancel(const char *d)
			{
				std::string path = std::string(d) + "/cancel.txt";
				std::string run = path + ".run";
				if (std::rename(path.c_str(), run.c_str()) != 0) return;
				std::string id;
				{ std::ifstream f(run); f >> id; }
				std::remove(run.c_str());
				if (hold.active && id == hold.id)
					cancel_hold("request canceled by caller");
				else
					canceled_id = id;
			}

			// One request per file: "<id> <act> <arg>" written atomically by ms_headless.py.
			void poll_act(const char *d)
			{
				std::string path = std::string(d) + "/act.txt";
				std::string run = path + ".run";
				if (std::rename(path.c_str(), run.c_str()) != 0)
					return;
				std::string id, name, arg;
				{
					std::ifstream f(run);
					f >> id >> name;
					std::getline(f, arg);
					if (!arg.empty() && arg[0] == ' ')
						arg.erase(0, 1);
				}
				std::remove(run.c_str());
				if (id == canceled_id)
				{
					canceled_id.clear();
					return act_result(id, name, false, "request canceled by caller");
				}

				if (name == "map-geometry")
				{
					if (!arg.empty()) return act_result(id, name, false, "expected no arguments");
					if (screen != "in_game") return act_result(id, name, false, "field is not active");
					// Export once per request, outside the recurring entity snapshot.
					std::ostringstream o;
					o << ",\"geometry\":{\"map\":" << Stage::get().get_mapid()
					  << ",\"field_entry_seq\":" << field_entry_seq << ",\"footholds\":[";
					std::vector<uint16_t> ids;
					const auto &footholds = Stage::get().get_footholds().get_footholds();
					for (const auto &entry : footholds) ids.push_back(entry.first);
					std::sort(ids.begin(), ids.end());
					bool comma = false;
					for (auto fid : ids)
					{
						const auto &f = footholds.at(fid);
						o << (comma ? "," : "") << "{\"id\":" << f.id() << ",\"layer\":" << int(f.layer())
						  << ",\"prev\":" << f.prev() << ",\"next\":" << f.next()
						  << ",\"x1\":" << f.x1() << ",\"y1\":" << f.y1()
						  << ",\"x2\":" << f.x2() << ",\"y2\":" << f.y2() << '}';
						comma = true;
					}
					o << "],\"climbables\":["; comma = false;
					for (const auto &l : Stage::get().get_ladders())
					{
						o << (comma ? "," : "") << "{\"ladder\":" << (l.is_ladder() ? "true" : "false")
						  << ",\"x\":" << l.get_x() << ",\"y1\":" << l.get_y1() << ",\"y2\":" << l.get_y2() << '}';
						comma = true;
					}
					o << "]}";
					return act_result(id, name, true, "", o.str());
				}

				if (name == "observe")
				{
					if (arg == "stats")
					{
						event("observation_metrics", ",\"progress\":" + std::to_string(progress)
							+ ",\"render_frame\":" + std::to_string(render_frame)
							+ ",\"exports\":" + std::to_string(export_count) + ",\"export_us\":" + std::to_string(export_us)
							+ ",\"export_max_us\":" + std::to_string(export_max_us));
						return act_result(id, name, true, "");
					}
					if (arg == "pause" || arg == "resume")
					{
						export_paused = arg == "pause";
					dirty = true;
						return act_result(id, name, true, "");
					}
					if (arg != "on" && arg != "off") return act_result(id, name, false, "expected on, off, pause, resume or stats");
					trace = arg == "on";
					motion_states.clear();
					observations = dropped = export_us = export_max_us = export_count = 0;
					return act_result(id, name, true, "");
				}
				if (name == "login-notice")
				{
					if (arg != "confirm" && arg != "cancel")
						return act_result(id, name, false, "expected confirm or cancel");
					auto notice = UI::get().get_element_checked<UILoginNotice>();
					if (screen == "in_game" || !notice || !notice->is_active()
						|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM))
						return act_result(id, name, false, "login notice is not available");
					bool ok = notice->respond(arg == "confirm");
					dirty = true;
					return act_result(id, name, ok, ok ? "" : "notice has no cancel button");
				}
				if (name == "pic-submit" || name == "pic-cancel")
				{
					auto keyboard = UI::get().get_element<UISoftKey>();
					bool modal = (UI::get().is_element_active(UIElement::Type::LOGINNOTICE) || UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM));
					if (screen != "character_select" || !keyboard || !keyboard->is_active()
						|| modal)
						return act_result(id, name, false, "PIC keyboard is not available");
					if (name == "pic-cancel" && !arg.empty())
						return act_result(id, name, false, "expected no arguments");
					bool ok = name == "pic-cancel" ? keyboard->cancel() : keyboard->submit_digits(arg);
					arg.clear();
					dirty = true;
					return act_result(id, name, ok, ok ? "" : (name == "pic-cancel" ? "PIC keyboard has no cancel callback" : "expected 6-16 ASCII digits"));
				}
				// Do not change the underlying selection while a modal owns input.
				if (name == "view-all" || name == "world-choose" || name == "channel-choose" || name == "character-choose" || name == "character-enter" || name == "character-back" || name == "character-delete")
				{
					auto keyboard = UI::get().get_element<UISoftKey>();
					bool modal = (UI::get().is_element_active(UIElement::Type::LOGINNOTICE) || UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM));
					if ((keyboard && keyboard->is_active()) || modal)
						return act_result(id, name, false, "dismiss the active login dialog before selecting");
				}

				if (name == "view-all")
				{
					if (!arg.empty()) return act_result(id, name, false, "expected no arguments");
					if ((screen != "world_select" && screen != "channel_select" && screen != "character_select") || !ViewAllCharHandler::request())
						return act_result(id, name, false, "world selection is not available");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "world-choose" || name == "channel-choose")
				{
					auto selection = UI::get().get_element<UIWorldSelect>();
					if ((screen != "world_select" && screen != "channel_select") || !selection || !selection->is_active())
						return act_result(id, name, false, "world selection is not active");
					int value;
					try { size_t n; value = std::stoi(arg, &n); if (n != arg.size()) throw 0; }
					catch (...) { return act_result(id, name, false, "expected numeric selection"); }
					bool ok = name == "world-choose" ? selection->select_world(value) : selection->select_channel(value);
					if (!ok) return act_result(id, name, false, "selection is unavailable");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "character-back")
				{
					if (!arg.empty()) return act_result(id, name, false, "expected no arguments");
					auto selection = UI::get().get_element<UICharSelect>();
					if (screen != "character_select" || !selection || !selection->return_to_world())
						return act_result(id, name, false, "character selection cannot return to world selection");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "character-delete")
				{
					auto selection = UI::get().get_element<UICharSelect>();
					if (screen != "character_select" || !selection || !selection->delete_character(arg))
						return act_result(id, name, false, "expected the exact selected character name");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "character-enter")
				{
					if (!arg.empty()) return act_result(id, name, false, "expected no arguments");
					auto selection = UI::get().get_element<UICharSelect>();
					if (screen != "character_select" || !selection || !selection->enter_character())
						return act_result(id, name, false, "selected character is not available");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "character-choose")
				{
					auto selection = UI::get().get_element<UICharSelect>();
					if (screen != "character_select" || !selection || !selection->is_active())
						return act_result(id, name, false, "character selection is not active");
					if (!selection->select_character(arg))
						return act_result(id, name, false, "no such character name");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "cash-exit")
				{
					if (!arg.empty() || screen != "cash_shop" || !CashShopState::get().pending.empty())
						return act_result(id, name, false, "Cash Shop is unavailable or a request is pending");
					auto shop = UI::get().get_element<UICashShop>();
					bool ok = shop && shop->request_exit();
					return act_result(id, name, ok, ok ? "" : "Cash Shop exit unavailable");
				}
				if (name.compare(0, 5, "cash-") == 0 && name != "cash-enter" && name != "cash-item-open" && name != "cash-item-use")
				{
					bool ok = screen == "cash_shop" && CashShopState::request(name, arg);
					return act_result(id, name, ok, ok ? "" : "invalid Cash Shop request, unavailable inventory or request pending");
				}
				if (name == "cash-enter")
				{
					if (!arg.empty() || screen != "in_game" || hold.active || dialog_open || Stage::get().get_player().get_stats().get_stat(MapleStat::HP) <= 0)
						return act_result(id, name, false, "field input is unavailable");
					EnterCashShopPacket().dispatch();
					return act_result(id, name, true, "");
				}
				if (screen != "in_game")
					return act_result(id, name, false, "action requires an in-game screen");

				Inventory &inv = Stage::get().get_player().get_inventory();
				auto num = [&](int32_t &out) {
					try { size_t n; out = std::stoi(arg, &n); return n == arg.size(); } catch (...) { return false; }
				};
				int32_t n = 0;

				auto quit = UI::get().get_element<UIQuit>();
				if (name == "logout")
				{
					if (arg != "open" && arg != "cancel" && arg != "confirm")
						return act_result(id, name, false, "expected open, cancel or confirm");
					if (arg == "open")
					{
						auto cashshop = UI::get().get_element<UICashShop>();
						if (hold.active || dialog_open || (cashshop && cashshop->is_active())
							|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE)
							|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM))
							return act_result(id, name, false, "finish the active move or dialog first");
						if (!quit || !quit->is_active()) UI::get().emplace<UIQuit>(Stage::get().get_player().get_stats());
					}
					else if (!quit || !quit->respond(arg == "confirm"))
						return act_result(id, name, false, "logout dialog is not active");
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (quit && quit->is_active())
					return act_result(id, name, false, "dismiss the active logout dialog first");

                if(name=="mts-enter" || name=="mts-action"){
 if(name=="mts-enter"){if(screen!="in_game" || !arg.empty())return act_result(id,name,false,"enter a field first");OutPacket(0x9c).dispatch();return act_result(id,name,true,"");}
 auto panel=UI::get().get_element<UIMTS>();if(screen!="mts" || !panel || !panel->is_active())return act_result(id,name,false,"MTS is not open");
 std::istringstream input(arg);std::string action,tail;int index=-1;input>>action;if(input>>index){if(input>>tail)return act_result(id,name,false,"unexpected arguments");}else if(!input.eof())return act_result(id,name,false,"expected listing index");
 bool ok=panel->act(action,index);dirty=true;return act_result(id,name,ok,ok?"":MTSState::get().status);
 }
                if (name == "market-open" || name == "market-action")
                {
                    if (screen != "in_game") return act_result(id, name, false, "enter a field first");
                    if (name == "market-open") { UI::get().emplace<UIMarket>(); return act_result(id,name,true,""); }
                    auto panel=UI::get().get_element<UIMarket>();
                    if (!panel || !panel->is_active()) return act_result(id,name,false,"open the market window first");
                    std::istringstream input(arg); std::string action, values; input >> action; std::getline(input >> std::ws,values);
                    bool ok=panel->act(action,values); dirty=true; return act_result(id,name,ok,ok?"":MarketState::get().status);
                }
				if (name == "social-open")
				{
					if (screen != "in_game") return act_result(id, name, false, "enter a field first");
					if (arg != "buddy" && arg != "messenger" && arg != "notes" && arg != "party" && arg != "family" && arg != "guide" && arg != "newyear" && arg != "marriage")
						return act_result(id, name, false, "expected buddy, messenger, notes, party, family, guide, newyear or marriage");
					UI::get().emplace<UISocial>(arg);
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "social-action")
				{
					auto panel = UI::get().get_element<UISocial>();
					if (screen != "in_game" || !panel || !panel->is_active())
						return act_result(id, name, false, "open the social window first");
					std::istringstream input(arg);
					std::string action, target, text;
					input >> action >> target;
					std::getline(input >> std::ws, text);
					bool ok = panel->act(action, target == "-" ? "" : target, text);
					dirty = true;
					return act_result(id, name, ok, ok ? "" : SocialState::get().status);
				}
				if (name == "chat-send")
				{
					if (screen != "in_game") return act_result(id, name, false, "enter a field first");
					const auto &text = arg;
					auto chat = UI::get().get_element<UIChatBar>();
					if (!chat || !chat->submit(text)) return act_result(id, name, false, "invalid chat request; see chat feedback");
					return act_result(id, name, true, "");
				}

				if (name == "channel-change")
				{
					auto cashshop = UI::get().get_element<UICashShop>();
					if (hold.active || dialog_open || (cashshop && cashshop->is_active())
						|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE)
						|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM))
						return act_result(id, name, false, "finish the active move or dialog first");
					if (!num(n) || !UIChannel::request_channel(n))
						return act_result(id, name, false, "channel unavailable or already current");
					dirty = true;
					return act_result(id, name, true, "");
				}

				if (name == "cash-item-open" || name == "cash-item-use" || name == "reset-open" || name == "ap-reset" || name == "sp-reset" || name == "pickup" || name == "sit" || name == "stand" || name == "chair" || name == "item-move" || name == "item-drop" || name == "item-split" || name == "scripted-item" || name.rfind("skill-", 0) == 0 || name == "buff-cancel")
				{
					auto cash = UI::get().get_element<UICashShop>();
					if (hold.active || dialog_open || (cash && cash->is_active())
						|| UI::get().is_element_active(UIElement::Type::NOTICE)
						|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE)
						|| UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM))
						return act_result(id, name, false, "finish the active move or dialog first");
				}

				if (name == "reset-open" || name == "cash-item-open")
				{
					if (!num(n) || n < 1 || n > 32767) return act_result(id, name, false, "expected cash slot");
					int32_t item = inv.get_item_id(InventoryType::CASH, n);
					if (!UICashItemDialog::supports(item) || (name == "reset-open" && item / 10000 != 505)) return act_result(id, name, false, "supported cash item required");
					UI::get().emplace<UICashItemDialog>(n, item);
					dirty = true;
					return act_result(id, name, true, "");
				}
				if (name == "cash-item-use")
				{
					std::istringstream args(arg); int32_t slot; std::string values;
					if (!(args >> slot) || slot < 1 || slot > 32767) return act_result(id, name, false, "expected cash slot and target values");
					std::getline(args, values);
					int32_t item = inv.get_item_id(InventoryType::CASH, slot);
					if (item / 10000 == 505) return act_result(id, name, false, "use AP/SP reset action");
					auto error = UICashItemDialog::request(slot, item, values);
					if (!error.empty()) return act_result(id, name, false, error);
					dirty = true; return act_result(id, name, true, "");
				}
				if (name == "ap-reset" || name == "sp-reset")
				{
					std::istringstream args(arg);
					int32_t slot; std::string to, from, extra;
					if (!(args >> slot >> to >> from) || (args >> extra) || slot < 1 || slot > 32767)
						return act_result(id, name, false, "expected cash slot, destination and source");
					const int32_t item = inv.get_item_id(InventoryType::CASH, slot);
					if ((name == "ap-reset" && item != 5050000) || (name == "sp-reset" && (item <= 5050000 || item / 10000 != 505)))
						return act_result(id, name, false, "matching AP/SP Reset required");
					auto error = UICashItemDialog::request(slot, item, to + " " + from);
					if (!error.empty()) return act_result(id, name, false, error);
					dirty = true;
					return act_result(id, name, true, "");
				}

				if (name == "skill-use" || name == "skill-down" || name == "skill-up" || name == "skill-cancel" || name == "buff-cancel" || name == "skill-learn" || name == "skill-assign")
				{
					auto &combat = Stage::get().get_combat();
					auto &p = Stage::get().get_player();
					if (name == "skill-cancel")
					{ if (!arg.empty() || !combat.cancel_skill()) return act_result(id, name, false, "no held skill"); }
					else if (name == "skill-assign")
					{
						std::istringstream input(arg); int key = 0, skill = 0; std::string extra;
						if (!(input >> key >> skill) || input >> extra || key < 2 || key > 88 || p.get_skilllevel(skill) <= 0 || SkillData::get(skill).is_passive()) return act_result(id, name, false, "expected bindable Maple key 2-88 and learned active skill");
						ChangeKeyMapPacket({{static_cast<KeyConfig::Key>(key), KeyType::SKILL, skill}}).dispatch();
						UI::get().get_keyboard().assign(key, KeyType::SKILL, skill);
					}
					else
					{
						if (!num(n) || n <= 0) return act_result(id, name, false, "expected positive skill id");
						if (name == "skill-learn")
						{
							const auto &data = SkillData::get(n);
							if (p.get_stats().get_stat(MapleStat::SP) <= 0 || !p.get_stats().get_job().can_use(n) || p.get_skilllevel(n) >= data.get_masterlevel()) return act_result(id, name, false, "no SP or skill unavailable/maxed");
							for (const auto &req : data.get_reqskills()) if (p.get_skilllevel(req.first) < req.second) return act_result(id, name, false, "skill prerequisite missing");
							SpendSpPacket(n).dispatch();
						}
						else if (name == "buff-cancel")
						{
							bool active = false;
							for (const auto &b : p.get_buffs().values()) if (b.skillid == n) active = true;
							if (!active) return act_result(id, name, false, "buff is not active");
							CancelSkillPacket(n).dispatch();
						}
						else if (name == "skill-use")
						{ if (combat.held_skill() || !combat.use_move(n)) return act_result(id, name, false, "skill unavailable, unlearned or insufficient resources"); }
						else if (!combat.skill_key(n, name == "skill-down")) return act_result(id, name, false, "skill key rejected");
					}
				}
				else if (name == "pickup")
				{
					if (!arg.empty() || !Stage::get().request_pickup()) return act_result(id, name, false, "no settled drop within pickup range");
				}
				else if (name == "sit")
				{
					if (!arg.empty() || Stage::get().get_player().is_sitting()) return act_result(id, name, false, "expected standing player and no arguments");
					Stage::get().send_key(KeyType::ACTION, KeyAction::SIT, true);
					Stage::get().send_key(KeyType::ACTION, KeyAction::SIT, false);
					if (!Stage::get().get_player().is_sitting()) return act_result(id, name, false, "no map seat in reach");
				}
				else if (name == "stand")
				{
					auto &player = Stage::get().get_player();
					if (!arg.empty() || !player.is_sitting()) return act_result(id, name, false, "player is not sitting");
					if (player.get_chair()) player.stand_up();
					else { Stage::get().send_key(KeyType::ACTION, KeyAction::UP, true); Stage::get().send_key(KeyType::ACTION, KeyAction::UP, false); }
				}
				else if (name == "item-move" || name == "item-drop" || name == "item-split")
				{
					std::istringstream args(arg); std::string tab, extra; int source = 0, dest = 0, qty = 0;
					args >> tab >> source;
					if (name == "item-drop") args >> qty; else args >> dest >> qty;
					InventoryType::Id type = tab == "equip" ? InventoryType::EQUIP : tab == "use" ? InventoryType::USE : tab == "setup" ? InventoryType::SETUP : tab == "etc" ? InventoryType::ETC : tab == "cash" ? InventoryType::CASH : InventoryType::NONE;
					if (name == "item-split") return act_result(id, name, false, "partial-stack moves are unsupported by the v83 server; move a whole stack or drop a quantity");
					if (!args || (args >> extra) || source < 1 || source > 32767 || dest < 0 || dest > 32767 || qty < 1 || qty > 32767 || (name == "item-move" && dest == 0)
						|| !inv.request_move(type, source, dest, qty)) return act_result(id, name, false, "invalid inventory move or protected drop");
				}
				else if (name == "chair" || name == "scripted-item")
				{
					if (dialog_open || !num(n) || n < 1 || n > 32767 || !Stage::get().get_player().use_inventory_item(name == "chair" ? InventoryType::SETUP : InventoryType::USE, n, name == "scripted-item"))
						return act_result(id, name, false, "invalid item slot or player state");
				}
				else if (name == "npc-talk")
				{
					if (!num(n)) return act_result(id, name, false, "expected npc oid");
					if (!Stage::get().get_npcs().get_npcs()->contains(n)) return act_result(id, name, false, "no such npc oid");
					TalkToNPCPacket(n).dispatch();
				}
				else if (name == "npc-choose")
				{
					auto talk = UI::get().get_element<UINpcTalk>();
					if (!talk || !dialog_open) return act_result(id, name, false, "no open npc dialog");
					int32_t v;
					if (dialog_type == 4)
					{
						// selection menu: only an option index or close
						if (arg == "close") v = -1;
						else if (!num(v) || v < 0) return act_result(id, name, false, "expected close|<option index>");
					}
					else if (arg == "next" || arg == "yes" || arg == "ok") v = 1;
					else if (arg == "no" || arg == "prev") v = 0;
					else if (arg == "close") v = -1;
					else if (!num(v) || v < -1 || v > 1) return act_result(id, name, false, "expected next|yes|ok|no|prev|close|-1|0|1");
					if (!talk->headless_respond(v)) return act_result(id, name, false, "unsupported dialog type");
				}
				else if (name == "npc-input") {
                    auto talk = UI::get().get_element<UINpcTalk>();
                    if (!dialog_open || !talk || !talk->submit_input(arg))
                        return act_result(id, name, false, "input does not match the received NPC dialog or range");
                }
                else if (name == "use-item")
				{
					if (!num(n)) return act_result(id, name, false, "expected use slot");
					int32_t itemid = inv.get_item_id(InventoryType::Id::USE, n);
					if (!itemid) return act_result(id, name, false, "empty use slot");
					if (n < 1 || n > 32767 || !Stage::get().get_player().use_inventory_item(InventoryType::USE, n, false)) return act_result(id, name, false, "item requires another use path or player is busy");
				}
				else if (name == "equip")
				{
					if (!num(n)) return act_result(id, name, false, "expected equip slot");
					int32_t itemid = inv.get_item_id(InventoryType::Id::EQUIP, n);
					if (!itemid) return act_result(id, name, false, "empty equip slot");
					EquipSlot::Id dest = inv.find_equipslot(itemid);
					if (dest == EquipSlot::Id::NONE) return act_result(id, name, false, "item not equippable");
					EquipItemPacket(n, dest).dispatch();
				}
				else if (name == "unequip")
				{
					if (!num(n)) return act_result(id, name, false, "expected equipped slot");
					if (!inv.get_item_id(InventoryType::Id::EQUIPPED, n)) return act_result(id, name, false, "nothing equipped there");
					int16_t free_slot = inv.find_free_slot(InventoryType::Id::EQUIP);
					if (!free_slot) return act_result(id, name, false, "equip tab full");
					UnequipItemPacket(n, free_slot).dispatch();
				}
				else if (name == "window")
				{
					std::istringstream args(arg);
					std::string window, operation, extra;
					args >> window >> operation;
					if ((args >> extra) || (operation != "open" && operation != "close"))
						return act_result(id, name, false, "expected <stats|items|equipment> <open|close>");
					bool open = operation == "open";
					if (window == "stats")
					{
						auto current = UI::get().get_element<UIStatsInfo>();
						if (open && (!current || !current->is_active()))
							UI::get().emplace<UIStatsInfo>(Stage::get().get_player().get_stats());
						else if (!open && current && current->is_active())
							UI::get().remove(UIElement::Type::STATSINFO);
					}
					else if (window == "items")
					{
						auto current = UI::get().get_element<UIItemInventory>();
						if (open && (!current || !current->is_active()))
							UI::get().emplace<UIItemInventory>(inv);
						else if (!open && current && current->is_active())
							UI::get().remove(UIElement::Type::ITEMINVENTORY);
					}
					else if (window == "equipment")
					{
						auto current = UI::get().get_element<UIEquipInventory>();
						if (open && (!current || !current->is_active()))
							UI::get().emplace<UIEquipInventory>(inv);
						else if (!open && current && current->is_active())
							UI::get().remove(UIElement::Type::EQUIPINVENTORY);
					}
					else
						return act_result(id, name, false, "expected stats|items|equipment");
				}
				else if (name == "move")
				{
					// "<left|right|up|down> <ms>": hold the key, result is reported on release
					std::istringstream a(arg);
					std::string dirname;
					int32_t ms = 0;
					a >> dirname >> ms;
					int32_t action = dirname == "left" ? KeyAction::Id::LEFT : dirname == "right" ? KeyAction::Id::RIGHT
									: dirname == "up" ? KeyAction::Id::UP : dirname == "down" ? KeyAction::Id::DOWN : 0;
					if (!action || ms <= 0 || ms > 30000) return act_result(id, name, false, "expected left|right|up|down <ms 1-30000>");
					if (hold.active) return act_result(id, name, false, "a move is already running");
					Stage::get().send_key(KeyType::Id::ACTION, action, true);
					hold = {true, action, id, name, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)};
					return;
				}
				else if (name == "party-create" || name == "party-leave" || name == "party-invite" || name == "party-accept" || name == "party-deny" || name == "party-kick" || name == "party-leader" || name == "party-search-start" || name == "party-search-stop")
				{
					auto error = party_request(name, arg, arg);
					if (!error.empty()) return act_result(id, name, false, error);
				}
				else if (name == "jump")
				{
					if (hold.active) return act_result(id, name, false, "finish the active move first");
					if (arg == "left" || arg == "right")
					{
						auto &player = Stage::get().get_player();
						if (!player.get_phobj().onground || player.is_attacking() || player.get_stats().get_stat(MapleStat::Id::HP) <= 0)
							return act_result(id, name, false, "directional jump requires a living player standing on a foothold");
						const auto action = arg == "left" ? KeyAction::Id::LEFT : KeyAction::Id::RIGHT;
						const auto now = std::chrono::steady_clock::now();
						Stage::get().send_key(KeyType::Id::ACTION, action, true);
						// Build normal walking momentum before Jump, without overlapping
						// native focus-dependent input or bypassing collision physics.
						hold = {true, action, id, name, now + std::chrono::milliseconds(1000), true, now + std::chrono::milliseconds(150)};
						return;
					}
					if (arg == "up")
					{
						// Keep Up pressed through the jump so normal airborne ladder
						// detection can grab a rope that hangs above the player.
						Stage::get().send_key(KeyType::Id::ACTION, KeyAction::Id::UP, true);
						key_tap(KeyAction::Id::JUMP);
						hold = {true, KeyAction::Id::UP, id, name, std::chrono::steady_clock::now() + std::chrono::milliseconds(1000)};
						return;
					}
					if (!arg.empty() && arg != "down") return act_result(id, name, false, "expected empty argument, down, up, left or right");
					if (arg == "down")
					{
						auto &player = Stage::get().get_player();
						if (!player.get_phobj().onground || !player.get_phobj().enablejd || player.is_attacking())
							return act_result(id, name, false, "cannot jump down from this foothold");
						Stage::get().send_key(KeyType::Id::ACTION, KeyAction::Id::DOWN, true);
						key_tap(KeyAction::Id::JUMP);
						Stage::get().send_key(KeyType::Id::ACTION, KeyAction::Id::DOWN, false);
					}
					else key_tap(KeyAction::Id::JUMP);
				}
				else if (name == "portal")
					key_tap(KeyAction::Id::UP);
				else if (name == "attack")
					key_tap(KeyAction::Id::ATTACK);
				else
					return act_result(id, name, false, "unknown act");

				act_result(id, name, true, "");
			}
		}

		std::vector<std::string> party_rows()
		{
			std::vector<std::string> rows;
			for (const auto &m : party_members) rows.push_back(m.name);
			return rows;
		}

		std::string party_request(const std::string &action, const std::string &target, const std::string &options)
		{
			if (screen != "in_game") return "Enter the game first.";
			class Request : public OutPacket
			{
			public:
				Request(int opcode) : OutPacket(opcode) {}
				using OutPacket::write_byte;
				using OutPacket::write_int;
				using OutPacket::write_string;
			};
			if (action == "party-create") CreatePartyPacket().dispatch();
			else if (action == "party-leave") LeavePartyPacket().dispatch();
			else if (action == "party-invite")
			{
				if (target.empty() || target.size() > 13 || target.find(' ') != std::string::npos)
					return "Enter a character name (1-13 characters).";
				InviteToPartyPacket(target).dispatch();
			}
			else if (action == "party-accept" || action == "party-deny")
			{
				if (!invite_party) return "No pending party invitation.";
				if (action == "party-accept") JoinPartyPacket(invite_party).dispatch();
				else
				{
					Request p(0x7d); p.write_byte(0); p.write_string(invite_from); p.dispatch();
				}
				invite_party = 0; invite_from.clear();
			}
			else if (action == "party-kick" || action == "party-leader")
			{
				if (party_leader != Stage::get().get_player().get_oid()) return "Only the party leader can do this.";
				auto member = std::find_if(party_members.begin(), party_members.end(), [&](const auto &m) { return m.name == target; });
				if (member == party_members.end() || member->id == party_leader) return "Select another received party member.";
				if (action == "party-kick") ExpelFromPartyPacket(member->id).dispatch();
				else ChangePartyLeaderPacket(member->id).dispatch();
			}
			else if (action == "party-search-start")
			{
				if (!party_id || party_leader != Stage::get().get_player().get_oid()) return "Create a party and become its leader first.";
				int min = 0, max = 0; std::string extra;
				std::istringstream input(options);
				if (!(input >> min >> max) || (input >> extra) || min < 1 || max > 200 || max < min || max - min > 30)
					return "Search options: minLevel maxLevel (all jobs); levels 1-200, range at most 30.";
				Request p(0xde); p.write_int(min); p.write_int(max); p.write_int(6); p.write_int(0x7fffffff); p.dispatch();
			}
			else if (action == "party-search-stop") { Request(0xdf).dispatch(); }
			else return "Unknown party action.";
			SocialState::get().changed(action + " requested; wait for the server response.");
			dirty = true;
			return {};
		}

		void on_party(InPacket &recv)
		{
			int8_t mode = recv.read_byte();
			SocialState::get().changed("Party response " + std::to_string(int(mode)));
			auto roster = [&]() {
				PartyMember m[6];
				for (auto &x : m) x.id = recv.read_int();
				for (auto &x : m) x.name = recv.read_padded_string(13);
				for (auto &x : m) x.job = recv.read_int();
				for (auto &x : m) x.level = recv.read_int();
				for (auto &x : m) x.channel = recv.read_int();
				party_leader = recv.read_int();
				for (auto &x : m) x.map = recv.read_int();
				party_members.clear();
				for (auto &x : m)
					if (x.id) party_members.push_back(x);
				event("party", ",\"party\":" + [&] { std::ostringstream o; party_json(o); return o.str(); }());
			};
			if (mode == 4)
			{
				invite_party = recv.read_int();
				invite_from = recv.read_string();
				SocialState::get().changed("Party invitation from " + invite_from);
				event("party_invite", ",\"party\":" + std::to_string(invite_party) + ",\"from\":\"" + esc(invite_from) + "\"");
			}
			else if (mode == 8)
			{
				party_id = recv.read_int();
				party_leader = Stage::get().get_player().get_oid();
				event("party_created", ",\"party\":" + std::to_string(party_id));
			}
			else if (mode == 7 || mode == 0x0c || mode == 0x0f)
			{
				int32_t pid = recv.read_int();
				if (mode == 0x0c)
				{
					int32_t target = recv.read_int();
					if (!recv.read_byte())
					{
						// disband (second form) clears the party
						party_id = 0; party_leader = 0; party_members.clear();
						event("party", ",\"party\":null");
						return;
					}
					recv.skip_byte();
					recv.skip_string();
					if (target == Stage::get().get_player().get_oid())
					{
						// leave/expel: the departing player is sent the remaining roster
						party_id = 0; party_leader = 0; party_members.clear();
						event("party", ",\"party\":null");
						return;
					}
				}
				else if (mode == 0x0f)
					recv.skip_string();
				party_id = pid;
				roster();
			}
			else if (mode == 23)
			{
				SocialState::get().changed(recv.read_string() + " declined the party invitation.");
			}
			else if (mode == 0x1b)
			{
				party_leader = recv.read_int();
				event("party", ",\"party\":" + [&] { std::ostringstream o; party_json(o); return o.str(); }());
			}
			else
				event("party_message", ",\"mode\":" + std::to_string(int(mode)));
		}

		void set_dialog(int32_t npcid, int8_t type, const std::string &text)
		{
			dialog_open = true;
			dialog_npc = npcid;
			dialog_type = type;
			dialog_text = text;
			dirty = true;
			event("npc_dialog", ",\"npc\":" + std::to_string(npcid) + ",\"msgtype\":" + std::to_string(type) + ",\"text\":\"" + esc(text) + "\"");
		}

		void clear_dialog()
		{
			if (dialog_open)
				event("npc_dialog_closed");
			dialog_open = false;
			dirty = true;
		}

		std::string escape(const std::string &s)
		{
			return esc(s);
		}

		void event(const char *type, const std::string &fields)
		{
			const char *d = dir();
			if (!d)
				return;

			static int64_t seq = 0;
			static size_t bytes = 0;
			const size_t cap = 1 << 20;

			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();
			std::ostringstream o;
			o << "{\"session_id\":\"" << esc(session_id) << "\",\"seq\":" << ++seq
			  << ",\"ts\":" << ms << ",\"type\":\"" << type << "\"" << fields << "}\n";
			std::string line = o.str();

			std::string path = std::string(d) + "/events.jsonl";
			if (bytes + line.size() > cap)
			{
				std::rename(path.c_str(), (path + ".1").c_str());
				bytes = 0;
			}
			std::ofstream f(path, std::ios::app);
			f << line;
			bytes += line.size();
		}

		void packet_error(int32_t opcode, size_t packet_length, size_t parser_offset,
						  const std::string &reason, bool handled)
		{
			event("packet_error", ",\"opcode\":" + std::to_string(opcode)
				+ ",\"length\":" + std::to_string(packet_length)
				+ ",\"offset\":" + std::to_string(parser_offset)
				+ ",\"handled\":" + (handled ? "true" : "false")
				+ ",\"reason\":\"" + esc(reason) + "\"");
		}

		void set_screen(const char *name)
		{
			// A login screen starts a fresh account session. Keep the process-wide
			// event/field sequence, but never carry observations into a later login.
			if (screen != name && std::string(name) == "login")
			{
				ViewAllCharHandler::reset();
				buff_expiry.clear();
				mob_statuses.clear();
				cancel_hold("returned to login");
				clear_dialog();
				dialog_npc = 0;
				dialog_type = -1;
				dialog_text.clear();
				party_id = 0;
				party_leader = 0;
				party_members.clear();
				invite_party = 0;
				invite_from.clear();
				last_map = -1;
				known.clear();
				CashShopState::get() = CashShopState{};
                SocialState::get() = SocialState{};
                MarketState::get() = MarketState{};
 MTSState::get()=MTSState{};
				event("observations_reset", ",\"reason\":\"login\"");
			}
			if (std::string(name) == "in_game") { ++field_entry_seq; mob_statuses.clear(); buff_expiry.clear(); }
			if (screen != name)
				event("screen", std::string(",\"screen\":\"") + name + "\"");
			screen = name;
			dirty = true;
		}

		void mob_status(int32_t oid, const std::string &json)
		{
			if (!dir()) return;
			// Bound retained packet metadata independently of map population.
			if (mob_statuses.size() >= 2048 && !mob_statuses.count(oid)) return;
			mob_statuses[oid] = json;
		}

		void buff_changed(int stat, int source, int duration, int value)
		{
			if (!dir()) return;
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - process_start).count();
			if (source) buff_expiry[stat] = now + duration;
			else buff_expiry.erase(stat);
			if (observing()) event("buff", ",\"boundary\":\"model\",\"stat\":" + std::to_string(stat)
				+ ",\"source\":" + std::to_string(source) + ",\"duration_ms\":" + std::to_string(duration)
				+ ",\"value\":" + std::to_string(value));
		}

		bool observing() { return dir() && trace; }

		void begin_frame() { if (dir()) ++render_frame; }

		uint64_t presentation(const char *boundary, const char *kind, int64_t value, int32_t actor, uint64_t id)
		{
			if (!observing()) return 0;
			if (observations >= 4096) { ++dropped; return 0; }
			if (!id) id = ++observation_id;
			++observations;
			event("presentation", std::string(",\"boundary\":\"") + boundary + "\",\"kind\":\"" + kind
				+ "\",\"observation_id\":" + std::to_string(id) + ",\"value\":" + std::to_string(value)
				+ ",\"actor\":" + std::to_string(actor) + ",\"render_frame\":" + std::to_string(render_frame)
				+ ",\"map\":" + std::to_string(Stage::get().get_mapid()));
			return id;
		}

		void update()
		{
			++progress;
			const char *d = dir();
			if (!d)
				return;

			poll_cancel(d);
			if (screen == "in_game")
			{
				int32_t previous_map = last_map;
				track_changes();
				if (previous_map != -1 && previous_map != last_map)
					cancel_hold("map changed during move");
				if (Stage::get().get_player().get_stats().get_stat(MapleStat::Id::HP) <= 0)
					cancel_hold("player died during move");
				poll_hold();
			}
			else
				cancel_hold("left in-game screen");
			// Selection actions also use the request mailbox; field actions reject
			// explicitly outside a field instead of waiting until a later login.
			poll_act(d);

			auto now = std::chrono::steady_clock::now();
			if (!dirty && (export_paused || now - last < std::chrono::milliseconds(250)))
				return;
			last = now;
			dirty = false;

			const auto export_start = std::chrono::steady_clock::now();
			std::ostringstream o;
			const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					now - process_start).count();
			const char *build_id = std::getenv("MS_HEADLESS_BUILD_ID");
			const char *asset_id = std::getenv("MS_HEADLESS_ASSET_ID");
			o << "{\"schema\":1,\"build\":\"" << esc(build_id && *build_id ? build_id : "heavenclient-v83-headless-1")
			  << "\",\"assets\":\"" << esc(asset_id && *asset_id ? asset_id : "runtime-nx") << "\""
			  << ",\"session_id\":\"" << esc(session_id) << "\",\"seq\":" << ++snapshot_seq
			  << ",\"progress\":" << progress << ",\"uptime_ms\":" << elapsed_ms
			  << ",\"capabilities\":[\"map-geometry\",\"mts-enter\",\"mts-action\",\"market-open\",\"market-action\",\"social-open\",\"social-action\",\"chat-send\",\"cash-item-open\",\"cash-item-use\",\"reset-open\",\"ap-reset\",\"sp-reset\",\"cash-enter\",\"cash-exit\",\"cash-buy\",\"cash-gift\",\"cash-retrieve\",\"cash-coupon\",\"cash-name-rules\",\"cash-world-rules\",\"observe\",\"view-all\",\"login-notice\",\"pic-submit\",\"pic-cancel\",\"world-choose\",\"channel-choose\",\"channel-change\",\"logout\",\"character-choose\",\"character-enter\",\"character-back\",\"character-delete\",\"npc-talk\",\"npc-choose\",\"npc-input\",\"pickup\",\"sit\",\"stand\",\"chair\",\"item-move\",\"item-drop\",\"item-split\",\"scripted-item\",\"use-item\",\"equip\",\"unequip\",\"window\",\"move\",\"jump\",\"portal\",\"attack\",\"skill-use\",\"skill-down\",\"skill-up\",\"skill-cancel\",\"skill-learn\",\"skill-assign\",\"buff-cancel\",\"party-create\",\"party-leave\",\"party-invite\",\"party-accept\",\"party-deny\",\"party-kick\",\"party-leader\",\"party-search-start\",\"party-search-stop\"]"
			  << ",\"observation\":{\"export_paused\":" << (export_paused ? "true" : "false") << ",\"enabled\":" << (trace ? "true" : "false") << ",\"count\":" << observations
			  << ",\"dropped\":" << dropped << ",\"limit\":4096,\"render_frame\":" << render_frame
			  << ",\"exports\":" << export_count << ",\"export_us\":" << export_us << ",\"export_max_us\":" << export_max_us << "}"
			  << ",\"screen\":\"" << screen << "\""
			  << ",\"logout_dialog\":" << (UI::get().is_element_active(UIElement::Type::QUIT) ? "true" : "false");
			{
				BroadcastState::get().snapshot(o);
				SocialState::get().snapshot(o);
                MarketState::get().snapshot(o);
 MTSState::get().snapshot(o);
				const auto &owl = OwlSearchState::get();
				o << ",\"owl\":{\"known\":" << (owl.known ? "true" : "false") << ",\"item\":" << owl.item
				  << ",\"draw_calls\":" << owl.draw_calls << ",\"rows\":[";
				bool comma = false;
				for (const auto &row : owl.rows)
				{
					if (comma) o << ','; comma = true;
					o << "{\"owner\":\"" << esc(row.owner) << "\",\"description\":\"" << esc(row.description)
					  << "\",\"map\":" << row.map << ",\"bundles\":" << row.bundles << ",\"quantity\":" << row.quantity
					  << ",\"price\":" << row.price << ",\"owner_id\":" << row.owner_id << ",\"channel\":" << row.channel << ",\"type\":" << row.type << '}';
				}
				o << "]},\"maple_life\":{\"known\":" << (MapleLifeState::get().known ? "true" : "false")
				  << ",\"code\":" << MapleLifeState::get().code << ",\"name\":\"" << esc(MapleLifeState::get().name)
				  << "\",\"draw_calls\":" << MapleLifeState::get().draw_calls << '}';
				const auto &vega = VegaSpellState::get();
				o << ",\"vega\":{\"pending\":" << (vega.pending ? "true" : "false")
				  << ",\"started\":" << (vega.started ? "true" : "false") << ",\"code\":" << vega.code
				  << ",\"model_updated\":" << (vega.model_updated ? "true" : "false")
				  << ",\"equip_slot\":" << vega.equip_slot << ",\"scroll_slot\":" << vega.scroll_slot
				  << ",\"elapsed_ms\":" << vega.elapsed_ms() << ",\"draw_calls\":" << vega.draw_calls
				  << ",\"asset\":" << (vega.asset ? "true" : "false") << '}';
			}
			{
				const auto& cash = CashShopState::get();
				o << ",\"cash_shop\":{\"opened\":" << (cash.opened ? "true" : "false")
				  << ",\"balances_known\":" << (cash.balances_known ? "true" : "false")
				  << ",\"inventory_known\":" << (cash.inventory_known ? "true" : "false")
				  << ",\"credit\":" << cash.credit << ",\"points\":" << cash.points << ",\"prepaid\":" << cash.prepaid
				  << ",\"revision\":" << cash.revision << ",\"pending\":\"" << esc(cash.pending) << "\",\"result\":\"" << esc(cash.result)
				  << "\",\"message\":" << cash.message << ",\"rules\":\"" << esc(cash.rules) << "\",\"rules_error\":" << cash.rules_error;
				auto cash_items = [&](const char* key, const std::vector<CashShopState::Item>& items) {
					o << ",\"" << key << "\":["; bool comma = false;
					for (const auto& item : items) {
						o << (comma ? "," : "") << "{\"cash_id\":\"" << item.id << "\",\"item\":" << item.item
						  << ",\"serial\":" << item.serial << ",\"quantity\":" << item.quantity
						  << ",\"expiration_raw\":\"" << item.expiration << "\",\"from\":\"" << esc(item.from) << "\"}"; comma = true;
					} o << ']';
				};
				cash_items("inventory", cash.inventory); cash_items("gifts", cash.gifts);
				o << '}';
			}
			if (screen == "in_game")
				o << ",\"navigation\":{\"world_id\":" << int(Configuration::get().get_worldid())
				  << ",\"configured_channel\":" << int(Configuration::get().get_channelid()) + 1
				  << ",\"pending_channel\":" << UIChannel::get_pending_channel()
				  << ",\"field_entry_seq\":" << field_entry_seq << '}';
			if (auto reset = UI::get().get_element_checked<UICashItemDialog>(); reset && reset->is_active())
				o << ",\"reset_dialog\":{\"slot\":" << reset->get_slot() << ",\"item\":" << reset->get_item()
				  << ",\"values\":\"" << esc(reset->get_values()) << "\",\"error\":\"" << esc(reset->get_error()) << "\"}";
			else o << ",\"reset_dialog\":null";
			// Dialog presence and notice IDs only: never serialize PIC entry text.
			auto keyboard = UI::get().get_element<UISoftKey>();
			auto notice = UI::get().get_element_checked<UILoginNotice>();
			o << ",\"login_dialog\":{\"pic_keyboard\":" << (keyboard && keyboard->is_active() ? "true" : "false")
			  << ",\"modal\":" << ((UI::get().is_element_active(UIElement::Type::LOGINNOTICE) || UI::get().is_element_active(UIElement::Type::LOGINNOTICE_CONFIRM)) ? "true" : "false")
			  << ",\"pic_can_cancel\":" << (keyboard && keyboard->is_active() && keyboard->can_cancel() ? "true" : "false")
			  << ",\"notice\":" << (notice && notice->is_active() ? "true" : "false")
			  << ",\"can_cancel\":" << (notice && notice->is_active() && notice->can_cancel() ? "true" : "false")
			  << ",\"message_id\":";
			if (notice && notice->is_active()) o << notice->get_message();
			else o << "null";
			o << '}';
			if (screen == "world_select" || screen == "channel_select")
			{
				if (auto selection = UI::get().get_element<UIWorldSelect>(); selection && selection->is_active())
				{
					o << ",\"login\":{\"world_id\":" << selection->get_selected_world()
					  << ",\"channel\":" << selection->get_selected_channel() << ",\"worlds\":[";
					bool comma = false;
					for (const auto &world : selection->get_worlds())
					{
						if (comma) o << ',';
						comma = true;
						o << "{\"id\":" << int(world.wid) << ",\"name\":\"" << esc(world.name)
						  << "\",\"channel_count\":" << int(world.channelcount) << '}';
					}
					o << "]}";
				}
			}
			if (screen == "character_select")
			{
				if (auto selection = UI::get().get_element<UICharSelect>(); selection && selection->is_active())
				{
					o << ",\"login\":{\"pic_mode\":" << selection->get_pic_mode()
					  << ",\"view_all\":" << (selection->is_view_all() ? "true" : "false")
					  << ",\"selected_index\":" << selection->get_selected_character();
					const auto &characters = selection->get_characters();
					const auto selected = selection->get_selected_character();
					if (selected >= 0 && static_cast<size_t>(selected) < characters.size())
						o << ",\"selected_name\":\"" << esc(characters[selected].stats.name) << "\"";
					o << ",\"characters\":[";
					bool comma = false;
					for (const auto &character : selection->get_characters())
					{
						if (comma) o << ',';
						comma = true;
						o << "{\"id\":" << character.id << ",\"name\":\"" << esc(character.stats.name) << "\",\"world_id\":" << (selection->is_view_all() ? selection->character_world(character.id) : Configuration::get().get_worldid()) << "}";
					}
					o << "]}";
				}
			}
			if (screen == "in_game")
			{
				Player &p = Stage::get().get_player();
				const CharStats &s = p.get_stats();
				auto pos = p.get_position();
				o << ",\"map\":" << Stage::get().get_mapid();
				o << ",\"player\":{\"id\":" << p.get_oid() << ",\"name\":\"" << esc(s.get_name())
				  << "\",\"level\":" << p.get_level() << ",\"job\":\"" << esc(s.get_jobname())
				  << "\",\"x\":" << pos.x() << ",\"y\":" << pos.y()
				  << ",\"hp\":" << s.get_stat(MapleStat::Id::HP) << ",\"maxhp\":" << s.get_stat(MapleStat::Id::MAXHP)
				  << ",\"mp\":" << s.get_stat(MapleStat::Id::MP) << ",\"maxmp\":" << s.get_stat(MapleStat::Id::MAXMP)
				  << ",\"str\":" << s.get_stat(MapleStat::STR) << ",\"dex\":" << s.get_stat(MapleStat::DEX)
				  << ",\"int\":" << s.get_stat(MapleStat::INT) << ",\"luk\":" << s.get_stat(MapleStat::LUK)
				  << ",\"exp\":" << s.get_exp() << ",\"ap\":" << s.get_stat(MapleStat::AP) << ",\"sp\":" << s.get_stat(MapleStat::SP);
				motion(o, p);
				o << "}";
				o << ",\"skills\":[";
				bool skill_comma = false;
				size_t skill_count = 0;
				for (const auto &entry : p.get_skills().entries())
				{
					if (skill_count++ == 512) break;
					if (skill_comma) o << ','; skill_comma = true;
					o << "{\"id\":" << entry.first << ",\"level\":" << entry.second.level << ",\"master_level\":" << entry.second.masterlevel << ",\"expiry_raw\":" << entry.second.expiration << '}';
				}
				o << "],\"skills_truncated\":" << (p.get_skills().entries().size() > 512 ? "true" : "false") << ",\"charge\":{\"skill\":" << Stage::get().get_combat().held_skill() << ",\"elapsed_ms\":" << Stage::get().get_combat().charge_ms() << ",\"asset\":" << (p.has_charge_asset() ? "true" : "false") << "},\"summons\":[";
				bool summon_comma = false;
				for (const auto &entry : Stage::get().get_combat().get_summons())
				{
					if (summon_comma) o << ','; summon_comma = true;
					const auto &s = entry.second;
					o << "{\"oid\":" << s.oid << ",\"owner\":" << s.owner << ",\"skill\":" << s.skill << ",\"level\":" << s.level << ",\"x\":" << s.position.x() << ",\"y\":" << s.position.y() << ",\"movement\":" << s.movement << ",\"follow_supported\":false,\"attacking\":" << (s.attacking ? "true" : "false") << ",\"asset\":" << (s.asset ? "true" : "false") << '}';
				}
				o << ']';
				o << ",\"look\":";
				look(o, p.get_look());
				o << ",\"buffs\":[";
				bool buff_comma = false;
				for (const auto &b : p.get_buffs().values())
				{
					if (!b.skillid) continue;
					if (buff_comma) o << ',';
					buff_comma = true;
					o << "{\"stat\":" << int(b.stat) << ",\"source\":" << b.skillid << ",\"value\":" << b.value
					  << ",\"duration_ms\":" << b.duration << ",\"expires_uptime_ms\":";
					auto expiry = buff_expiry.find(int(b.stat));
					if (expiry == buff_expiry.end()) o << "null"; else o << expiry->second;
					o << '}';
				}
				o << ']';
				o << ",\"inventory\":";
				inventory(o, p.get_inventory());
				o << ",\"party\":";
				party_json(o);
				o << ",\"party_invite\":";
				if (invite_party)
					o << "{\"party\":" << invite_party << ",\"from\":\"" << esc(invite_from) << "\"}";
				else
					o << "null";
				o << ",\"npc_dialog\":";
				if (dialog_open)
					dialog(o);
				else
					o << "null";
				auto chat = UI::get().get_element<UIChatBar>();
				auto stats = UI::get().get_element<UIStatsInfo>();
				auto item_inventory = UI::get().get_element<UIItemInventory>();
				auto equip_inventory = UI::get().get_element<UIEquipInventory>();
				o << ",\"ui\":{\"chat_input_open\":" << (chat && chat->is_chatfieldopen() ? "true" : "false")
				  << ",\"stats_open\":"
				  << (stats && stats->is_active() ? "true" : "false")
				  << ",\"item_inventory_open\":"
				  << (item_inventory && item_inventory->is_active() ? "true" : "false")
				  << ",\"equip_inventory_open\":"
				  << (equip_inventory && equip_inventory->is_active() ? "true" : "false") << "}";
				o << ",";
				objects(o, Stage::get().get_chars().get_chars(), "chars");
				o << ",";
				objects(o, Stage::get().get_npcs().get_npcs(), "npcs");
				o << ",";
				objects(o, Stage::get().get_mobs().get_mobs(), "mobs");
				o << ",";
				objects(o, Stage::get().get_drops().get_drops(), "drops");
				o << ",";
				objects(o, Stage::get().get_reactors().get_reactors(), "reactors");
			}
			else
				o << ",\"map\":null";
			o << "}\n";

			std::string path = std::string(d) + "/state.json";
			std::string tmp = path + ".tmp";
			{
				std::ofstream f(tmp, std::ios::trunc);
				f << o.str();
			}
			std::rename(tmp.c_str(), path.c_str());
			const auto cost = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - export_start).count();
			export_us += cost;
			export_max_us = std::max(export_max_us, static_cast<uint64_t>(cost));
			++export_count;
		}
	}
}
