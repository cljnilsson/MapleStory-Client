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

#include "../UIElement.h"
#include <ostream>
#include <vector>

#include "../Graphics/Text.h"
#include "../Components/Slider.h"
#include "../Components/Textfield.h"

namespace ms
{
	class UINpcTalk : public UIElement
	{
	public:
		enum TalkType : int8_t
		{
			NONE = -1,
			SENDOK,
			SENDYESNO,

			// TODO: Unconfirmed
					SENDNEXT,
			SENDNEXTPREV,
			SENDACCEPTDECLINE,
			SENDGETTEXT,
			SENDGETNUMBER,
			SENDSIMPLE,
			LENGTH
		};

		static constexpr Type TYPE = UIElement::Type::NPCTALK;
		static constexpr bool FOCUSED = true;
		static constexpr bool TOGGLED = false;

		UINpcTalk();

		void draw(float inter) const override;

		void update() override;

		Cursor::State send_cursor(bool clicked, Point<int16_t> cursorpos) override;

		void send_key(int32_t keycode, bool pressed, bool escape) override;

		UIElement::Type get_type() const override;

		// Scripted-run response: -1 close, 0 no/prev, 1 yes/next/ok, or the
		// selection index for SENDSIMPLE; input dialogs accept close only here.
        // Text/number/style values go through submit_input.
		bool headless_respond(int32_t value);
        bool submit_input(const std::string &value);
        void configure_input(const std::string &initial, int32_t minimum, int32_t maximum,
                             const std::vector<int32_t> &styles);
        void write_input_state(std::ostream &out) const;

		void change_text(int32_t npcid, int8_t msgtype, int16_t style, int8_t speaker, const std::string &text);

	protected:
		Button::State button_pressed(uint16_t buttonid) override;

	private:
		TalkType get_by_value(int8_t value);

		std::string format_text(const std::string &tx, const int32_t &npcid);

		static constexpr int16_t MAX_HEIGHT = 248;

		enum Buttons
		{
			ALLLEVEL,
			CLOSE,
			MYLEVEL,
			NEXT,
			NO,
			OK,
			PREV,
			QAFTER,
			QCNO,
			QCYES,
			QGIVEUP,
			QNO,
			QSTART,
			QYES,
			YES
		};

		Texture top;
		Texture fill;
		Texture bottom;
		Texture nametag;
		Texture speaker;

		Text text;
		Text name;
        Textfield input;
        Text input_hint;
        int32_t input_min = 0, input_max = 0;
        std::vector<int32_t> input_styles;
        bool has_input() const { return raw_type == 2 || raw_type == 3 || raw_type == 7; }

		int16_t height;
		int16_t offset;
		int16_t unitrows;
		int16_t rowmax;
		int16_t min_height;

		bool show_slider;
		bool draw_text;
		Slider slider;
		TalkType type;
		int8_t raw_type = -1;
		std::string formatted_text;
		size_t formatted_text_pos;
		uint16_t timestep;

		std::function<void(bool)> onmoved;
	};
}