#pragma once
// Compatibility lookup for older (v83) UI.nx data.
// Prefer the existing newer layout when present, otherwise use UIWindow.img.
#include <nlnx/nx.hpp>
#include <nlnx/node.hpp>
#include <string>
namespace ms { namespace v83ui {
inline nl::node window(const std::string& section) {
    nl::node newer = nl::nx::ui["UIWindow2.img"][section];
    if (newer) return newer;
    return nl::nx::ui["UIWindow.img"][section];
}
inline nl::node minimap(const std::string& section) {
    nl::node found = window(section);
    if (found) return found;
    return window("MiniMap");
}
} }
