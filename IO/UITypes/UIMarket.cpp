// Fan-client market controls. Server packets own stock, visitors and sale
// results.
#include "UIMarket.h"
#include "../../Data/ItemData.h"
#include "../../Gameplay/Stage.h"
#include "../../Graphics/Geometry.h"
#include "../../HeadlessState.h"
#include "../../Net/Handlers/Helpers/ItemParser.h"
#include "../../Net/Handlers/Helpers/LoginParser.h"
#include "../../Net/Handlers/InventoryHandlers.h"
#include "../../Net/OutPacket.h"
#include "../Components/AreaButton.h"
#include "../UI.h"
#include <sstream>
#include <stdexcept>
namespace ms {
namespace {
class Request : public OutPacket {
public:
  Request(int op, int mode = -1) : OutPacket(op) {
    if (mode >= 0)
      write_byte(mode);
  }
  using OutPacket::write_byte;
  using OutPacket::write_int;
  using OutPacket::write_short;
  using OutPacket::write_string;
};
void read_stock(InPacket &p, MarketState &s) {
  int n = uint8_t(p.read_byte());
  if (n > 16)
    throw std::runtime_error("Market listing limit");
  std::vector<MarketListing> rows;
  for (int i = 0; i < n; ++i) {
    MarketListing r;
    r.bundles = p.read_short();
    r.quantity = p.read_short();
    r.price = p.read_int();
    auto peek = p;
    peek.read_byte();
    r.item = peek.read_int();
    if (r.item < 1000000 || r.item >= 6000000)
      throw std::runtime_error("Invalid market item");
    Inventory preview;
    ItemParser::parse_item(p, static_cast<InventoryType::Id>(r.item / 1000000),
                           1, preview);
    rows.push_back(r);
  }
  s.listings = std::move(rows);
}
void sold(InPacket &p, MarketState &s) {
  int n = uint8_t(p.read_byte());
  if (n > 255)
    throw std::runtime_error("Market sales limit");
  for (int i = 0; i < n; ++i) {
    int item = p.read_int(), q = p.read_short(), price = p.read_int();
    auto buyer = p.read_string();
    s.messages.push_back(buyer + " bought " + std::to_string(q) + " x " +
                         ItemData::get(item).get_name() + " for " +
                         std::to_string(price));
  }
}
} // namespace
MarketState &MarketState::get() {
  static MarketState s;
  return s;
}
void MarketState::changed(const std::string &s) {
  status = s;
  ++revision;
  while (messages.size() > 32)
    messages.pop_front();
  HeadlessState::event("market",
                       ",\"text\":\"" + HeadlessState::escape(s) + "\"");
}
std::string MarketState::request(const std::string &action,
                                 const std::string &args) {
  std::istringstream in(args);
  int a = 0, b = 0, c = 0, d = 0, e = 0;
  std::string tail;
  auto done = [&]() { return !(in >> tail); };
  if (action == "create") {
    if (active)
      return "Leave the current room first.";
    if (!(in >> a))
      return "Choose a received Cash permit ID, then a description.";
    std::getline(in >> std::ws, tail);
    if (tail.empty() || tail.size() > 40)
      return "Description must be 1-40 characters.";
    if (a / 10000 != 514 && a / 10000 != 503)
      return "Select a store permit or hired merchant.";
    bool owned = false;
    for (auto row : Stage::get().get_player().get_inventory().list_slots(
             InventoryType::CASH))
      if (row[1] == a && row[2] > 0)
        owned = true;
    if (!owned)
      return "Permit is not in received Cash inventory.";
    Request p(0x7b, 0);
    p.write_byte(a / 10000 == 514 ? 4 : 5);
    p.write_string(tail);
    p.write_byte(0);
    p.write_short(0);
    p.write_int(a);
    p.dispatch();
  } else if (action == "visit") {
    if (active)
      return "Leave the current room first.";
    if (!(in >> a) || !done() || !boxes.count(a))
      return "Select a received shop owner ID from the directory.";
    Request p(0x7b, 4);
    p.write_int(boxes.at(a).object);
    p.dispatch();
  } else if (action == "owl") {
    auto &owl = OwlSearchState::get();
    if (active)
      return "Leave the current room first.";
    if (!(in >> a) || !done() || a < 0 || size_t(a) >= owl.rows.size())
      return "Choose a received Owl result index.";
    Request p(0x43);
    p.write_int(owl.rows[a].owner_id);
    p.write_int(owl.rows[a].map);
    p.dispatch();
  } else {
    if (!active)
      return "Enter a shop room first.";
    if (action == "stock") {
      if (!owner)
        return "Only the owner can stock items.";
      if (!(in >> a >> b >> c >> d >> e) || !done() || a < 1 || a > 4 ||
          b < 1 || b > 32767 || c < 1 || d < 1 || c > 2000 || d > 2000 ||
          int64_t(c) * d > 2000 || e < 1)
        return "Stock: inventory(1-4) slot bundles quantity price.";
      auto &inv = Stage::get().get_player().get_inventory();
      auto t = static_cast<InventoryType::Id>(a);
      bool enough = false;
      for (auto row : inv.list_slots(t))
        if (row[0] == b && row[2] >= c * d)
          enough = true;
      if (!enough)
        return "Received inventory has insufficient stock.";
      Request p(0x7b, type == 4 ? 0x16 : 0x21);
      p.write_byte(a);
      p.write_short(b);
      p.write_short(c);
      p.write_short(d);
      p.write_int(e);
      p.dispatch();
    } else if (action == "buy") {
      if (owner)
        return "Owners cannot buy their own stock.";
      if (!(in >> a >> b) || !done() || a < 0 || size_t(a) >= listings.size() ||
          b < 1 || b > listings[a].bundles)
        return "Buy: listing index and available bundle count.";
      Request p(0x7b, type == 4 ? 0x17 : 0x22);
      p.write_byte(a);
      p.write_short(b);
      p.dispatch();
    } else if (action == "chat") {
      if (args.empty() || args.size() > 127)
        return "Chat must be 1-127 characters.";
      Request p(0x7b, 6);
      p.write_string(args);
      p.dispatch();
    } else if (action == "ban") {
      if (!owner || type != 4)
        return "Only a player-shop owner can ban.";
      if (!(in >> tail) || !(tail.size() <= 13))
        return "Choose a visitor name.";
      bool found = false;
      for (auto &v : visitors)
        if (v.second == tail)
          found = true;
      if (!found)
        return "Choose a received visitor name.";
      Request p(0x7b, 0x1c);
      p.write_byte(0);
      p.write_string(tail);
      p.dispatch();
    } else if (action == "take") {
      if (!owner || !(in >> a) || !done() || a < 0 ||
          size_t(a) >= listings.size())
        return "Take: received listing index (owner only).";
      Request p(0x7b, type == 4 ? 0x1b : 0x26);
      p.write_short(a);
      p.dispatch();
    } else if (action == "open") {
      if (!owner || !args.empty())
        return "Open requires an owner and no arguments.";
      Request p(0x7b, 0x0b);
      p.write_byte(0);
      p.dispatch();
    } else if (action == "withdraw" || action == "organize" ||
               action == "resume") {
      if (!owner || type != 5 || !args.empty())
        return "Hired-merchant owner only, no arguments.";
      Request(0x7b, action == "withdraw"   ? 0x2b
                    : action == "organize" ? 0x28
                                           : 0x27)
          .dispatch();
    } else if (action == "leave" || action == "close") {
      if (!args.empty())
        return "Leave/close takes no arguments.";
      if (action == "close" && !owner)
        return "Only the owner can close.";
      Request(0x7b, action == "close" && type == 5 ? 0x29 : 0x0a).dispatch();
      if (action == "leave") {
        active = false;
        visitors.clear();
      }
    } else
      return "Unknown market action.";
  }
  pending = action;
  changed("Requested " + action + "; wait for server response.");
  return "";
}
void MarketOwlResultHandler::handle(InPacket &p) const {
  int code = uint8_t(p.read_byte());
  std::string message = code == 1    ? "Shop no longer exists."
                        : code == 2  ? "Shop is full."
                        : code == 3  ? "Shop moved."
                        : code == 17 ? "You are banned from this shop."
                        : code == 18
                            ? "Shop is closed."
                            : "Owl entry result " + std::to_string(code);
  MarketState::get().changed(message);
  UI::get().emplace<UIMarket>();
}
void MarketHandler::handle(InPacket &p) const {
  auto &s = MarketState::get();
  int mode = uint8_t(p.read_byte());
  if (mode == 5) {
    int type = uint8_t(p.read_byte());
    if (type == 0) {
      s.changed("Room refused: " + std::to_string(uint8_t(p.read_byte())));
      UI::get().emplace<UIMarket>();
      return;
    }
    if (type != 4 && type != 5) {
      s.changed("Unsupported interaction room type " + std::to_string(type));
      return;
    }
    MarketState next;
    next.boxes = s.boxes;
    next.type = type;
    next.active = true;
    p.read_byte();
    if (type == 4) {
      next.slot = uint8_t(p.read_byte());
      next.owner = next.slot == 0;
      sold(p, next);
      LoginParser::parse_look(p);
      next.name = p.read_string();
    } else {
      next.slot = p.read_short();
      next.owner = next.slot == 0;
      p.read_int();
      p.read_string();
    }
    int count = 0;
    while (true) {
      int slot = uint8_t(p.read_byte());
      if (slot == 255)
        break;
      if (++count > 3 || slot < 1 || slot > 3)
        throw std::runtime_error("Invalid market visitor");
      LoginParser::parse_look(p);
      next.visitors[slot] = p.read_string();
    }
    if (type == 5) {
      int n = p.read_short();
      if (n < 0 || n > 1024)
        throw std::runtime_error("Market chat limit");
      for (int i = 0; i < n; ++i) {
        auto m = p.read_string();
        p.read_byte();
        next.messages.push_back(m);
      }
      next.name = p.read_string();
      if (next.owner) {
        p.read_short();
        p.read_short();
        p.read_byte();
        sold(p, next);
        next.mesos = p.read_int();
      }
    }
    if (type == 4 && !next.owner)
      for (auto &v : next.visitors)
        if (v.second == Stage::get().get_player().get_name())
          next.slot = v.first;
    next.description = p.read_string();
    p.read_byte();
    if (type == 5)
      next.mesos = p.read_int();
    read_stock(p, next);
    next.revision = s.revision + 1;
    s = std::move(next);
    s.changed("Received shop room.");
    UI::get().emplace<UIMarket>();
  } else if (mode == 4) {
    int slot = uint8_t(p.read_byte());
    if (slot < 1 || slot > 3)
      throw std::runtime_error("Market visitor slot");
    LoginParser::parse_look(p);
    auto name = p.read_string();
    for (auto it = s.visitors.begin(); it != s.visitors.end();)
      if (it->second == name)
        it = s.visitors.erase(it);
      else
        ++it;
    s.visitors[slot] = name;
    s.changed("Visitor joined.");
  } else if (mode == 6) {
    p.read_byte();
    p.read_byte();
    s.messages.push_back(p.read_string());
    s.changed("Received shop chat.");
  } else if (mode == 0x19) {
    if (s.type == 5)
      s.mesos = p.read_int();
    read_stock(p, s);
    s.changed("Received stock update.");
  } else if (mode == 0x1a) {
    int index = uint8_t(p.read_byte()), q = p.read_short();
    auto buyer = p.read_string();
    s.messages.push_back(buyer + " bought " + std::to_string(q) +
                         " from listing " + std::to_string(index));
    s.changed("Received sale.");
  } else if (mode == 0xa) {
    if (!p.available()) {
      s.active = false;
      s.visitors.clear();
      s.changed("Room closed.");
    } else {
      int slot = uint8_t(p.read_byte());
      int reason = p.available() ? uint8_t(p.read_byte()) : 0;
      if (reason != 0 || slot == 0 || slot == s.slot) {
        s.active = false;
        s.visitors.clear();
        s.changed("Left room; result " + std::to_string(reason));
      } else {
        s.visitors.erase(slot);
        s.changed("Visitor left.");
      }
    }
  } else if (mode == 0x2a) {
    s.active = false;
    s.visitors.clear();
    s.changed("Merchant owner room closed.");
  } else
    s.changed("Unhandled shop result " + std::to_string(mode));
}
template <int Op> void MarketBoxHandler<Op>::handle(InPacket &p) const {
  auto &s = MarketState::get();
  int owner = p.read_int();
  // Java refreshes the box after destruction; updates cannot recreate it.
  if constexpr (Op == 0x10b)
    if (!s.boxes.count(owner))
      return;
  if constexpr (Op == 0x10a) {
    s.boxes.erase(owner);
  } else {
    MarketBox box{};
    box.owner = owner;
    if constexpr (Op == 0x109) {
      p.read_int();
      box.position = p.read_point();
      p.read_short();
      box.name = p.read_string();
    }
    box.type = uint8_t(p.read_byte());
    if (!box.type) {
      s.boxes.erase(owner);
      if (owner == Stage::get().get_player().get_oid() && s.type == 4)
        s.active = false;
      s.changed("Shop removed.");
      return;
    }
    if (box.type != 4 && box.type != 5)
      return;
    box.object = p.read_int();
    box.description = p.read_string();
    box.label = Text(Text::A12M, Text::CENTER, Color::Name::WHITE,
                     box.description, 180);
    if constexpr (Op == 0x10b) {
      auto old = s.boxes.find(owner);
      if (old != s.boxes.end())
        box.position = old->second.position;
    }
    s.boxes[owner] = box;
    if constexpr (Op == 0x109 || Op == 0x10b) {
      if (owner == Stage::get().get_player().get_oid() && s.type == 5 &&
          (s.pending == "open" || s.pending == "resume")) {
        s.active = false;
        s.pending.clear();
      }
    }
  }
  s.changed("Received shop directory update.");
}
template class MarketBoxHandler<0xa5>;
template class MarketBoxHandler<0x109>;
template class MarketBoxHandler<0x10a>;
template class MarketBoxHandler<0x10b>;
UIMarket::UIMarket()
    : UIElement(Point<int16_t>(130, 70), Point<int16_t>(540, 490)) {
  UI::get().remove_textfield();
  args = Textfield(Text::A12M, Text::LEFT, Color::Name::WHITE,
                   Rectangle<int16_t>(15, 525, 330, 353), 127);
  args.set_horizontal_scroll(true);
  args.set_state(Textfield::NORMAL);
  const char *names[] = {"create", "visit",    "stock",    "open",   "buy",
                         "chat",   "ban",      "take",     "leave",  "close",
                         "owl",    "withdraw", "organize", "resume", "previous",
                         "next",   "hide"};
  for (int i = 0; i < 17; ++i) {
    actions[i] = names[i];
    buttons[i] = std::make_unique<AreaButton>(
        Point<int16_t>(15 + (i % 6) * 86, 365 + (i / 6) * 32),
        Point<int16_t>(80, 26));
  }
  refresh();
}
void UIMarket::refresh() {
  auto &s = MarketState::get();
  revision = s.revision;
  std::string text =
      s.active
          ? (s.type == 4 ? "Player shop: " : "Hired merchant: ") + s.name +
                " - " + s.description + "\\n" +
                (s.owner ? "Owner" : "Visitor") +
                (s.type == 5 ? " | Merchant balance " + std::to_string(s.mesos)
                             : "") +
                " | wallet " +
                std::to_string(
                    Stage::get().get_player().get_inventory().get_meso()) +
                "\\n"
          : "Shop directory (owner ID: description)\\n";
  std::vector<std::string> rows;
  if (!s.active) {
    for (auto &b : s.boxes)
      rows.push_back(std::to_string(b.first) + ": " + b.second.description);
    for (size_t i = 0; i < OwlSearchState::get().rows.size(); ++i) {
      auto &r = OwlSearchState::get().rows[i];
      rows.push_back("Owl " + std::to_string(i) + ": " + r.owner + " " +
                     std::to_string(r.price) + " mesos");
    }
  } else {
    for (auto &v : s.visitors)
      rows.push_back("Visitor " + std::to_string(v.first) + ": " + v.second);
    for (size_t i = 0; i < s.listings.size(); ++i) {
      auto &r = s.listings[i];
      rows.push_back(
          std::to_string(i) + ": " + ItemData::get(r.item).get_name() + " x" +
          std::to_string(r.quantity) + " | " + std::to_string(r.bundles) +
          " bundles @ " + std::to_string(r.price));
    }
    for (auto &m : s.messages)
      rows.push_back(m);
  }
  if (page * 7 >= rows.size())
    page = rows.empty() ? 0 : (rows.size() - 1) / 7;
  for (size_t i = page * 7; i < rows.size() && i < page * 7 + 7; ++i)
    text += rows[i] + "\\n";
  content = Text(Text::A12M, Text::LEFT, Color::Name::WHITE, text, 510);
  help = Text(Text::A11M, Text::LEFT, Color::Name::WHITE,
              "Create: permitID description | Visit: ownerID | Owl: result "
              "index\\nStock: inventory slot bundles quantity price | Buy: "
              "index bundles\\nBan: visitor name | Take: index | Chat: "
              "message; other actions: empty",
              510);
  status = Text(Text::A11M, Text::LEFT, Color::Name::WHITE, s.status, 510);
}
void UIMarket::update() {
  UIElement::update();
  args.update(position);
  auto received_wallet = Stage::get().get_player().get_inventory().get_meso();
  if (revision != MarketState::get().revision || wallet != received_wallet) {
    wallet = received_wallet;
    refresh();
  }
}
void UIMarket::draw(float) const {
  ColorBox(540, 490, Color::Name::BLACK, 0.96f).draw(position);
  content.draw(position + Point<int16_t>(15, 12));
  status.draw(position + Point<int16_t>(15, 232));
  help.draw(position + Point<int16_t>(15, 275));
  ColorBox(510, 23, Color::Name::WHITE, 1.0f)
      .draw(position + Point<int16_t>(15, 330));
  ColorBox(508, 21, Color::Name::BLACK, 1.0f)
      .draw(position + Point<int16_t>(16, 331));
  args.draw(position);
  for (auto &a : actions) {
    auto b = buttons.at(a.first)->bounds(position);
    ColorBox(80, 26, Color::Name::WHITE, 1.0f).draw(b.get_left_top());
    Text(Text::A11M, Text::LEFT, Color::Name::BLACK, a.second)
        .draw(b.get_left_top() + Point<int16_t>(5, 4));
  }
  ++MarketState::get().draws;
}
bool UIMarket::act(const std::string &a, const std::string &v) {
  auto error = MarketState::get().request(a, v);
  if (!error.empty()) {
    MarketState::get().changed(error);
    return false;
  }
  return true;
}
Button::State UIMarket::button_pressed(uint16_t id) {
  auto action = actions.at(id);
  if (action == "hide") {
    args.set_state(Textfield::DISABLED);
    deactivate();
  } else if (action == "previous") {
    if (page)
      --page;
    revision = ~uint64_t(0);
  } else if (action == "next") {
    ++page;
    revision = ~uint64_t(0);
  } else if (act(action, args.get_text()))
    args.change_text("");
  return Button::State::NORMAL;
}
Cursor::State UIMarket::send_cursor(bool pressed, Point<int16_t> p) {
  if (pressed)
    args.set_state(Textfield::NORMAL);
  auto c = args.send_cursor(p, pressed);
  if (c != Cursor::IDLE)
    return c;
  if (pressed)
    UIElement::send_cursor(false, p);
  return UIElement::send_cursor(pressed, p);
}
void UIMarket::send_key(int32_t, bool pressed, bool escape) {
  if (pressed && escape) {
    args.set_state(Textfield::DISABLED);
    deactivate();
  }
}
void UIMarket::snapshot_controls(std::ostream &o) const {
  o << ",\"input\":\"" << HeadlessState::escape(args.get_text())
    << "\",\"rows\":\"" << HeadlessState::escape(content.get_text())
    << "\",\"controls\":[";
  bool comma = false;
  for (auto &a : actions) {
    if (comma)
      o << ',';
    comma = true;
    auto p = buttons.at(a.first)->bounds(position).get_left_top();
    o << "{\"label\":\"" << a.second << "\",\"x\":" << p.x()
      << ",\"y\":" << p.y() << '}';
  }
  o << ']';
}
void MarketState::snapshot(std::ostream &o) const {
  o << ",\"market\":{\"active\":" << (active ? "true" : "false")
    << ",\"owner\":" << (owner ? "true" : "false") << ",\"type\":" << type
    << ",\"slot\":" << slot << ",\"mesos\":" << mesos
    << ",\"draw_calls\":" << draws << ",\"name\":\""
    << HeadlessState::escape(name) << "\",\"status\":\""
    << HeadlessState::escape(status) << "\",\"listings\":[";
  bool comma = false;
  for (auto &r : listings) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"item\":" << r.item << ",\"bundles\":" << r.bundles
      << ",\"quantity\":" << r.quantity << ",\"price\":" << r.price << '}';
  }
  o << "],\"visitors\":{";
  comma = false;
  for (auto &v : visitors) {
    if (comma)
      o << ',';
    comma = true;
    o << '"' << v.first << "\":\"" << HeadlessState::escape(v.second) << '"';
  }
  o << "},\"boxes\":[";
  comma = false;
  for (auto &b : boxes) {
    if (comma)
      o << ',';
    comma = true;
    o << "{\"owner\":" << b.first << ",\"object\":" << b.second.object
      << ",\"type\":" << b.second.type << ",\"description\":\""
      << HeadlessState::escape(b.second.description) << "\"}";
  }
  o << ']';
  if (auto ui = UI::get().get_element<UIMarket>(); ui && ui->is_active())
    ui->snapshot_controls(o);
  o << '}';
}
} // namespace ms
