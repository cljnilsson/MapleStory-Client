// Fan-client MTS controls; wire layouts follow HeavenMS
// MaplePacketCreator/MTSHandler.
#pragma once
#include "../../Graphics/Text.h"
#include "../../Net/PacketHandler.h"
#include "../UIElement.h"
#include <ostream>
namespace ms {
struct MTSRow {
  int id = 0, item = 0, quantity = 0, price = 0, fee = 0;
  std::string seller;
};
struct MTSState {
  static MTSState &get();
  bool opened = false, known = false;
  int prepaid = 0, points = 0, tab = 1, type = 0, page = 0, total = 0;
  std::vector<MTSRow> rows, transfer, selling;
  std::string status;
  uint64_t revision = 0;
  mutable uint64_t draws = 0;
  void changed(const std::string &);
  std::string request(const std::string &, int index = -1);
  void snapshot(std::ostream &) const;
};
class UIMTS : public UIElement {
public:
  static constexpr Type TYPE = UIElement::MTS;
  static constexpr bool FOCUSED = true, TOGGLED = false;
  UIMTS();
  Type get_type() const override { return TYPE; }
  void update() override;
  void draw(float) const override;
  void send_key(int32_t, bool, bool) override;
  Button::State button_pressed(uint16_t) override;
  bool act(const std::string &, int index = -1);
  void snapshot(std::ostream &) const;

private:
  int selected = -1;
  uint64_t revision = ~uint64_t(0);
  Text content, status;
  std::vector<Text> listing_labels;
  std::map<int, std::string> actions;
  void refresh();
};
template <int Opcode> class MTSPacket : public PacketHandler {
public:
  void handle(InPacket &) const override;
};
} // namespace ms
