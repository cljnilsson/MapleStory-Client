// Fan-client social controls. Packet operation order follows HeavenMS v83.
#include "UISocial.h"
#include "../../Data/ItemData.h"
#include "../../Gameplay/Stage.h"
#include "../../Graphics/Geometry.h"
#include "../../HeadlessState.h"
#include "../../Net/Handlers/Helpers/ItemParser.h"
#include "../../Net/Handlers/Helpers/LoginParser.h"
#include "../../Net/Packets/MessagingPackets.h"
#include "../Components/AreaButton.h"
#include "../UI.h"
#include "UIChatBar.h"
#include <algorithm>
#include <nlnx/nx.hpp>
#include <sstream>
#include <stdexcept>
namespace ms {
namespace {
class SocialRequest : public OutPacket {
public:
  explicit SocialRequest(int opcode) : OutPacket(opcode) {}
  SocialRequest(int opcode, int mode) : OutPacket(opcode) { write_byte(mode); }
  using OutPacket::write_byte;
  using OutPacket::write_int;
  using OutPacket::write_short;
  using OutPacket::write_string;
};
bool name_valid(const std::string &s) {
  return !s.empty() && s.size() <= 13 && s.find(' ') == std::string::npos;
}
void open_marriage() {
  if (auto panel = UI::get().get_element<UISocial>()) {
    if (panel->tab == "marriage") {
      panel->makeactive();
      return;
    }
  }
  UI::get().emplace<UISocial>("marriage");
}
void line(const std::string &s) {
  if (auto chat = UI::get().get_element<UIChatBar>())
    chat->send_chatline(s, UIChatBar::BLUE);
}
} // namespace
NewYearCard NewYearCard::read(InPacket &r) {
  NewYearCard c;
  c.id = r.read_int();
  c.sender = r.read_int();
  c.from = r.read_string();
  c.sender_discarded = r.read_bool();
  c.sent = r.read_long();
  c.receiver = r.read_int();
  c.to = r.read_string();
  c.receiver_discarded = r.read_bool();
  c.received = r.read_bool();
  c.accepted = r.read_long();
  c.message = r.read_string();
  if (c.id <= 0 || c.from.size() > 13 || c.to.size() > 13 ||
      c.message.size() > 1024)
    throw std::runtime_error("Invalid New Year card");
  return c;
}
SocialState &SocialState::get() {
  static SocialState s;
  return s;
}
void SocialState::changed(const std::string &message) {
  status = message;
  ++revision;
  HeadlessState::event("social",
                       ",\"text\":\"" + HeadlessState::escape(message) + "\"");
}
std::string SocialState::request(const std::string &action,
                                 const std::string &target,
                                 const std::string &text) {
  if (action == "wedding-propose" || action == "wedding-invitation") {
    auto type =
        action == "wedding-propose" ? InventoryType::USE : InventoryType::ETC;
    int slot = 0, item = 0;
    for (auto row :
         Stage::get().get_player().get_inventory().list_slots(type)) {
      bool matches = action == "wedding-propose"
                         ? row[1] >= 2240000 && row[1] <= 2240015
                         : row[1] == 4031406 || row[1] == 4031407;
      if (matches && row[2] > 0) {
        slot = row[0];
        item = row[1];
        break;
      }
    }
    if (!slot)
      return "The required ring box or received invitation is not in your "
             "inventory.";
    if (action == "wedding-propose") {
      if (!name_valid(target))
        return "Enter a valid partner name.";
      if (proposal_pending)
        return "A proposal is already pending; cancel it first.";
      SocialRequest p(0x89, 0);
      p.write_string(target);
      p.write_int(item);
      p.dispatch();
      proposal_pending = true;
      changed("Proposal sent; waiting for the server and your partner.");
    } else {
      SocialRequest p(0x89, 6);
      p.write_int(slot);
      p.write_int(item);
      p.dispatch();
      changed("Opening received invitation.");
    }
    return "";
  }
  if (action == "wedding-invite") {
    if (!name_valid(target) || relationship_id <= 0)
      return "Enter a guest name and obtain your engagement record first.";
    int slot = 0;
    for (auto row : Stage::get().get_player().get_inventory().list_slots(
             InventoryType::ETC))
      if ((row[1] == 4031377 || row[1] == 4031395) && row[2] > 0) {
        slot = row[0];
        break;
      }
    if (!slot || slot > 127)
      return "An invitation envelope in Etc inventory is required.";
    SocialRequest p(0x89, 5);
    p.write_string(target);
    p.write_int(relationship_id);
    p.write_byte(slot);
    p.dispatch();
    changed("Guest invitation requested; wait for item consumption or server "
            "refusal.");
    return "";
  }
  if (action == "wedding-accept" || action == "wedding-deny") {
    if (proposal_id <= 0 || proposal_from.empty())
      return "No received proposal is awaiting your answer.";
    SocialRequest p(0x89, 2);
    p.write_byte(action == "wedding-accept");
    p.write_string(proposal_from);
    p.write_int(proposal_id);
    p.dispatch();
    proposal_id = 0;
    proposal_from.clear();
    changed(action == "wedding-accept"
                ? "Acceptance sent; waiting for server result."
                : "Proposal declined.");
    return "";
  }
  if (action == "wedding-cancel") {
    if (!proposal_pending)
      return "No outgoing proposal is pending.";
    SocialRequest(0x89, 1).dispatch();
    proposal_pending = false;
    changed("Proposal cancelled.");
    return "";
  }
  if (action == "wedding-wishlist") {
    if (!wishlist_pending)
      return "No wishlist form was requested by the server.";
    std::vector<std::string> entries;
    std::istringstream input(text);
    std::string entry;
    while (std::getline(input, entry, '|')) {
      if (entry.empty() || entry.size() > 100 || entries.size() == 10)
        return "Enter 1-10 item names of 1-100 characters, separated by |.";
      entries.push_back(entry);
    }
    if (entries.empty())
      return "Enter at least one item name.";
    SocialRequest p(0x89, 9);
    p.write_short(entries.size());
    for (const auto &e : entries)
      p.write_string(e);
    p.dispatch();
    wishlist_pending = false;
    changed(
        "Wishlist submitted; view it through the Gift Registry to confirm.");
    return "";
  }
  if (action == "wedding-bless") {
    if (ceremony_step < 0)
      return "No wedding ceremony has been received.";
    SocialRequest(0x8b).dispatch();
    changed("Blessing requested; waiting for the server.");
    return "";
  }
  if (action == "wedding-claim") {
    if (!wedding_received || (wedding_mode != 0x0a && wedding_mode != 0x0f) ||
        wedding_index < 0 || size_t(wedding_index) >= wedding_gifts.size())
      return "Select a received wedding gift addressed to you.";
    SocialRequest p(0x8a, 7);
    p.write_byte(wedding_gifts[wedding_index].first / 1000000);
    p.write_byte(wedding_index);
    p.dispatch();
    changed("Wedding gift claim requested; wait for inventory and registry "
            "update.");
    return "";
  }
  if (action == "newyear-send") {
    if (!name_valid(target) || text.empty() || text.size() > 127)
      return "Enter recipient and a message of 1-127 characters.";
    auto &inv = Stage::get().get_player().get_inventory();
    int slot = 0;
    for (auto row : inv.list_slots(InventoryType::USE))
      if (row[1] == 2160101 && row[2] > 0) {
        slot = row[0];
        break;
      }
    if (!slot)
      return "No New Year card (2160101) in received Use inventory.";
    SocialRequest p(0x9f, 0);
    p.write_short(slot);
    p.write_int(2160101);
    p.write_string(target);
    p.write_string(text);
    p.dispatch();
    changed("New Year card requested; wait for server response.");
    return "";
  }
  if (action == "newyear-receive") {
    auto c = cards.find(selected_card);
    if (c == cards.end() || c->second.received || c->second.sender_discarded ||
        (!c->second.to.empty() &&
         c->second.receiver != Stage::get().get_player().get_oid()))
      return "Select an unreceived card addressed to you.";
    SocialRequest p(0x9f, 1);
    p.write_int(selected_card);
    p.dispatch();
    changed("Card receipt requested; wait for server response.");
    return "";
  }
  if (action == "guide-open") {
    SocialRequest(0xa2).dispatch();
    changed("Guide requested; wait for the scripted dialog.");
    return "";
  }
  if (action == "incubator-next" || action == "incubator-food" ||
      action == "incubator-feed") {
    auto &inv = Stage::get().get_player().get_inventory();
    if (action == "incubator-next") {
      auto slots = inv.list_slots(InventoryType::ETC);
      std::vector<std::array<int32_t, 3>> eligible;
      for (const auto &slot : slots)
        if (ItemData::get(slot[1]).quest_id > 0 &&
            !ItemData::get(slot[1]).quest_food.empty())
          eligible.push_back(slot);
      if (eligible.empty())
        return "No quest incubator in received ETC inventory.";
      auto found =
          std::find_if(eligible.begin(), eligible.end(),
                       [&](auto slot) { return slot[0] > incubator_slot; });
      if (found == eligible.end())
        found = eligible.begin();
      incubator_slot = (*found)[0];
      incubator_item = (*found)[1];
      food_type = food_slot = 0;
    } else {
      if (!incubator_item ||
          inv.get_item_id(InventoryType::ETC, incubator_slot) != incubator_item)
        return "Select a received quest incubator first.";
      const auto &data = ItemData::get(incubator_item);
      if (action == "incubator-food") {
        std::vector<std::pair<int, int>> eligible;
        for (auto type : {InventoryType::USE, InventoryType::ETC})
          for (const auto &slot : inv.list_slots(type))
            if (data.quest_food.count(slot[1]))
              eligible.emplace_back(type, slot[0]);
        if (eligible.empty())
          return "No compatible food in received inventory.";
        auto found = std::find(eligible.begin(), eligible.end(),
                               std::make_pair(food_type, food_slot));
        if (found == eligible.end() || ++found == eligible.end())
          found = eligible.begin();
        food_type = found->first;
        food_slot = found->second;
      } else {
        auto type = static_cast<InventoryType::Id>(food_type);
        auto item = inv.get_item_id(type, food_slot);
        if (!food_slot || !data.quest_food.count(item) ||
            inv.get_item_count(type, food_slot) < 1)
          return "Select available compatible food first.";
        if (!Stage::get().get_player().get_quests().is_started(data.quest_id))
          return "The incubator quest has not started.";
        SocialRequest p(0xee);
        p.write_byte(food_type);
        p.write_short(food_slot);
        p.write_int(incubator_item);
        p.dispatch();
      }
    }
    changed(action == "incubator-feed"
                ? "Feed requested; wait for inventory and quest update."
                : "Incubator selection updated.");
    return "";
  }
  if (action.rfind("family-", 0) == 0) {
    if (action == "family-open") {
      SocialRequest(0x92).dispatch();
    } else if (action == "family-pedigree") {
      std::string name =
          target.empty() ? Stage::get().get_player().get_name() : target;
      if (!name_valid(name))
        return "Enter a character name.";
      SocialRequest p(0x91);
      p.write_string(name);
      p.dispatch();
    } else if (action == "family-invite") {
      if (!name_valid(target))
        return "Enter a character name.";
      SocialRequest p(0x93);
      p.write_string(target);
      p.dispatch();
    } else if (action == "family-accept" || action == "family-deny") {
      if (!family_invite)
        return "No family invitation.";
      SocialRequest p(0x96);
      p.write_int(family_invite);
      p.write_string(family_from);
      p.write_byte(action == "family-accept");
      p.dispatch();
      family_invite = 0;
      family_from.clear();
    } else if (action == "family-summon-accept" ||
               action == "family-summon-deny") {
      if (summon_from.empty())
        return "No family summon request.";
      SocialRequest p(0x99);
      p.write_string(summon_from);
      p.write_byte(action == "family-summon-accept");
      p.dispatch();
      summon_from.clear();
      summon_family.clear();
    } else if (action == "family-summon" || action == "family-reunite") {
      auto member =
          std::find_if(pedigree.begin(), pedigree.end(), [&](const auto &m) {
            return m.name == target && m.online;
          });
      if (member == pedigree.end() ||
          member->id == Stage::get().get_player().get_oid())
        return "Select another online member from the received pedigree.";
      int id = action == "family-summon" ? 1 : 0;
      if (!family_leader || privileges.size() <= static_cast<size_t>(id) ||
          family_rep < privileges[id].cost || family_used[id])
        return "Family privilege unavailable: check reputation and daily use.";
      SocialRequest p(0x97);
      p.write_int(id);
      p.write_string(target);
      p.dispatch();
    } else
      return "Unknown family action.";
    changed(action + " requested; wait for server response.");
    return "";
  }
  if (action.rfind("party-", 0) == 0)
    return HeadlessState::party_request(action, target, text);
  auto buddy = std::find_if(buddies.begin(), buddies.end(),
                            [&](const auto &b) { return b.name == target; });
  if (action == "buddy-add") {
    if (!name_valid(target))
      return "Enter a character name (1-13 characters).";
    SocialRequest p(0x82, 1);
    p.write_string(target);
    p.write_string("Default Group");
    p.dispatch();
  } else if (action == "buddy-accept" || action == "buddy-deny") {
    if (!buddy_invite)
      return "No buddy invitation.";
    SocialRequest p(0x82, action == "buddy-accept" ? 2 : 3);
    p.write_int(buddy_invite);
    p.dispatch();
    buddy_invite = 0;
    buddy_from.clear();
  } else if (action == "buddy-send" || action == "buddy-remove") {
    if (buddy == buddies.end())
      return "Select a received buddy by name.";
    if (action == "buddy-remove") {
      SocialRequest p(0x82, 3);
      p.write_int(buddy->id);
      p.dispatch();
    } else {
      if (text.empty() || text.size() > 127)
        return "Message must contain 1-127 characters.";
      GroupChatPacket(0, {buddy->id}, text).dispatch();
    }
  } else if (action == "messenger-create") {
    if (messenger_requested || messenger_joined)
      return "Leave the current Messenger first.";
    SocialRequest p(0x7a, 0);
    p.write_int(0);
    p.dispatch();
    messenger_requested = true;
  } else if (action == "messenger-invite") {
    if (!messenger_requested && !messenger_joined)
      return "Open a Messenger room first.";
    if (!name_valid(target))
      return "Enter a character name.";
    SocialRequest p(0x7a, 3);
    p.write_string(target);
    p.dispatch();
  } else if (action == "messenger-accept" || action == "messenger-deny") {
    if (!messenger_invite)
      return "No Messenger invitation.";
    if (action == "messenger-accept") {
      if (messenger_requested || messenger_joined)
        return "Leave the current Messenger first.";
      SocialRequest p(0x7a, 0);
      p.write_int(messenger_invite);
      p.dispatch();
      messenger_requested = true;
    } else {
      SocialRequest p(0x7a, 5);
      p.write_string(messenger_from);
      p.dispatch();
    }
    messenger_invite = 0;
    messenger_from.clear();
  } else if (action == "messenger-leave") {
    if (!messenger_requested && !messenger_joined)
      return "No Messenger room to leave.";
    SocialRequest(0x7a, 2).dispatch();
    messenger_requested = messenger_joined = false;
    members.clear();
    position = -1;
  } else if (action == "messenger-send") {
    if (!messenger_requested && !messenger_joined)
      return "Open or join a Messenger room first.";
    if (text.empty() || text.size() > 127)
      return "Message must contain 1-127 characters.";
    SocialRequest p(0x7a, 6);
    p.write_string(text);
    p.dispatch();
  } else if (action == "note-send") {
    if (!name_valid(target) || text.empty() || text.size() > 127)
      return "Enter recipient and message (1-127 characters).";
    SocialRequest p(0x83, 0);
    p.write_string(target);
    p.write_string(text);
    p.dispatch();
  } else if (action == "note-discard") {
    auto note = std::find_if(notes.begin(), notes.end(), [&](const auto &n) {
      return !n.discarded && n.id == selected_note;
    });
    if (note == notes.end())
      return "Select a received note.";
    SocialRequest p(0x83, 1);
    p.write_byte(1);
    p.write_byte(0);
    p.write_byte(0);
    p.write_int(note->id);
    p.write_byte(0);
    p.dispatch();
    note->discarded = true;
  } else
    return "Unknown social action.";
  changed(
      action == "note-send" ? "Note requested. A gift note ticket is required; "
                              "delivery has no acknowledgement."
      : action == "messenger-create" ? "Room requested. Invite a character; no "
                                       "creation acknowledgement is sent."
      : action == "messenger-leave" ? "Leave requested; local Messenger closed."
      : action == "note-discard"    ? "Discard requested; note hidden locally."
      : action == "buddy-add"       ? "Buddy request sent."
      : action == "buddy-remove"    ? "Buddy removal requested."
      : action == "buddy-accept"    ? "Buddy acceptance sent."
      : action == "buddy-deny"      ? "Buddy invitation declined."
      : action == "buddy-send"
          ? "Buddy message sent; no delivery acknowledgement."
      : action == "messenger-invite" ? "Messenger invitation requested."
      : action == "messenger-accept" ? "Joining Messenger."
      : action == "messenger-deny"
          ? "Messenger invitation declined."
          : "Messenger message sent; no delivery acknowledgement.");
  return {};
}
void SocialState::snapshot(std::ostream &o) const {
  auto q = [](const std::string &s) {
    return "\"" + HeadlessState::escape(s) + "\"";
  };
  o << ",\"social\":{\"status\":" << q(status) << ",\"draws\":" << draws
    << ",\"buddy_from\":" << q(buddy_from)
    << ",\"messenger_from\":" << q(messenger_from)
    << ",\"selected_note\":" << selected_note
    << ",\"messenger_invite\":" << messenger_invite
    << ",\"messenger_requested\":" << (messenger_requested ? "true" : "false")
    << ",\"messenger_joined\":" << (messenger_joined ? "true" : "false")
    << ",\"buddies\":[";
  bool comma = false;
  for (const auto &b : buddies) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"name\":" << q(b.name) << ",\"channel\":" << b.channel << '}';
  }
  o << "],\"members\":[";
  comma = false;
  for (const auto &m : members) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"position\":" << m.first << ",\"name\":" << q(m.second.name)
      << ",\"channel\":" << m.second.channel << '}';
  }
  o << "],\"incubator\":{\"item\":" << incubator_item
    << ",\"slot\":" << incubator_slot << ",\"food_type\":" << food_type
    << ",\"food_slot\":" << food_slot;
  if (incubator_item) {
    auto &player = Stage::get().get_player();
    auto &inv = player.get_inventory();
    const auto &data = ItemData::get(incubator_item);
    o << ",\"name\":" << q(data.get_name()) << ",\"quest\":" << data.quest_id
      << ",\"started\":"
      << (player.get_quests().is_started(data.quest_id) ? "true" : "false")
      << ",\"progress\":" << q(player.get_quests().progress(data.quest_id))
      << ",\"cap\":" << data.quest_exp * data.quest_grade << ",\"food_count\":"
      << (food_slot ? inv.get_item_count(
                          static_cast<InventoryType::Id>(food_type), food_slot)
                    : 0);
  }
  o << "}";
  o << ",\"family\":{\"received\":" << (family_received ? "true" : "false")
    << ",\"name\":" << q(family_name) << ",\"message\":" << q(family_message)
    << ",\"reputation\":" << family_rep << ",\"total\":" << family_total
    << ",\"today\":" << family_today << ",\"leader\":" << family_leader
    << ",\"juniors\":" << family_juniors
    << ",\"invite_from\":" << q(family_from)
    << ",\"summon_from\":" << q(summon_from)
    << ",\"summon_family\":" << q(summon_family)
    << ",\"pedigree_received\":" << (pedigree_received ? "true" : "false")
    << ",\"pedigree\":[";
  comma = false;
  for (const auto &m : pedigree) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"id\":" << m.id << ",\"senior\":" << m.senior
      << ",\"name\":" << q(m.name)
      << ",\"online\":" << (m.online ? "true" : "false")
      << ",\"level\":" << m.level << ",\"job\":" << m.job
      << ",\"reputation\":" << m.reputation << ",\"channel\":" << m.channel
      << '}';
  }
  o << "],\"privileges\":[";
  comma = false;
  for (size_t i = 0; i < privileges.size(); ++i) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"name\":" << q(privileges[i].name)
      << ",\"description\":" << q(privileges[i].description)
      << ",\"cost\":" << privileges[i].cost << ",\"used\":"
      << (family_used.count(i) && family_used.at(i) ? "true" : "false") << '}';
  }
  o << "]}";
  o << ",\"rates_hint\":" << q(rates_hint);
  o << ",\"wedding\":{\"received\":" << (wedding_received ? "true" : "false")
    << ",\"view\":\"" << HeadlessState::escape(marriage_view) << "\""
    << ",\"proposal_from\":\"" << HeadlessState::escape(proposal_from) << "\""
    << ",\"proposal_id\":" << proposal_id
    << ",\"proposal_pending\":" << (proposal_pending ? "true" : "false")
    << ",\"wishlist_pending\":" << (wishlist_pending ? "true" : "false")
    << ",\"marriage_status\":" << marriage_status
    << ",\"relationship_id\":" << relationship_id << ",\"groom\":\""
    << HeadlessState::escape(groom_name) << "\""
    << ",\"bride\":\"" << HeadlessState::escape(bride_name) << "\""
    << ",\"invitation_type\":" << invitation_type
    << ",\"ceremony_step\":" << ceremony_step
    << ",\"ceremony_ended\":" << (ceremony_ended ? "true" : "false")
    << ",\"ceremony_effects\":" << ceremony_effects
    << ",\"partner_id\":" << partner_id << ",\"partner_map\":" << partner_map
    << ",\"mode\":" << wedding_mode << ",\"selected\":" << wedding_index
    << ",\"gifts\":[";
  bool gift_comma = false;
  for (auto g : wedding_gifts) {
    if (gift_comma)
      o << ',';
    gift_comma = true;
    o << "{\"item\":" << g.first << ",\"quantity\":" << g.second << '}';
  }
  o << "],\"wishlist\":[";
  bool wc = false;
  for (const auto &v : wedding_wishlist) {
    if (wc)
      o << ",";
    wc = true;
    o << q(v);
  }
  o << "]}";
  o << ",\"newyear\":{\"selected\":" << selected_card << ",\"cards\":[";
  bool card_comma = false;
  for (const auto &entry : cards) {
    const auto &c = entry.second;
    if (card_comma)
      o << ',';
    card_comma = true;
    o << "{\"id\":" << c.id << ",\"from\":" << q(c.from)
      << ",\"to\":" << q(c.to) << ",\"message\":" << q(c.message)
      << ",\"received\":" << (c.received ? "true" : "false") << '}';
  }
  o << "]}";
  o << ",\"notes\":[";
  comma = false;
  for (const auto &n : notes) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"from\":" << q(n.from) << ",\"message\":" << q(n.message)
      << ",\"time\":" << n.time << ",\"fame\":" << n.fame
      << ",\"discard_requested\":" << (n.discarded ? "true" : "false") << '}';
  }
  if (auto panel = UI::get().get_element<UISocial>()) {
    o << "],\"window\":{\"active\":" << (panel->is_active() ? "true" : "false")
      << ",\"tab\":" << q(panel->tab);
    panel->snapshot_controls(o);
    o << "},\"messages\":[";
  } else
    o << "],\"window\":null,\"messages\":[";
  comma = false;
  for (const auto &m : messages) {
    if (comma)
      o << ',';
    comma = true;
    o << q(m);
  }
  o << "]}";
}
void UISocial::snapshot_controls(std::ostream &out) const {
  out << ",\"target\":\"" << HeadlessState::escape(target.get_text())
      << "\",\"options\":\"" << HeadlessState::escape(body.get_text())
      << "\",\"controls\":[";
  bool comma = false;
  for (const auto &entry : labels) {
    if (comma)
      out << ',';
    comma = true;
    auto bounds = buttons.at(entry.first)->bounds(position);
    out << "{\"label\":\"" << HeadlessState::escape(entry.second.get_text())
        << "\",\"x\":" << bounds.get_left_top().x()
        << ",\"y\":" << bounds.get_left_top().y() << '}';
  }
  out << "],\"rows\":[";
  comma = false;
  for (const auto &row : rows) {
    if (comma)
      out << ',';
    comma = true;
    out << '"' << HeadlessState::escape(row.get_text()) << '"';
  }
  out << "],\"choices\":[";
  comma = false;
  for (size_t i = 0; i < row_targets.size(); ++i) {
    if (!buttons.count(100 + i))
      continue;
    if (comma)
      out << ',';
    comma = true;
    auto p = buttons.at(100 + i)->bounds(position).get_left_top();
    out << "{\"name\":\"" << HeadlessState::escape(row_targets[i])
        << "\",\"x\":" << p.x() << ",\"y\":" << p.y() << '}';
  }
  out << ']';
}
UISocial::UISocial(std::string t)
    : UIElement(Point<int16_t>(180, 100), Point<int16_t>(480, 470)),
      tab(std::move(t)) {
  UI::get().remove_textfield();
  target = Textfield(Text::A12M, Text::LEFT, Color::Name::WHITE,
                     Rectangle<int16_t>(15, 465, 300, 323), 13);
  body = Textfield(Text::A12M, Text::LEFT, Color::Name::WHITE,
                   Rectangle<int16_t>(15, 465, 345, 368),
                   tab == "marriage" ? 1009 : 127);
  target.set_horizontal_scroll(true);
  body.set_horizontal_scroll(true);
  target.set_key_callback(KeyAction::ESCAPE, [this]() { close(); });
  body.set_key_callback(KeyAction::ESCAPE, [this]() { close(); });
  target.set_state(Textfield::NORMAL);
  body.set_state(Textfield::NORMAL);
  target.set_enter_callback([this](std::string) {
    target.set_state(Textfield::NORMAL);
    if (tab != "family" && tab != "guide")
      body.set_state(Textfield::FOCUSED);
  });
  body.set_enter_callback([this](std::string) {
    if (tab == "family" || tab == "guide" || tab == "wedding")
      return;
    if (tab == "marriage") {
      act("wedding-wishlist", target.get_text(), body.get_text());
      return;
    }
    if (tab == "party") {
      act("party-search-start", target.get_text(), body.get_text());
      return;
    }
    act(tab == "buddy"     ? "buddy-send"
        : tab == "newyear" ? "newyear-send"
        : tab == "notes"   ? "note-send"
                           : "messenger-send",
        target.get_text(), body.get_text());
  });
  recipient_label =
      Text(Text::A11M, Text::LEFT, Color::Name::WHITE, "Recipient");
  message_label =
      Text(Text::A11M, Text::LEFT, Color::Name::WHITE, "Message (Enter sends)");
  refresh();
}
void UISocial::refresh() {
  auto &s = SocialState::get();
  revision = s.revision;
  message_label.change_text(tab == "party"
                                ? "Search levels: min max (Enter starts)"
                                : "Message (Enter sends)");
  if (tab == "marriage") {
    recipient_label.change_text("Partner or guest name");
    message_label.change_text(
        "Wishlist item names, separated by | (Enter submits)");
  } else
    recipient_label.change_text("Recipient");
  if (tab == "family")
    message_label.change_text(
        s.privileges.size() > 1
            ? "Reunite: " + std::to_string(s.privileges[0].cost) +
                  " rep; Summon: " + std::to_string(s.privileges[1].cost) +
                  " rep (daily)"
            : "Family privilege costs not received.");
  status_label =
      Text(Text::A11M, Text::LEFT, Color::Name::WHITE, s.status, 445);
  rows.clear();
  labels.clear();
  buttons.clear();
  row_targets.clear();
  auto label = [&](int id, int x, int y, const std::string &text) {
    buttons[id] = std::make_unique<AreaButton>(Point<int16_t>(x, y),
                                               Point<int16_t>(105, 24));
    labels.emplace(id, Text(Text::A11M, Text::LEFT, Color::Name::BLACK, text));
  };
  label(0, 15, 12, "Buddies");
  label(1, 79, 12, "Messenger");
  label(2, 159, 12, "Notes");
  label(7, 223, 12, "Party");
  label(8, 287, 12, "Family");
  label(9, 351, 12, "Guide");
  label(3, 415, 12, "Close");
  for (int id : {0, 1, 2, 7, 8, 9, 3}) {
    auto p = buttons[id]->bounds(Point<int16_t>(0, 0)).get_left_top();
    buttons[id] = std::make_unique<AreaButton>(p, Point<int16_t>(id == 1   ? 76
                                                                 : id == 3 ? 50
                                                                           : 60,
                                                                 24));
  }
  auto row = [&](const std::string &text) {
    rows.emplace_back(Text::A11M, Text::LEFT, Color::Name::WHITE, text, 445);
  };
  if (tab == "buddy") {
    row(s.buddy_from.empty() ? "Select a buddy, or enter a name to add."
                             : "Invitation from " + s.buddy_from);
    size_t start = page * 5;
    if (start >= s.buddies.size())
      start = page = 0;
    for (size_t i = start; i < s.buddies.size() && i < start + 5; ++i) {
      auto &b = s.buddies[i];
      row((target.get_text() == b.name ? "> " : "  ") + b.name +
          (b.channel > 0 ? " CH" + std::to_string(b.channel) : " offline"));
      row_targets.push_back(b.name);
      buttons[100 + row_targets.size() - 1] = std::make_unique<AreaButton>(
          Point<int16_t>(15, 70 + 22 * (row_targets.size() - 1)),
          Point<int16_t>(445, 22));
    }
    label(10, 15, 385, "Add");
    label(11, 130, 385, "Remove");
    label(12, 245, 385, "Accept");
    label(13, 360, 385, "Deny");
  } else if (tab == "party") {
    row("Party members (click a name for kick / leader).");
    row("Search levels in Message: min max (all jobs)");
    for (const auto &name : HeadlessState::party_rows()) {
      row(name);
      row_targets.push_back(name);
      buttons[100 + row_targets.size() - 1] = std::make_unique<AreaButton>(
          Point<int16_t>(15, 92 + 22 * (row_targets.size() - 1)),
          Point<int16_t>(445, 22));
    }
    label(40, 15, 385, "Create");
    label(41, 130, 385, "Invite");
    label(42, 245, 385, "Accept");
    label(43, 360, 385, "Deny");
    label(44, 15, 420, "Kick");
    label(45, 130, 420, "Leader");
    label(46, 245, 420, "Search");
    label(47, 360, 420, "Stop search");
    label(48, 360, 225, "Leave");
  } else if (tab == "guide") {
    row("Guide opens the server's NPC script.");
    row("Quest incubator: choose item, choose food, then feed.");
    if (s.incubator_item) {
      auto &player = Stage::get().get_player();
      auto &inv = player.get_inventory();
      const auto &data = ItemData::get(s.incubator_item);
      row(data.get_name() + " (ETC slot " + std::to_string(s.incubator_slot) +
          ")");
      row(std::string(player.get_quests().is_started(data.quest_id)
                          ? "Quest started; progress: "
                          : "Quest not started; progress: ") +
          player.get_quests().progress(data.quest_id) + " / " +
          std::to_string(data.quest_exp * data.quest_grade));
      if (s.food_slot) {
        auto type = static_cast<InventoryType::Id>(s.food_type);
        int item = inv.get_item_id(type, s.food_slot);
        row("Food: " + ItemData::get(item).get_name() + " x" +
            std::to_string(inv.get_item_count(type, s.food_slot)));
      } else
        row("Select food from your inventory.");
    } else
      row("No quest incubator selected.");
    label(60, 15, 385, "Guide dialog");
    label(61, 130, 385, "Choose item");
    label(62, 245, 385, "Choose food");
    label(63, 360, 385, "Feed one");
  } else if (tab == "family") {
    row(s.family_received ? (s.family_leader ? "Family: " + s.family_name
                                             : "No family membership")
                          : "Refresh to request family info.");
    row("Rep " + std::to_string(s.family_rep) + " / total " +
        std::to_string(s.family_total) + " / today " +
        std::to_string(s.family_today));
    if (!s.summon_from.empty())
      row("Summon from " + s.summon_from + " (" + s.summon_family + ")");
    else if (!s.family_from.empty())
      row("Family invitation from " + s.family_from);
    else
      row("Pedigree: select a member; offline members cannot summon.");
    std::vector<const FamilyMember *> unique;
    for (const auto &m : s.pedigree)
      if (std::none_of(unique.begin(), unique.end(),
                       [&](const auto *n) { return n->id == m.id; }))
        unique.push_back(&m);
    if (page * 4 >= unique.size())
      page = 0;
    for (size_t i = page * 4; i < unique.size() && i < page * 4 + 4; ++i) {
      const auto &m = *unique[i];
      row(m.name + " Lv" + std::to_string(m.level) +
          (m.online ? " online" : " offline") + "; senior: " + ([&]() {
            auto senior =
                std::find_if(s.pedigree.begin(), s.pedigree.end(),
                             [&](const auto &n) { return n.id == m.senior; });
            return !m.senior                    ? std::string("none")
                   : senior == s.pedigree.end() ? std::string("outside view")
                                                : senior->name;
          })());
      row_targets.push_back(m.name);
      buttons[100 + row_targets.size() - 1] = std::make_unique<AreaButton>(
          Point<int16_t>(15, 114 + 22 * (row_targets.size() - 1)),
          Point<int16_t>(445, 22));
    }
    label(50, 15, 385, "Refresh");
    label(51, 130, 385, "Pedigree");
    label(52, 245, 385, "Summon");
    label(53, 360, 385, "Reunite");
    label(54, 15, 420, "Invite");
    label(55, 130, 420, "Accept");
    label(56, 245, 420, "Deny");
    label(57, 15, 225, "Summon yes");
    label(58, 130, 225, "Summon no");
    label(4, 245, 225, "Previous");
    label(5, 360, 225, "Next");
  } else if (tab == "marriage") {
    row("Wedding - " + s.marriage_view);
    if (s.proposal_id > 0)
      row(s.proposal_from + " has proposed. Accept or decline?");
    if (s.proposal_pending)
      row("Waiting for your partner's answer.");
    if (!s.groom_name.empty())
      row(s.groom_name + " and " + s.bride_name);
    if (s.relationship_id > 0)
      row(s.marriage_status == 3 ? "Marriage confirmed by server."
                                 : "Engagement confirmed by server.");
    if (s.invitation_type >= 0 && s.marriage_view == "invitation")
      row("Wedding invitation received. Use the Wedding Assistant to attend.");
    if (s.wishlist_pending)
      row("Enter up to ten desired items below.");
    if (s.ceremony_step >= 0) {
      static const char *steps[] = {"Ceremony gathering",
                                    "Heading to the altar", "Exchanging vows",
                                    "Ceremony celebration"};
      row(s.ceremony_ended ? "Wedding blessing / ceremony end"
                           : steps[std::min(3, s.ceremony_step)]);
    }
    label(80, 15, 385, "Propose");
    label(81, 130, 385, "Accept");
    label(82, 245, 385, "Decline");
    label(83, 360, 385, "Cancel proposal");
    label(84, 15, 420, "Invitation");
    label(85, 130, 420, "Send wishlist");
    label(86, 245, 420, "Bless couple");
    label(87, 360, 420, "Invite guest");
  } else if (tab == "wedding") {
    row("Wedding Gift Registry");
    for (size_t i = 0; i < s.wedding_wishlist.size() && i < 3; ++i)
      row(s.wedding_wishlist[i]);
    if (page >= s.wedding_gifts.size())
      page = 0;
    s.wedding_index = -1;
    if (!s.wedding_gifts.empty()) {
      s.wedding_index = page;
      auto g = s.wedding_gifts[page];
      row("Gift " + std::to_string(page + 1) + " / " +
          std::to_string(s.wedding_gifts.size()));
      row(ItemData::get(g.first).get_name() + " x" + std::to_string(g.second));
    } else
      row("No received gifts in this registry.");
    label(75, 15, 385, "Claim gift");
    label(4, 15, 420, "Previous");
    label(5, 130, 420, "Next");
  } else if (tab == "newyear") {
    row("New Year cards: send with a card in Use inventory.");
    if (page >= s.cards.size())
      page = 0;
    s.selected_card = 0;
    if (!s.cards.empty()) {
      auto it = s.cards.begin();
      std::advance(it, page);
      const auto &c = it->second;
      s.selected_card = c.id;
      row("From: " + c.from + "  To: " + (c.to.empty() ? "you" : c.to));
      row(c.received ? "Received" : "Awaiting receipt");
      row(c.message);
    } else
      row("No received card records or delivery notices.");
    label(70, 15, 385, "Receive");
  } else if (tab == "messenger") {
    row(s.messenger_from.empty()
            ? (s.messenger_joined      ? "Joined Messenger"
               : s.messenger_requested ? "Room requested"
                                       : "No Messenger room")
            : "Invitation from " + s.messenger_from);
    for (auto &m : s.members)
      row(m.second.name + " CH" + std::to_string(m.second.channel));
    size_t start = s.messages.size() > 3 ? s.messages.size() - 3 : 0;
    for (size_t i = start; i < s.messages.size(); ++i)
      row(s.messages[i]);
    label(20, 15, 385, "Create");
    label(21, 130, 385, "Invite");
    label(22, 245, 385, "Accept");
    label(23, 360, 385, "Deny");
    label(24, 245, 420, "Leave");
  } else {
    row("Received notes: select a sender to read/discard.");
    std::vector<const SocialNote *> visible;
    for (auto &n : s.notes)
      if (!n.discarded)
        visible.push_back(&n);
    if (page >= visible.size())
      page = 0;
    if (!visible.empty()) {
      auto &n = *visible[page];
      row("From: " + n.from + " (fame " + std::to_string(n.fame) + ")");
      row(n.message);
      row_targets.push_back(n.from);
      target.change_text(n.from);
      s.selected_note = n.id;
    }
    label(30, 15, 385, "Discard");
  }
  if (tab != "party" && tab != "family" && tab != "guide" && tab != "wedding" &&
      tab != "marriage") {
    label(4, 15, 420, "Previous");
    label(5, 130, 420, "Next");
    label(6, 360, 420, "Send");
  }
}
void UISocial::draw(float) const {
  auto &s = SocialState::get();
  ++s.draws;
  ColorBox(480, 470, Color::Name::BLACK, 0.96f).draw(position);
  for (auto &l : labels) {
    auto p = buttons.at(l.first)->bounds(position).get_left_top();
    ColorBox(buttons.at(l.first)->bounds(position).width(), 24,
             Color::Name::WHITE, 1)
        .draw(p);
    l.second.draw(p + Point<int16_t>(5, 4));
  }
  int y = 48;
  for (auto &r : rows) {
    r.draw(position + Point<int16_t>(15, y),
           Range<int16_t>(position.y() + 44, position.y() + 255));
    y += std::max<int>(22, r.height() + 3);
  }
  status_label.draw(position + Point<int16_t>(15, 260),
                    Range<int16_t>(position.y() + 260, position.y() + 289));
  if (tab != "guide" && tab != "wedding")
    recipient_label.draw(position + Point<int16_t>(15, 285));
  if (tab != "guide" && tab != "wedding")
    message_label.draw(position + Point<int16_t>(15, 328));
  if (tab != "guide" && tab != "wedding")
    ColorBox(450, 23, Color::Name::DARKGREY, 1)
        .draw(position + Point<int16_t>(15, 300));
  if (tab != "guide" && tab != "family" && tab != "wedding")
    ColorBox(450, 23, Color::Name::DARKGREY, 1)
        .draw(position + Point<int16_t>(15, 345));
  if (tab == "messenger") {
    int i = 0;
    for (const auto &m : s.members) {
      if (m.second.look)
        m.second.look->draw(position + Point<int16_t>(425, 105 + 70 * i), false,
                            Stance::STAND1, Expression::DEFAULT);
      ++i;
    }
  }
  if (tab != "guide" && tab != "wedding")
    target.draw(position);
  if (tab != "guide" && tab != "family" && tab != "wedding")
    body.draw(position);
}
void UISocial::update() {
  UIElement::update();
  target.update(position);
  body.update(position);
  if (tab == "guide") {
    auto &player = Stage::get().get_player();
    auto &s = SocialState::get();
    std::string signature;
    for (auto type : {InventoryType::USE, InventoryType::ETC})
      for (const auto &slot : player.get_inventory().list_slots(type))
        for (auto n : slot)
          signature += std::to_string(n) + ",";
    if (s.incubator_item)
      signature += player.get_quests().progress(
          ItemData::get(s.incubator_item).quest_id);
    if (signature != s.incubator_signature) {
      s.incubator_signature = signature;
      revision = static_cast<uint64_t>(-1);
    }
  }
  if (revision != SocialState::get().revision)
    refresh();
}
void UISocial::close() {
  if (tab == "wedding")
    SocialRequest(0x8a, 8).dispatch();
  target.set_state(Textfield::DISABLED);
  body.set_state(Textfield::DISABLED);
  deactivate();
}
Cursor::State UISocial::send_cursor(bool pressed, Point<int16_t> p) {
  if (pressed) {
    target.set_state(Textfield::NORMAL);
    body.set_state(Textfield::NORMAL);
  }
  auto a = tab == "guide" || tab == "wedding" ? Cursor::IDLE
                                              : target.send_cursor(p, pressed);
  auto b = tab == "guide" || tab == "family" || tab == "wedding"
               ? Cursor::IDLE
               : body.send_cursor(p, pressed);
  if (a != Cursor::IDLE)
    return a;
  if (b != Cursor::IDLE)
    return b;
  if (pressed)
    UIElement::send_cursor(false, p);
  return UIElement::send_cursor(pressed, p);
}
void UISocial::send_key(int32_t key, bool pressed, bool escape) {
  if (pressed && escape)
    close();
}
bool UISocial::act(const std::string &action, const std::string &name,
                   const std::string &message) {
  auto &s = SocialState::get();
  auto error = s.request(action, name, message);
  if (!error.empty()) {
    s.changed(error);
    return false;
  }
  return true;
}
Button::State UISocial::button_pressed(uint16_t id) {
  if (id <= 2 || id == 7 || id == 8 || id == 9) {
    tab = id == 0   ? "buddy"
          : id == 1 ? "messenger"
          : id == 7 ? "party"
          : id == 8 ? "family"
          : id == 9 ? "guide"
                    : "notes";
    page = 0;
    // Rebuild after UIElement finishes iterating the current button map.
    revision = static_cast<uint64_t>(-1);
  } else if (id >= 100 && id < 100 + row_targets.size()) {
    target.change_text(row_targets[id - 100]);
    // Rebuild after UIElement finishes iterating the current button map.
    revision = static_cast<uint64_t>(-1);
  } else if (id == 3)
    close();
  else if (id == 4) {
    if (page)
      --page;
    // Rebuild after UIElement finishes iterating the current button map.
    revision = static_cast<uint64_t>(-1);
  } else if (id == 5) {
    ++page;
    // Rebuild after UIElement finishes iterating the current button map.
    revision = static_cast<uint64_t>(-1);
  } else {
    std::string action;
    switch (id) {
    case 60:
      action = "guide-open";
      break;
    case 61:
      action = "incubator-next";
      break;
    case 62:
      action = "incubator-food";
      break;
    case 63:
      action = "incubator-feed";
      break;
    case 50:
      action = "family-open";
      break;
    case 51:
      action = "family-pedigree";
      break;
    case 52:
      action = "family-summon";
      break;
    case 53:
      action = "family-reunite";
      break;
    case 54:
      action = "family-invite";
      break;
    case 55:
      action = "family-accept";
      break;
    case 56:
      action = "family-deny";
      break;
    case 57:
      action = "family-summon-accept";
      break;
    case 58:
      action = "family-summon-deny";
      break;
    case 40:
      action = "party-create";
      break;
    case 41:
      action = "party-invite";
      break;
    case 42:
      action = "party-accept";
      break;
    case 43:
      action = "party-deny";
      break;
    case 44:
      action = "party-kick";
      break;
    case 45:
      action = "party-leader";
      break;
    case 46:
      action = "party-search-start";
      break;
    case 47:
      action = "party-search-stop";
      break;
    case 48:
      action = "party-leave";
      break;
    case 6:
      action = tab == "newyear" ? "newyear-send"
               : tab == "buddy" ? "buddy-send"
               : tab == "notes" ? "note-send"
                                : "messenger-send";
      break;
    case 10:
      action = "buddy-add";
      break;
    case 11:
      action = "buddy-remove";
      break;
    case 12:
      action = "buddy-accept";
      break;
    case 13:
      action = "buddy-deny";
      break;
    case 20:
      action = "messenger-create";
      break;
    case 21:
      action = "messenger-invite";
      break;
    case 22:
      action = "messenger-accept";
      break;
    case 23:
      action = "messenger-deny";
      break;
    case 24:
      action = "messenger-leave";
      break;
    case 87:
      action = "wedding-invite";
      break;
    case 80:
      action = "wedding-propose";
      break;
    case 81:
      action = "wedding-accept";
      break;
    case 82:
      action = "wedding-deny";
      break;
    case 83:
      action = "wedding-cancel";
      break;
    case 84:
      action = "wedding-invitation";
      break;
    case 85:
      action = "wedding-wishlist";
      break;
    case 86:
      action = "wedding-bless";
      break;
    case 75:
      action = "wedding-claim";
      break;
    case 70:
      action = "newyear-receive";
      break;
    case 30:
      action = "note-discard";
      break;
    }
    if (!action.empty())
      act(action, target.get_text(), body.get_text());
  }
  return Button::NORMAL;
}
void SocialWindowHandler::handle(InPacket &r) const {
  auto &s = SocialState::get();
  if (opcode == 0x4b) {
    s.partner_map = r.read_int();
    s.partner_id = r.read_int();
    s.changed("Partner location updated.");
    return;
  }
  if (opcode == 0x140 || opcode == 0x141) {
    int step = opcode == 0x140 ? uint8_t(r.read_byte()) : s.ceremony_step;
    int groom = r.read_int(), bride = r.read_int();
    if (step < -1 || step > 3 || r.available())
      throw std::runtime_error("Invalid wedding ceremony packet");
    s.ceremony_step = step;
    s.ceremony_ended = opcode == 0x141;
    s.groom_id = groom;
    s.bride_id = bride;
    s.marriage_view = "ceremony";
    if (opcode == 0x141 && (groom > 0 || bride > 0)) {
      for (int id : {groom, bride})
        if (auto c = Stage::get().get_character(id)) {
          c->show_attack_effect(
              Animation(nl::nx::effect["BasicEff.img"]["Wedding"]), 1);
          ++s.ceremony_effects;
        }
    }
    s.changed(opcode == 0x141 ? "Wedding blessing received."
                              : "Wedding ceremony advanced.");
    open_marriage();
    return;
  }
  int mode = static_cast<uint8_t>(r.read_byte());
  if (opcode == 0x48 || opcode == 0x49) {
    if (opcode == 0x48 && mode == 0) {
      auto name = r.read_string();
      int id = r.read_int();
      if (!name_valid(name) || id <= 0)
        throw std::runtime_error("Invalid proposal");
      s.proposal_from = name;
      s.proposal_id = id;
      s.marriage_view = "proposal";
      s.changed("Received engagement proposal.");
    } else if (opcode == 0x48 && mode == 9) {
      s.wishlist_pending = true;
      s.marriage_view = "wishlist";
      s.changed("Wedding wishlist requested.");
    } else if (opcode == 0x49 && mode == 15) {
      s.groom_name = r.read_string();
      s.bride_name = r.read_string();
      s.invitation_type = r.read_short();
      s.marriage_view = "invitation";
      s.changed("Wedding invitation opened.");
    } else if (opcode == 0x49 && mode == 11) {
      s.relationship_id = r.read_int();
      s.groom_id = r.read_int();
      s.bride_id = r.read_int();
      s.marriage_status = r.read_short();
      r.read_int();
      r.read_int();
      s.groom_name = r.read_padded_string(13);
      s.bride_name = r.read_padded_string(13);
      s.proposal_pending = false;
      s.proposal_id = 0;
      s.proposal_from.clear();
      s.marriage_view = "engaged";
      s.changed("Engagement confirmed.");
    } else if (opcode == 0x49 && (mode == 0 || mode == 36)) {
      if (mode == 36) {
        r.read_byte();
        r.read_string();
      }
      s.proposal_pending = false;
      s.changed("Engagement request ended; see server notice.");
    } else
      throw std::runtime_error("Unsupported marriage result");
    if (r.available())
      throw std::runtime_error("Marriage packet tail");
    open_marriage();
    return;
  }
  if (opcode == 0x4a) {
    auto wishlist = s.wedding_wishlist;
    std::vector<std::pair<int, int>> gifts;
    if (mode == 9 || mode == 0x0b) {
      wishlist.clear();
      int count = uint8_t(r.read_byte());
      if (count > 32)
        throw std::runtime_error("Wedding wishlist limit");
      for (int i = 0; i < count; ++i)
        wishlist.push_back(r.read_string());
    }
    if (mode == 0x0a || mode == 0x0b || mode == 0x0f) {
      r.read_long();
      int count = uint8_t(r.read_byte());
      for (int i = 0; i < count; ++i) {
        auto peek = r;
        peek.read_byte();
        int id = peek.read_int();
        if (id < 1000000 || id >= 6000000)
          throw std::runtime_error("Wedding gift item");
        auto type = static_cast<InventoryType::Id>(id / 1000000);
        Inventory preview;
        ItemParser::parse_item(r, type, 1, preview);
        gifts.emplace_back(id, preview.get_item_count(type, 1));
      }
    }
    if (mode != 9 && mode != 0x0a && mode != 0x0b && mode != 0x0c &&
        mode != 0x0e && mode != 0x0f)
      throw std::runtime_error("Unsupported wedding gift result");
    s.wedding_received = true;
    s.wedding_mode = mode;
    s.wedding_wishlist = std::move(wishlist);
    s.wedding_gifts = std::move(gifts);
    s.changed(mode == 0x0c || mode == 0x0e ? "Wedding gift request refused."
              : mode == 0x0f ? "Wedding gift collected; registry updated."
                             : "Wedding registry received.");
    UI::get().emplace<UISocial>("wedding");
  } else if (opcode == 0x76) {
    if (mode == 4 || mode == 6) {
      auto c = NewYearCard::read(r);
      s.cards[c.id] = c;
      s.changed(mode == 4 ? "New Year card sent." : "New Year card received.");
    } else if (mode == 0x0c) {
      NewYearCard c;
      c.id = r.read_int();
      c.from = r.read_string();
      if (!s.cards.count(c.id))
        s.cards[c.id] = c;
      s.changed("New Year card delivery from " + c.from);
    } else if (mode == 0x0d) {
      int id = r.read_int();
      int user = r.read_int();
      auto c = s.cards.find(id);
      if (c != s.cards.end() && user == Stage::get().get_player().get_oid()) {
        c->second.received = true;
        s.changed("New Year card receipt confirmed.");
      }
      return;
    } else if (mode == 0x0e) {
      s.cards.erase(r.read_int());
      s.changed("New Year card removed.");
    } else if (mode == 5 || mode == 7 || mode == 9 || mode == 0x0b) {
      int code = uint8_t(r.read_byte());
      s.changed(code == 0x13   ? "New Year recipient not found."
                : code == 0x0f ? "Cannot send a card to yourself."
                : code == 0x10
                    ? "New Year inventory is full."
                    : "New Year request refused: " + std::to_string(code));
    } else
      throw std::runtime_error("Unsupported New Year result " +
                               std::to_string(mode));
    UI::get().emplace<UISocial>("newyear");
  } else if (opcode == 0x3f) {
    if (mode == 7) {
      int count = static_cast<uint8_t>(r.read_byte());
      std::vector<SocialBuddy> next;
      for (int i = 0; i < count; ++i) {
        SocialBuddy b;
        b.id = r.read_int();
        b.name = r.read_padded_string(13);
        r.skip(1);
        b.channel = r.read_int() + 1;
        b.group = r.read_padded_string(13);
        r.skip(4);
        next.push_back(b);
      }
      r.skip(count * 4);
      s.buddies = std::move(next);
      s.changed("Buddy list received.");
    } else if (mode == 9) {
      s.buddy_invite = r.read_int();
      s.buddy_from = r.read_string();
      s.changed("Buddy invitation from " + s.buddy_from);
      UI::get().emplace<UISocial>("buddy");
    } else if (mode == 0x15) {
      s.capacity = static_cast<uint8_t>(r.read_byte());
      s.changed("Buddy capacity received.");
    } else
      s.changed("Buddy response " + std::to_string(mode));
  } else if (opcode == 0x139) {
    if (mode == 0 || mode == 7) {
      int pos = r.read_byte();
      auto look = std::make_shared<CharLook>(LoginParser::parse_look(r));
      auto name = r.read_string();
      int channel = r.read_byte() + 1;
      r.skip(1);
      s.members[pos] = {name, channel, look};
      s.messenger_joined = true;
      s.changed(name + " joined Messenger.");
    } else if (mode == 1) {
      s.position = r.read_byte();
      s.messenger_joined = true;
      s.changed("Joined Messenger.");
    } else if (mode == 2) {
      int pos = r.read_byte();
      auto member = s.members.find(pos);
      std::string name =
          member == s.members.end() ? "Member" : member->second.name;
      s.members.erase(pos);
      s.changed(name + " left Messenger.");
    } else if (mode == 3) {
      s.messenger_from = r.read_string();
      r.skip(1);
      s.messenger_invite = r.read_int();
      r.skip(1);
      s.changed("Messenger invitation from " + s.messenger_from);
      UI::get().emplace<UISocial>("messenger");
    } else if (mode == 4 || mode == 5) {
      auto name = r.read_string();
      bool ok = r.read_bool();
      s.changed(name + (mode == 5 ? " declined Messenger."
                        : ok      ? ": Messenger invitation delivered."
                                  : ": Messenger unavailable."));
    } else if (mode == 6) {
      auto text = r.read_string();
      s.messages.push_back(text);
      if (s.messages.size() > 64)
        s.messages.pop_front();
      s.changed(text);
      line("[Messenger] " + text);
    } else
      throw std::runtime_error("Unsupported Messenger mode " +
                               std::to_string(mode));
  } else if (opcode == 0x29) {
    if (mode != 3)
      throw std::runtime_error("Unsupported note mode " + std::to_string(mode));
    int count = static_cast<uint8_t>(r.read_byte());
    std::vector<SocialNote> next;
    for (int i = 0; i < count; ++i) {
      SocialNote n;
      n.id = r.read_int();
      n.from = r.read_string();
      while (!n.from.empty() && n.from.back() == ' ')
        n.from.pop_back();
      n.message = r.read_string();
      n.time = r.read_long();
      n.fame = r.read_byte();
      next.push_back(n);
    }
    s.notes = std::move(next);
    s.changed("Received " + std::to_string(count) + " notes.");
  }
}
} // namespace ms

namespace ms {
void FamilyWindowHandler::handle(InPacket &r) const {
  auto &s = SocialState::get();
  std::string message = "Family update";
  auto count = [&](int limit) {
    int n = r.read_int();
    if (n < 0 || n > limit)
      throw std::runtime_error("Invalid family entry count");
    return n;
  };
  switch (opcode) {
  case 0x64: {
    std::vector<FamilyPrivilege> next;
    int n = count(64);
    for (int i = 0; i < n; ++i) {
      FamilyPrivilege p;
      p.category = r.read_byte();
      p.cost = r.read_int();
      r.read_int();
      p.name = r.read_string();
      p.description = r.read_string();
      next.push_back(p);
    }
    s.privileges = std::move(next);
    break;
  }
  case 0x5f: {
    s.family_rep = r.read_int();
    s.family_total = r.read_int();
    s.family_today = r.read_int();
    s.family_juniors = r.read_short();
    r.skip(4);
    s.family_leader = r.read_int();
    s.family_name = r.read_string();
    s.family_message = r.read_string();
    int n = count(64);
    std::map<int, bool> used;
    for (int i = 0; i < n; ++i) {
      int id = r.read_int();
      used[id] = r.read_int() != 0;
    }
    s.family_used = std::move(used);
    s.family_received = true;
    message = "Family information received";
    break;
  }
  case 0x5e: {
    r.read_int();
    int n = count(4096);
    std::vector<FamilyMember> next;
    for (int i = 0; i < n; ++i) {
      FamilyMember m;
      m.id = r.read_int();
      m.senior = r.read_int();
      m.job = r.read_short();
      m.level = static_cast<uint8_t>(r.read_byte());
      m.online = r.read_bool();
      m.reputation = r.read_int();
      m.total = r.read_int();
      m.to_senior = r.read_int();
      m.today = r.read_int();
      m.channel = r.read_int();
      r.read_int();
      m.name = r.read_string();
      next.push_back(m);
    }
    int tails = count(4098);
    if (tails < 2)
      throw std::runtime_error("Invalid pedigree tail");
    r.skip(16);
    for (int i = 2; i < tails; ++i)
      r.skip(8);
    r.skip(6);
    s.pedigree = std::move(next);
    s.pedigree_received = true;
    message = "Pedigree received";
    break;
  }
  case 0x60: {
    int result = r.read_int();
    int mesos = r.read_int();
    message = "Family result " + std::to_string(result);
    if (mesos)
      message += "; mesos " + std::to_string(mesos);
    break;
  }
  case 0x61:
    s.family_invite = r.read_int();
    s.family_from = r.read_string();
    message = "Family invitation from " + s.family_from;
    UI::get().emplace<UISocial>("family");
    break;
  case 0x62: {
    bool accepted = r.read_bool();
    message = r.read_string() + (accepted ? " joined the family"
                                          : " declined the family invitation");
    break;
  }
  case 0x63:
    message = "Senior: " + r.read_string();
    r.read_int();
    break;
  case 0x65: {
    int gain = r.read_int();
    message = "Reputation " + std::to_string(gain) + " from " + r.read_string();
    break;
  }
  case 0x66: {
    bool online = r.read_bool();
    message = r.read_string() + (online ? " logged in" : " logged out");
    break;
  }
  case 0x68:
    s.summon_from = r.read_string();
    s.summon_family = r.read_string();
    message = "Summon from " + s.summon_from;
    UI::get().emplace<UISocial>("family");
    break;
  }
  s.changed(message);
}
} // namespace ms
