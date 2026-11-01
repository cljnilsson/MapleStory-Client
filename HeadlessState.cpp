#include "HeadlessState.h"

#include "Gameplay/Stage.h"
#include "Gameplay/MapleMap/Npc.h"
#include "Gameplay/MapleMap/Mob.h"
#include "IO/UI.h"
#include "IO/KeyAction.h"
#include "IO/UITypes/UINpcTalk.h"
#include "Net/Packets/InventoryPackets.h"
#include "Net/Packets/GameplayPackets.h"
#include "Net/Packets/NpcInteractionPackets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ms
{
	namespace HeadlessState
	{
		namespace
		{
			std::string screen = "starting";
			bool dirty = true;
			std::chrono::steady_clock::time_point last;

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

			void objects(std::ostringstream &o, MapObjects *objs, const char *kind)
			{
				o << "\"" << kind << "\":[";
				bool first = true;
				for (auto &entry : *objs)
				{
					auto &obj = entry.second;
					if (!first)
						o << ",";
					first = false;
					auto pos = obj->get_position();
					o << "{\"oid\":" << obj->get_oid() << ",\"x\":" << pos.x() << ",\"y\":" << pos.y();
					if (auto *c = dynamic_cast<Char *>(obj.get()))
						{
						o << ",\"name\":\"" << esc(c->get_name()) << "\",\"level\":" << c->get_level() << ",\"look\":";
						look(o, c->get_look());
						}
					else if (auto *n = dynamic_cast<Npc *>(obj.get()))
						o << ",\"id\":" << n->get_npcid();
					else if (auto *m = dynamic_cast<Mob *>(obj.get()))
						o << ",\"id\":" << m->get_mobid();
					o << "}";
				}
				o << "]";
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
						event("despawn", std::string(",\"kind\":\"") + kind + "\",\"oid\":" + std::to_string(oid));
				prev = now;
			}

			void track_changes()
			{
				int32_t map = Stage::get().get_mapid();
				if (map != last_map)
				{
					last_map = map;
					known.clear();
					event("map", ",\"map\":" + std::to_string(map));
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
						o << (first ? "" : ",") << "{\"slot\":" << s[0] << ",\"id\":" << s[1] << ",\"count\":" << s[2] << "}";
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
					size_t end = dialog_text.find("#l", close + 1);
					std::string label = dialog_text.substr(close + 1, end == std::string::npos ? std::string::npos : end - close - 1);
					o << (first ? "" : ",") << "{\"index\":" << dialog_text.substr(pos + 2, close - pos - 2) << ",\"label\":\"" << esc(label) << "\"}";
					first = false;
					pos = close;
				}
				o << "]}";
			}

			void act_result(const std::string &id, const std::string &name, bool ok, const std::string &error)
			{
				event("act", ",\"id\":\"" + esc(id) + "\",\"act\":\"" + esc(name) + "\",\"ok\":" + (ok ? "true" : "false")
							 + (ok ? "" : ",\"error\":\"" + esc(error) + "\""));
			}

			struct Hold
			{
				bool active = false;
				int32_t action = 0;
				std::string id, name;
				std::chrono::steady_clock::time_point until;
			} hold;

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

				Inventory &inv = Stage::get().get_player().get_inventory();
				auto num = [&](int32_t &out) {
					try { size_t n; out = std::stoi(arg, &n); return n == arg.size(); } catch (...) { return false; }
				};
				int32_t n = 0;

				if (name == "npc-talk")
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
					if (arg == "next" || arg == "yes" || arg == "ok") v = 1;
					else if (arg == "no" || arg == "prev") v = 0;
					else if (arg == "close") v = -1;
					else if (!num(v)) return act_result(id, name, false, "expected next|yes|no|prev|close|<n>");
					if (!talk->headless_respond(v)) return act_result(id, name, false, "unsupported dialog type");
				}
				else if (name == "use-item")
				{
					if (!num(n)) return act_result(id, name, false, "expected use slot");
					int32_t itemid = inv.get_item_id(InventoryType::Id::USE, n);
					if (!itemid) return act_result(id, name, false, "empty use slot");
					UseItemPacket(n, itemid).dispatch();
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
				else if (name == "party-create")
					CreatePartyPacket().dispatch();
				else if (name == "party-leave")
					LeavePartyPacket().dispatch();
				else if (name == "party-invite")
				{
					if (arg.empty()) return act_result(id, name, false, "expected character name");
					InviteToPartyPacket(arg).dispatch();
				}
				else if (name == "party-accept")
				{
					if (!invite_party) return act_result(id, name, false, "no pending party invite");
					JoinPartyPacket(invite_party).dispatch();
					invite_party = 0;
				}
				else if (name == "jump")
					key_tap(KeyAction::Id::JUMP);
				else if (name == "portal")
					key_tap(KeyAction::Id::UP);
				else if (name == "attack")
					key_tap(KeyAction::Id::ATTACK);
				else
					return act_result(id, name, false, "unknown act");

				act_result(id, name, true, "");
			}
		}

		void on_party(InPacket &recv)
		{
			int8_t mode = recv.read_byte();
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
				event("party_invite", ",\"party\":" + std::to_string(invite_party) + ",\"from\":\"" + esc(invite_from) + "\"");
			}
			else if (mode == 8)
			{
				party_id = recv.read_int();
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
					(void)target;
				}
				else if (mode == 0x0f)
					recv.skip_string();
				party_id = pid;
				roster();
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
			o << "{\"seq\":" << ++seq << ",\"ts\":" << ms << ",\"type\":\"" << type << "\"" << fields << "}\n";
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

		void set_screen(const char *name)
		{
			if (screen != name)
				event("screen", std::string(",\"screen\":\"") + name + "\"");
			screen = name;
			dirty = true;
		}

		void update()
		{
			const char *d = dir();
			if (!d)
				return;

			if (screen == "in_game")
			{
				track_changes();
				poll_hold();
				poll_act(d);
			}

			auto now = std::chrono::steady_clock::now();
			if (!dirty && now - last < std::chrono::milliseconds(250))
				return;
			last = now;
			dirty = false;

			std::ostringstream o;
			o << "{\"screen\":\"" << screen << "\"";
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
				  << ",\"exp\":" << s.get_exp() << "}";
				o << ",\"look\":";
				look(o, p.get_look());
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
				o << ",";
				objects(o, Stage::get().get_chars().get_chars(), "chars");
				o << ",";
				objects(o, Stage::get().get_npcs().get_npcs(), "npcs");
				o << ",";
				objects(o, Stage::get().get_mobs().get_mobs(), "mobs");
				o << ",";
				objects(o, Stage::get().get_drops().get_drops(), "drops");
			}
			o << "}\n";

			std::string path = std::string(d) + "/state.json";
			std::string tmp = path + ".tmp";
			{
				std::ofstream f(tmp, std::ios::trunc);
				f << o.str();
			}
			std::rename(tmp.c_str(), path.c_str());
		}
	}
}
