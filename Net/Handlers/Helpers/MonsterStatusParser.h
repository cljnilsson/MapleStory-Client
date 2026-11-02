//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton					//
//																//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by	//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.										//
//																//
//	This program is distributed in the hope that it will be useful,			//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of			//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the			//
//	GNU Affero General Public License for more details.							//
//																//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../../InPacket.h"

#include <cstdint>

namespace ms::MonsterStatusParser
{
	// Java MaplePacketCreator.encodeTemporary: four mask words, one 8-byte
	// record for each set status, then optional reflect counters and probability.
	inline void skip_temporary(InPacket& recv)
	{
		const uint32_t firstLow = static_cast<uint32_t>(recv.read_int());
		const uint32_t firstHigh = static_cast<uint32_t>(recv.read_int());
		const uint32_t secondLow = static_cast<uint32_t>(recv.read_int());
		const uint32_t secondHigh = static_cast<uint32_t>(recv.read_int());
		const uint32_t first = firstLow | firstHigh;
		const uint32_t second = secondLow | secondHigh;

		uint32_t remaining = first;
		size_t count = 0;
		while (remaining != 0)
		{
			remaining &= remaining - 1;
			++count;
		}
		remaining = second;
		while (remaining != 0)
		{
			remaining &= remaining - 1;
			++count;
		}
		recv.skip(count * 8);

		const bool weaponReflect = (first & 0x20000000u) != 0;
		const bool magicReflect = (first & 0x40000000u) != 0;
		if (weaponReflect) recv.skip_int();
		if (magicReflect) recv.skip_int();
		if (weaponReflect || magicReflect) recv.skip_int();
	}
}
