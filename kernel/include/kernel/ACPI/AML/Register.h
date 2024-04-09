#pragma once

#include <kernel/ACPI/AML/Node.h>

namespace Kernel::ACPI::AML
{

	struct Register : public AML::Node
	{
		BAN::RefPtr<AML::Node> value;

		Register()
			: Node(Node::Type::Register)
		{}
		Register(BAN::RefPtr<AML::Node> value)
			: Node(Node::Type::Register)
			, value(value)
		{}

		BAN::RefPtr<AML::Node> evaluate() override
		{
			if (value)
				return value->evaluate();
			return {};
		}

		bool store(BAN::RefPtr<AML::Node> source) override
		{
			auto evaluated = source->evaluate();
			if (!evaluated)
			{
				AML_ERROR("Failed to evaluate source for store");
				return false;
			}
			value = evaluated;
			return true;
		}

		void debug_print(int indent) const override
		{
			AML_DEBUG_PRINT_INDENT(indent);
			AML_DEBUG_PRINT("Register\n");
			if (value)
				value->debug_print(indent + 1);
			else
			{
				AML_DEBUG_PRINT_INDENT(indent + 1);
				AML_DEBUG_PRINT("No value\n");
			}
		}
	};

}
