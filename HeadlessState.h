#pragma once

#include <string>

#include "Net/InPacket.h"

namespace ms
{
	// Optional state export for scripted fan-client runs. Active only when
	// MS_HEADLESS_DIR is set; writes <dir>/state.json atomically.
	namespace HeadlessState
	{
		// Record the current screen name (the HEADLESS_STATE marker value).
		void set_screen(const char *name);

		// Append one JSON object to events.jsonl; `fields` is a comma-led JSON
		// fragment such as ",\"text\":\"hi\"" (empty allowed). No-op when disabled.
		void event(const char *type, const std::string &fields = "");

		// JSON string escape (without quotes).
		std::string escape(const std::string &s);

		// Track the open NPC dialog for the snapshot (type is the raw msgtype byte).
		void set_dialog(int32_t npcid, int8_t type, const std::string &text);
		void clear_dialog();

		// Parse a PARTY_OPERATION packet into the party snapshot / events.
		void on_party(InPacket &recv);

		// Call once per main-loop update; throttled and a no-op when disabled.
		void update();
	}
}
