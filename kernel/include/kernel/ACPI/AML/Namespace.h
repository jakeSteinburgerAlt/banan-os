#pragma once

#include <BAN/HashMap.h>
#include <kernel/ACPI/AML/Scope.h>
#include <kernel/ACPI/Headers.h>
#include <kernel/Lock/Mutex.h>

namespace Kernel::ACPI::AML
{

	struct Namespace : public AML::Scope
	{
		static BAN::RefPtr<AML::Namespace> root_namespace();

		template<typename F>
		static void for_each_child(const AML::NameString& scope, const F& callback)
		{
			auto canonical_path = root_namespace()->resolve_path({}, scope);
			ASSERT(canonical_path.has_value());

			for (auto& [path, child] : root_namespace()->m_objects)
			{
				if (path.size() < canonical_path->size() + 1)
					continue;
				if (path[canonical_path->size()] != '.')
					continue;
				if (path.sv().substring(0, canonical_path->size()) != canonical_path->sv())
					continue;
				if (path.sv().substring(canonical_path->size() + 1).contains('.'))
					continue;
				callback(path, child);
			}
		}

		Namespace(NameSeg name) : AML::Scope(Node::Type::Namespace, name) {}

		static BAN::RefPtr<AML::Namespace> create_root_namespace();
		bool parse(const SDTHeader& header);

		void debug_print(int indent) const override;

		BAN::Optional<BAN::String> resolve_path(const AML::NameString& relative_base, const AML::NameString& relative_path, bool allow_nonexistent = false);

		// Find an object in the namespace. Returns nullptr if the object is not found.
		BAN::RefPtr<NamedObject> find_object(const AML::NameString& relative_base, const AML::NameString& relative_path);

		// Add an object to the namespace. Returns false if the parent object could not be added.
		bool add_named_object(ParseContext&, const AML::NameString& object_path, BAN::RefPtr<NamedObject> object);

		// Remove an object from the namespace. Returns false if the object could not be removed.
		bool remove_named_object(const AML::NameString& absolute_path);

	private:
		BAN::HashMap<BAN::String, BAN::RefPtr<NamedObject>> m_objects;
		mutable Mutex m_object_mutex;
	};

}
