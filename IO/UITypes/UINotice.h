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

#include "../UIDragElement.h"

#include "../Components/Textfield.h"

namespace ms
{
	class UINotice : public UIDragElement<PosNOTICE>
	{
	public:
		static constexpr Type TYPE = UIElement::Type::NOTICE;
		static constexpr bool FOCUSED = true;
		static constexpr bool TOGGLED = false;

	protected:
		enum NoticeType : uint8_t
		{
			YESNO,
			ENTERNUMBER,
			OK
		};

		UINotice(std::string message, NoticeType type, Text::Alignment alignment);

		UINotice(std::string message, NoticeType type);

		void draw(bool textfield) const;

		int16_t box2offset(bool textfield) const;

	private:
		Texture top;
		Texture center;
		Texture centerbox;
		Texture box;
		Texture box2;
		Texture bottom;
		Texture bottombox;
		Text question;
		int16_t height;
		NoticeType type;
		Text::Alignment alignment;
	};

	class UIYesNo : public UINotice
	{
	public:
		UIYesNo(std::string message, std::function<void(bool yes)> yesnohandler, Text::Alignment alignment);

		UIYesNo(std::string message, std::function<void(bool yes)> yesnohandler);

		void draw(float alpha) const override;

		void send_key(int32_t keycode, bool pressed, bool escape) override;

		UIElement::Type get_type() const override;

	protected:
		Button::State button_pressed(uint16_t buttonid) override;

	private:
		enum Buttons : int16_t
		{
			YES, NO
		};

		std::function<void(bool yes)> yesnohandler;
	};

	class UIEnterNumber : public UINotice
	{
	public:
		UIEnterNumber(std::string message, std::function<void(int32_t number)> numhandler, int32_t max,
					  int32_t quantity);

		void draw(float alpha) const override;

		void update() override;

		Cursor::State send_cursor(bool pressed, Point<int16_t> cursorpos) override;

		void send_key(int32_t keycode, bool pressed, bool escape) override;

		UIElement::Type get_type() const override;

	protected:
		Button::State button_pressed(uint16_t buttonid) override;

	private:
		void handlestring(std::string numstr);

		enum Buttons : int16_t
		{
			OK, CANCEL
		};

		std::function<void(int32_t number)> numhandler;
		Textfield numfield;
		int32_t max;
	};

	// One cash-item request; values remain server-owned until update packets arrive.
	class UICashItemDialog : public UINotice
	{
	public:
		UICashItemDialog(int16_t slot, int32_t item);
		void draw(float alpha) const override;
		void update() override;
		Cursor::State send_cursor(bool pressed, Point<int16_t> cursorpos) override;
		void send_key(int32_t keycode, bool pressed, bool escape) override;
		UIElement::Type get_type() const override { return TYPE; }
		static bool supports(int32_t item);
		static std::string request(int16_t slot, int32_t item, const std::string &values);
		int16_t get_slot() const { return slot; }
		int32_t get_item() const { return item; }
		const std::string &get_values() const { return values.get_text(); }
		const std::string &get_error() const { return last_error; }
	protected:
		Button::State button_pressed(uint16_t buttonid) override;
	private:
		void submit();
		int16_t slot;
		int32_t item;
		std::string last_error;
		Textfield values;
		Text error;
		Text prompt;
		Text confirm_label;
		Text cancel_label;
	};

	// Paged server results, with a skin-independent readable layout.
	class UICashResult : public UINotice
	{
	public:
		UICashResult(std::string title, std::vector<std::string> rows, std::string kind);
		void draw(float alpha) const override;
		Cursor::State send_cursor(bool pressed, Point<int16_t> cursor) override;
		void send_key(int32_t key, bool pressed, bool escape) override;
		UIElement::Type get_type() const override { return TYPE; }
	protected:
		Button::State button_pressed(uint16_t id) override;
	private:
		void refresh();
		std::string title, kind;
		std::vector<std::string> rows;
		size_t page = 0;
		Text body, close_label, next_label, prev_label;
	};
	class UIVegaResult : public UINotice
	{
	public:
		UIVegaResult();
		void draw(float alpha) const override;
		void update() override;
		void send_key(int32_t key, bool pressed, bool escape) override;
		UIElement::Type get_type() const override { return TYPE; }
	protected:
		Button::State button_pressed(uint16_t) override;
	private:
		Animation animation;
		int phase = -1;
		mutable int drawn_phase = -1;
		Text status, close_label;
	};

	class UIOk : public UINotice
	{
	public:
		UIOk(std::string message, std::function<void(bool ok)> okhandler);

		void draw(float alpha) const override;

		void send_key(int32_t keycode, bool pressed, bool escape) override;

		UIElement::Type get_type() const override;

	protected:
		Button::State button_pressed(uint16_t buttonid) override;

	private:
		enum Buttons : int16_t
		{
			OK
		};

		std::function<void(bool ok)> okhandler;
	};
}