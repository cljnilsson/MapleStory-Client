//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton						//
//																				//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by	//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.											//
//																				//
//	This program is distributed in the hope that it will be useful,				//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of				//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the				//
//	GNU Affero General Public License for more details.							//
//																				//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Sprite.h"
#include "../HeadlessState.h"

#include "../Constants.h"

#include <map>
#include <list>

namespace ms
{
	// A list of animations. Animations will be removed after all frames were displayed.
	class EffectLayer
	{
	public:
		void set_actor(int32_t value) { actor = value; }

		void drawbelow(Point<int16_t> position, float alpha) const;

		void drawabove(Point<int16_t> position, float alpha) const;

		void update();

		void add(const Animation &effect, const DrawArgument &args, int8_t z, float speed);

		void add(const Animation &effect, const DrawArgument &args, int8_t z);

		void add(const Animation &effect, const DrawArgument &args);

		void add(const Animation &effect);

	private:
		class Effect
		{
		public:
			Effect(const Animation &a, const DrawArgument &args, float s, int32_t owner) : actor(owner), observation(HeadlessState::presentation("model", "effect", 0, owner)), sprite(a, args), speed(s)
			{}

			void draw(Point<int16_t> position, float alpha) const
			{
				sprite.draw(position, alpha);
				if (observation && !observed_draw)
				{
					HeadlessState::presentation("render", "effect", 0, actor, observation);
					observed_draw = true;
				}
			}

			bool update()
			{
				bool ended = sprite.update(static_cast<uint16_t>(Constants::TIMESTEP * speed));
				if (ended && observation) HeadlessState::presentation("end", "effect", 0, actor, observation);
				return ended;
			}

		private:
			int32_t actor = 0;
			uint64_t observation = 0;
			mutable bool observed_draw = false;
			Sprite sprite;
			float speed;
		};

		int32_t actor = 0;
		std::map<int8_t, std::list<Effect>> effects;
	};
}