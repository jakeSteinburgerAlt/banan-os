#pragma once

#include <BAN/Traits.h>
#include <stddef.h>

namespace BAN
{


	template<typename, size_t> class Array;
	template<typename> class ErrorOr;
	template<typename> class Function;
	template<typename> class Queue;
	class String;
	class StringView;
	template<typename> class Vector;
	template<typename> class LinkedList;
	template<typename... Ts> requires (!is_const_v<Ts> && ...) class Variant;

}
