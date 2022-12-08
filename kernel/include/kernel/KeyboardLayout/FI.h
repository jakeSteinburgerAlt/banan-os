#pragma once

#include <kernel/Keyboard.h>

namespace Keyboard
{

	static Key scs2_to_key_altgr[0xFF]
	{
		Key::INVALID,
		Key::F9,
		Key::INVALID,
		Key::F5,
		Key::F3,
		Key::F1,
		Key::F2,
		Key::F12,
		Key::INVALID,
		Key::F10,
		Key::F8,
		Key::F6,
		Key::F4,
		Key::Tab,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::Alt,
		Key::LeftShift,
		Key::INVALID,
		Key::Ctrl,
		Key::Q,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::At,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::Euro,
		Key::Dollar,
		Key::Pound,
		Key::INVALID,
		Key::INVALID,
		Key::Space,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::OpenBrace,
		Key::OpenBracket,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::CloseBrace,
		Key::CloseBracket,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::None,
		Key::BackSlash,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::None,
		Key::INVALID,
		Key::None,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::CapsLock,
		Key::RightShift,
		Key::Enter,
		Key::Tilde,
		Key::INVALID,
		Key::None,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Pipe,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Backspace,
		Key::INVALID,
		Key::INVALID,
		Key::Numpad1,
		Key::INVALID,
		Key::Numpad4,
		Key::Numpad7,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Numpad0,
		Key::NumpadComma,
		Key::Numpad2,
		Key::Numpad5,
		Key::Numpad6,
		Key::Numpad8,
		Key::Escape,
		Key::NumLock,
		Key::F11,
		Key::NumpadPlus,
		Key::Numpad3,
		Key::NumpadMinus,
		Key::NumpadMult,
		Key::Numpad9,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::F7,
	};

	static Key scs2_to_key_shift[0xFF]
	{
		Key::INVALID,
		Key::F9,
		Key::INVALID,
		Key::F5,
		Key::F3,
		Key::F1,
		Key::F2,
		Key::F12,
		Key::INVALID,
		Key::F10,
		Key::F8,
		Key::F6,
		Key::F4,
		Key::Tab,
		Key::Half,
		Key::INVALID,
		Key::INVALID,
		Key::Alt,
		Key::LeftShift,
		Key::INVALID,
		Key::Ctrl,
		Key::Q,
		Key::ExclamationMark,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Z,
		Key::S,
		Key::A,
		Key::W,
		Key::DoubleQuote,
		Key::INVALID,
		Key::INVALID,
		Key::C,
		Key::X,
		Key::D,
		Key::E,
		Key::Currency,
		Key::Hashtag,
		Key::INVALID,
		Key::INVALID,
		Key::Space,
		Key::V,
		Key::F,
		Key::T,
		Key::R,
		Key::Percent,
		Key::INVALID,
		Key::INVALID,
		Key::N,
		Key::B,
		Key::H,
		Key::G,
		Key::Y,
		Key::Ampersand,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::M,
		Key::J,
		Key::U,
		Key::Slash,
		Key::OpenParen,
		Key::INVALID,
		Key::INVALID,
		Key::Semicolon,
		Key::K,
		Key::I,
		Key::O,
		Key::Equals,
		Key::CloseParen,
		Key::INVALID,
		Key::INVALID,
		Key::Colon,
		Key::Underscore,
		Key::L,
		Key::O_Dots,
		Key::P,
		Key::QuestionMark,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::A_Dots,
		Key::INVALID,
		Key::A_Dot,
		Key::BackTick,
		Key::INVALID,
		Key::INVALID,
		Key::CapsLock,
		Key::RightShift,
		Key::Enter,
		Key::Caret,
		Key::INVALID,
		Key::Asterix,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::MoreThan,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Backspace,
		Key::INVALID,
		Key::INVALID,
		Key::End,
		Key::INVALID,
		Key::Left,
		Key::Right,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Insert,
		Key::Delete,
		Key::Down,
		Key::None,
		Key::Right,
		Key::Up,
		Key::Escape,
		Key::NumLock,
		Key::F11,
		Key::NumpadPlus,
		Key::PageDown,
		Key::NumpadMinus,
		Key::NumpadMult,
		Key::PageUp,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::F7,
	};

	static Key scs2_to_key[0xFF]
	{
		Key::INVALID,
		Key::F9,
		Key::INVALID,
		Key::F5,
		Key::F3,
		Key::F1,
		Key::F2,
		Key::F12,
		Key::INVALID,
		Key::F10,
		Key::F8,
		Key::F6,
		Key::F4,
		Key::Tab,
		Key::Section,
		Key::INVALID,
		Key::INVALID,
		Key::Alt,
		Key::LeftShift,
		Key::INVALID,
		Key::Ctrl,
		Key::Q,
		Key::_1,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Z,
		Key::S,
		Key::A,
		Key::W,
		Key::_2,
		Key::INVALID,
		Key::INVALID,
		Key::C,
		Key::X,
		Key::D,
		Key::E,
		Key::_4,
		Key::_3,
		Key::INVALID,
		Key::INVALID,
		Key::Space,
		Key::V,
		Key::F,
		Key::T,
		Key::R,
		Key::_5,
		Key::INVALID,
		Key::INVALID,
		Key::N,
		Key::B,
		Key::H,
		Key::G,
		Key::Y,
		Key::_6,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::M,
		Key::J,
		Key::U,
		Key::_7,
		Key::_8,
		Key::INVALID,
		Key::INVALID,
		Key::Comma,
		Key::K,
		Key::I,
		Key::O,
		Key::_0,
		Key::_9,
		Key::INVALID,
		Key::INVALID,
		Key::Period,
		Key::Hyphen,
		Key::L,
		Key::O_Dots,
		Key::P,
		Key::Plus,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::A_Dots,
		Key::INVALID,
		Key::A_Dot,
		Key::Tick,
		Key::INVALID,
		Key::INVALID,
		Key::CapsLock,
		Key::RightShift,
		Key::Enter,
		Key::Caret,
		Key::INVALID,
		Key::SingleQuote,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::LessThan,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Backspace,
		Key::INVALID,
		Key::INVALID,
		Key::Numpad1,
		Key::INVALID,
		Key::Numpad4,
		Key::Numpad7,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::Numpad0,
		Key::NumpadComma,
		Key::Numpad2,
		Key::Numpad5,
		Key::Numpad6,
		Key::Numpad8,
		Key::Escape,
		Key::NumLock,
		Key::F11,
		Key::NumpadPlus,
		Key::Numpad3,
		Key::NumpadMinus,
		Key::NumpadMult,
		Key::Numpad9,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::INVALID,
		Key::F7,
	};

}