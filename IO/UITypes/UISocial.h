// Fan-client social controls. Protocol layouts follow HeavenMS v83.
#pragma once
#include "../../Character/Look/CharLook.h"
#include "../../Net/PacketHandler.h"
#include "../Components/Textfield.h"
#include "../UIElement.h"
#include <deque>
#include <ostream>
namespace ms {
struct NewYearCard {
  int32_t id = 0, sender = 0, receiver = 0;
  std::string from, to, message;
  bool sender_discarded = false, receiver_discarded = false, received = false;
  int64_t sent = 0, accepted = 0;
  static NewYearCard read(InPacket &);
};
struct SocialBuddy {
  int32_t id;
  std::string name, group;
  int channel;
};
struct SocialNote {
  int32_t id;
  std::string from, message;
  int64_t time;
  int fame;
  bool discarded = false;
};
struct MessengerMember {
  std::string name;
  int channel = 0;
  std::shared_ptr<CharLook> look;
};
struct FamilyMember {
  int32_t id, senior, reputation, total, to_senior, today, channel;
  int job, level;
  bool online;
  std::string name;
};
struct FamilyPrivilege {
  int category, cost;
  std::string name, description;
};
class SocialState {
public:
  static SocialState &get();
  std::map<int32_t, NewYearCard> cards;
  int32_t selected_card = 0;
  // Wedding requests are pending until the matching server result arrives.
  std::string marriage_view = "proposal", proposal_from, groom_name, bride_name;
  int32_t proposal_id = 0, relationship_id = 0, groom_id = 0, bride_id = 0;
  int partner_map = 0, partner_id = 0, ceremony_step = -1, invitation_type = -1;
  int marriage_status = 0;
  bool proposal_pending = false, wishlist_pending = false,
       ceremony_ended = false;
  uint64_t ceremony_effects = 0;
  bool wedding_received = false;
  int wedding_mode = 0, wedding_index = -1;
  std::vector<std::pair<int, int>> wedding_gifts;
  std::vector<std::string> wedding_wishlist;
  int32_t incubator_item = 0;
  int incubator_slot = 0, food_type = 0, food_slot = 0;
  std::string incubator_signature;
  int family_rep = 0, family_total = 0, family_today = 0, family_juniors = 0;
  int32_t family_leader = 0, family_invite = 0;
  std::string rates_hint;
  std::string family_name, family_message, family_from, summon_from,
      summon_family;
  bool family_received = false, pedigree_received = false;
  std::vector<FamilyMember> pedigree;
  std::vector<FamilyPrivilege> privileges;
  std::map<int, bool> family_used;
  std::vector<SocialBuddy> buddies;
  std::vector<SocialNote> notes;
  std::map<int, MessengerMember> members;
  int32_t buddy_invite = 0, messenger_invite = 0;
  std::string buddy_from, messenger_from, status;
  bool messenger_requested = false, messenger_joined = false;
  int position = -1, capacity = 0;
  int32_t selected_note = 0;
  std::deque<std::string> messages;
  uint64_t revision = 0;
  mutable uint64_t draws = 0;
  std::string request(const std::string &action, const std::string &target,
                      const std::string &text);
  void changed(const std::string &message);
  void snapshot(std::ostream &out) const;
};
class UISocial : public UIElement {
public:
  static constexpr Type TYPE = UIElement::SOCIAL;
  static constexpr bool FOCUSED = true, TOGGLED = false;
  explicit UISocial(std::string tab = "buddy");
  Type get_type() const override { return TYPE; }
  void draw(float alpha) const override;
  void update() override;
  Cursor::State send_cursor(bool pressed, Point<int16_t> cursor) override;
  void send_key(int32_t key, bool pressed, bool escape) override;
  Button::State button_pressed(uint16_t id) override;
  bool act(const std::string &action, const std::string &name,
           const std::string &message);
  std::string tab;
  void snapshot_controls(std::ostream &out) const;

private:
  void refresh();
  void close();
  Textfield target, body;
  Text status_label, recipient_label, message_label;
  std::vector<Text> rows;
  std::map<uint16_t, Text> labels;
  std::vector<std::string> row_targets;
  size_t page = 0;
  uint64_t revision = static_cast<uint64_t>(-1);
};
class SocialWindowHandler : public PacketHandler {
public:
  explicit SocialWindowHandler(int opcode) : opcode(opcode) {}
  void handle(InPacket &recv) const override;

private:
  int opcode;
};
template <int Opcode> class SocialWindowPacket : public SocialWindowHandler {
public:
  SocialWindowPacket() : SocialWindowHandler(Opcode) {}
};
} // namespace ms

namespace ms {
class FamilyWindowHandler : public PacketHandler {
public:
  explicit FamilyWindowHandler(int opcode) : opcode(opcode) {}
  void handle(InPacket &r) const override;

private:
  int opcode;
};
template <int Opcode> class FamilyWindowPacket : public FamilyWindowHandler {
public:
  FamilyWindowPacket() : FamilyWindowHandler(Opcode) {}
};
} // namespace ms
