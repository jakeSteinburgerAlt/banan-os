#pragma once

#include <BAN/HashMap.h>
#include <kernel/ACPI/AML/Node.h>
#include <kernel/ACPI/AML/ParseContext.h>
#include <kernel/ACPI/AML/Pkg.h>

namespace Kernel::ACPI::AML
{

	struct IfElse
	{
		static ParseResult parse(ParseContext& context)
		{
			ASSERT(context.aml_data.size() >= 1);
			ASSERT(static_cast<Byte>(context.aml_data[0]) == Byte::IfOp);
			context.aml_data = context.aml_data.slice(1);

			auto if_pkg = AML::parse_pkg(context.aml_data);
			if (!if_pkg.has_value())
				return ParseResult::Failure;

			auto outer_aml_data = context.aml_data;
			context.aml_data = if_pkg.value();

			auto predicate = AML::parse_object(context);
			if (!predicate.success())
				return ParseResult::Failure;
			auto predicate_node = predicate.node();
			if (!predicate_node)
			{
				AML_ERROR("If predicate is not an integer");
				return ParseResult::Failure;
			}
			auto predicate_integer = predicate_node->as_integer();
			if (!predicate_integer.has_value())
			{
				AML_ERROR("If predicate is not an integer");
				return ParseResult::Failure;
			}

			// Else
			if (!predicate_integer.value())
			{
				if (outer_aml_data.size() < 1 || static_cast<Byte>(outer_aml_data[0]) != Byte::ElseOp)
					context.aml_data = BAN::ConstByteSpan();
				else
				{
					outer_aml_data = outer_aml_data.slice(1);
					auto else_pkg = AML::parse_pkg(outer_aml_data);
					if (!else_pkg.has_value())
						return ParseResult::Failure;
					context.aml_data = else_pkg.value();
				}
			}

			while (context.aml_data.size() > 0)
			{
				auto object_result = AML::parse_object(context);
				if (object_result.returned())
					return ParseResult(ParseResult::Result::Returned, object_result.node());
				if (!object_result.success())
					return ParseResult::Failure;
			}

			context.aml_data = outer_aml_data;

			return ParseResult::Success;
		}
	};

}
