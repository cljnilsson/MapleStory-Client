// Server-owned MTS browse/cart state. No predicted trades or placeholder stock.
#include "UIMTS.h"
#include "../../Data/ItemData.h"
#include "../../Graphics/Geometry.h"
#include "../../HeadlessState.h"
#include "../../Net/Handlers/Helpers/CashShopParser.h"
#include "../../Net/Handlers/Helpers/ItemParser.h"
#include "../../Net/Packets/GameplayPackets.h"
#include "../Components/AreaButton.h"
#include "../UI.h"
#include <stdexcept>
namespace ms {
namespace {
class Request : public OutPacket {
public:
  Request(int mode) : OutPacket(0xfd) { write_byte(mode); }
  using OutPacket::write_int;
};
std::vector<MTSRow> read_rows(InPacket &r, int count) {
  if (count < 0 || count > 1024)
    throw std::runtime_error("MTS listing limit");
  std::vector<MTSRow> rows;
  for (int i = 0; i < count; ++i) {
    MTSRow row;
    auto peek = r;
    peek.read_byte();
    row.item = peek.read_int();
    if (row.item < 1000000 || row.item >= 6000000)
      throw std::runtime_error("MTS invalid item");
    Inventory preview;
    auto type = static_cast<InventoryType::Id>(row.item / 1000000);
    ItemParser::parse_item(r, type, 1, preview);
    row.quantity = preview.get_item_count(type, 1);
    row.id = r.read_int();
    row.fee = r.read_int();
    row.price = r.read_int();
    r.read_int();
    r.read_long();
    row.seller = r.read_string();
    r.read_string();
    r.skip(28);
    rows.push_back(row);
  }
  return rows;
}
} // namespace
MTSState &MTSState::get() {
  static MTSState s;
  return s;
}
void MTSState::changed(const std::string &v) {
  status = v;
  ++revision;
  HeadlessState::event("mts", ",\"text\":\"" + HeadlessState::escape(v) + "\"");
}
std::string MTSState::request(const std::string &action, int index) {
  if (!opened)
    return "MTS is not open.";
  if (action == "exit") {
    ChangeMapPacket().dispatch();
    changed("Return to field requested.");
    return "";
  }
  if (action == "browse" || action == "cart" || action == "next" ||
      action == "previous") {
    if (action == "next" && (page + 1) * 16 >= total)
      return "No next server page.";
    if (action == "previous" && page <= 0)
      return "Already on first server page.";
    Request p(5);
    p.write_int(action == "cart" ? 4 : action == "browse" ? 1 : tab);
    p.write_int(action == "cart" || action == "browse" ? 0 : type);
    p.write_int(action == "next"       ? page + 1
                : action == "previous" ? page - 1
                                       : 0);
    p.dispatch();
  } else if (action == "add" || action == "remove") {
    if (index < 0 || size_t(index) >= rows.size())
      return "Select a received listing.";
    if ((action == "remove") != (tab == 4))
      return "Use Add in Browse or Remove in Cart.";
    Request p(action == "add" ? 9 : 10);
    p.write_int(rows[index].id);
    p.dispatch();
  } else
    return "Unknown MTS action.";
  changed("Requested " + action + "; wait for server response.");
  return "";
}
void MTSState::snapshot(std::ostream &o) const {
  o << ",\"mts\":{\"opened\":" << (opened ? "true" : "false")
    << ",\"known\":" << (known ? "true" : "false") << ",\"prepaid\":" << prepaid
    << ",\"points\":" << points << ",\"tab\":" << tab << ",\"page\":" << page
    << ",\"total\":" << total << ",\"draw_calls\":" << draws << ",\"status\":\""
    << HeadlessState::escape(status) << "\",\"rows\":[";
  bool comma = false;
  for (const auto &r : rows) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"id\":" << r.id << ",\"item\":" << r.item
      << ",\"quantity\":" << r.quantity << ",\"price\":" << r.price
      << ",\"fee\":" << r.fee << ",\"seller\":\""
      << HeadlessState::escape(r.seller) << "\"}";
  }
  o << ']';
  if (auto panel = UI::get().get_element<UIMTS>())
    panel->snapshot(o);
  o << '}';
}
template <int Op> void MTSPacket<Op>::handle(InPacket &r) const {
  auto &s = MTSState::get();
  if constexpr (Op == 0x7e) {
    CashShopParser::parseCharacterInfo(r);
    r.read_string();
    r.skip(28);
    if (r.available())
      throw std::runtime_error("MTS entry tail");
    s = MTSState{};
    s.opened = true;
    s.changed("MTS opened; receiving listings.");
    UI::get().emplace<UIMTS>();
    HeadlessState::set_screen("mts");
  } else if constexpr (Op == 0x15b) {
    s.prepaid = r.read_int();
    s.points = r.read_int();
    s.changed("MTS balances received.");
  } else {
    int mode = uint8_t(r.read_byte());
    if (mode == 0x15) {
      int total = r.read_int(), count = r.read_int();
      int tab = r.read_int(), type = r.read_int(), page = r.read_int();
      r.read_byte();
      r.read_byte();
      auto rows = read_rows(r, count);
      r.read_byte();
      s.rows = std::move(rows);
      s.total = total;
      s.tab = tab;
      s.type = type;
      s.page = page;
      s.known = true;
      s.changed(tab == 4 ? "Cart received." : "Listings received.");
    } else if (mode == 0x21 || mode == 0x23) {
      auto rows = read_rows(r, r.read_int());
      if (mode == 0x21) {
        s.transfer = std::move(rows);
        r.skip(5);
      } else {
        if (rows.empty())
          r.read_int();
        s.selling = std::move(rows);
      }
      ++s.revision;
    } else if (mode == 0x3d) {
      r.read_int();
      r.read_int();
    } else
      throw std::runtime_error("Unsupported MTS result " +
                               std::to_string(mode));
  }
}
template class MTSPacket<0x7e>;
template class MTSPacket<0x15b>;
template class MTSPacket<0x15c>;
UIMTS::UIMTS() : UIElement(Point<int16_t>(80, 50), Point<int16_t>(640, 500)) {
  UI::get().remove_textfield();
  const char *names[] = {"browse",   "cart", "add", "remove",
                         "previous", "next", "exit"};
  for (int i = 0; i < 7; ++i) {
    actions[i] = names[i];
    buttons[i] = std::make_unique<AreaButton>(
        Point<int16_t>(15 + (i % 4) * 150, 410 + (i / 4) * 38),
        Point<int16_t>(140, 28));
  }
  refresh();
}
void UIMTS::refresh() {
  auto &s = MTSState::get();
  revision = s.revision;
  selected = -1;
  for (auto it = buttons.begin(); it != buttons.end();)
    if (it->first >= 100)
      it = buttons.erase(it);
    else
      ++it;
  listing_labels.clear();
  std::string text =
      "Maple Trading System | " + std::string(s.tab == 4 ? "Cart" : "Browse") +
      " | Page " + std::to_string(s.page + 1) +
      "\\nPrepaid: " + std::to_string(s.prepaid) +
      " | Maple Points: " + std::to_string(s.points) +
      "\\nSelect a listing, then Add or Remove; Cart refreshes membership.\\n";
  if (s.rows.empty())
    text += "No received listings.\\n";
  for (size_t i = 0; i < s.rows.size() && i < 16; ++i) {
    const auto &r = s.rows[i];
    listing_labels.emplace_back(
        Text::A11M, Text::LEFT, Color::Name::WHITE,
        std::to_string(i + 1) + ". " + ItemData::get(r.item).get_name() + " x" +
            std::to_string(r.quantity) + " | " + r.seller + " | " +
            std::to_string(r.price) + " + fee " + std::to_string(r.fee),
        610);
    buttons[100 + i] = std::make_unique<AreaButton>(
        Point<int16_t>(15, 78 + int(i) * 18), Point<int16_t>(610, 18));
  }
  content = Text(Text::A11M, Text::LEFT, Color::Name::WHITE, text, 610);
  status = Text(Text::A11M, Text::LEFT, Color::Name::WHITE, s.status, 610);
}
void UIMTS::update() {
  UIElement::update();
  if (revision != MTSState::get().revision)
    refresh();
}
void UIMTS::draw(float) const {
  ColorBox(640, 500, Color::Name::BLACK, .98f).draw(position);
  content.draw(position + Point<int16_t>(15, 12));
  for (size_t i = 0; i < listing_labels.size(); ++i)
    listing_labels[i].draw(position + Point<int16_t>(15, 78 + i * 18));
  status.draw(position + Point<int16_t>(15, 375));
  for (const auto &a : actions) {
    auto p = buttons.at(a.first)->bounds(position).get_left_top();
    ColorBox(140, 28, Color::Name::WHITE, 1).draw(p);
    Text(Text::A11M, Text::LEFT, Color::Name::BLACK, a.second)
        .draw(p + Point<int16_t>(5, 5));
  }
  if (selected >= 0)
    Text(Text::A11M, Text::LEFT, Color::Name::WHITE,
         "Selected listing " + std::to_string(selected + 1))
        .draw(position + Point<int16_t>(15, 350));
  ++MTSState::get().draws;
}
bool UIMTS::act(const std::string &a, int index) {
  auto &s = MTSState::get();
  auto err = s.request(a, index);
  if (!err.empty()) {
    s.changed(err);
    return false;
  }
  return true;
}
Button::State UIMTS::button_pressed(uint16_t id) {
  if (id >= 100) {
    selected = id - 100;
  } else if (actions.count(id))
    act(actions.at(id), selected);
  return Button::NORMAL;
}
void UIMTS::send_key(int32_t, bool pressed, bool escape) {
  if (pressed && escape)
    act("exit");
}
void UIMTS::snapshot(std::ostream &o) const {
  o << ",\"window\":{\"active\":" << (is_active() ? "true" : "false")
    << ",\"selected\":" << selected << ",\"rows\":\""
    << HeadlessState::escape(content.get_text()) << "\",\"controls\":[";
  bool comma = false;
  for (const auto &a : actions) {
    if (comma)
      o << ',';
    comma = true;
    auto p = buttons.at(a.first)->bounds(position).get_left_top();
    o << "{\"label\":\"" << a.second << "\",\"x\":" << p.x()
      << ",\"y\":" << p.y() << '}';
  }
  o << "],\"choices\":[";
  for (size_t i = 0; i < listing_labels.size(); ++i) {
    if (i)
      o << ',';
    auto p = buttons.at(100 + i)->bounds(position).get_left_top();
    o << "{\"index\":" << i << ",\"text\":\""
      << HeadlessState::escape(listing_labels[i].get_text())
      << "\",\"x\":" << p.x() << ",\"y\":" << p.y() << '}';
  }
  o << "]}";
}
} // namespace ms
