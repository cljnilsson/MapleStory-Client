// Fan-client market controls. Wire layouts follow HeavenMS MaplePacketCreator.
#pragma once
#include "../../Net/PacketHandler.h"
#include "../Components/Textfield.h"
#include "../UIElement.h"
#include <deque>
#include <ostream>
namespace ms {
struct MarketListing {
  int item, bundles, quantity, price;
};
struct MarketBox {
  int owner, object, type;
  std::string name, description;
  Point<int16_t> position;
  Text label;
};
struct MarketState {
  static MarketState &get();
  bool active = false, owner = false;
  int type = 0, slot = 0, mesos = 0;
  std::string name, description, status, pending;
  std::map<int, std::string> visitors;
  std::map<int, MarketBox> boxes;
  std::vector<MarketListing> listings;
  std::deque<std::string> messages;
  uint64_t revision = 0;
  mutable uint64_t draws = 0;
  void changed(const std::string &s);
  std::string request(const std::string &action, const std::string &args);
  void snapshot(std::ostream &) const;
};
class UIMarket : public UIElement {
public:
  static constexpr Type TYPE = UIElement::MARKET;
  static constexpr bool FOCUSED = true, TOGGLED = false;
  UIMarket();
  Type get_type() const override { return TYPE; }
  void update() override;
  void draw(float) const override;
  Cursor::State send_cursor(bool, Point<int16_t>) override;
  void send_key(int32_t, bool, bool) override;
  Button::State button_pressed(uint16_t) override;
  bool act(const std::string &, const std::string &);
  void snapshot_controls(std::ostream &) const;

private:
  Textfield args;
  Text content, help, status;
  std::map<int, std::string> actions;
  uint64_t revision = ~uint64_t(0);
  int64_t wallet = -1;
  size_t page = 0;
  void refresh();
};
class MarketOwlResultHandler : public PacketHandler {
public:
  void handle(InPacket &) const override;
};
class MarketHandler : public PacketHandler {
public:
  void handle(InPacket &) const override;
};
template <int Opcode> class MarketBoxHandler : public PacketHandler {
public:
  void handle(InPacket &) const override;
};
} // namespace ms
