#pragma once

#include <string>
#include <vector>
#include <cstdint>

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
		// Record packet decode and dispatch failures without including packet contents.
		void packet_error(int32_t opcode, size_t packet_length, size_t parser_offset,
						 const std::string &reason, bool handled);

		// JSON string escape (without quotes).
		std::string escape(const std::string &s);

		// Track the open NPC dialog for the snapshot (type is the raw msgtype byte).
		void set_dialog(int32_t npcid, int8_t type, const std::string &text);
		void clear_dialog();

		// Parse a PARTY_OPERATION packet into the party snapshot / events.
		void on_party(InPacket &recv);
		std::string party_request(const std::string &action, const std::string &target, const std::string &options);
		std::vector<std::string> party_rows();

		// Bounded, opt-in diagnostic observations. IDs correlate model objects with
		// their first draw invocation; drawing is not a pixel/visibility assertion.
		bool observing();
		uint64_t presentation(const char *boundary, const char *kind, int64_t value = 0, int32_t actor = 0, uint64_t id = 0);
		void begin_frame();
		void mob_status(int32_t oid, const std::string &json);
		void buff_changed(int stat, int source, int duration, int value);

		// Call once per main-loop update; throttled and a no-op when disabled.
		void update();
	}
}
