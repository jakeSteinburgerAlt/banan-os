#pragma once

#include <BAN/HashMap.h>
#include <BAN/String.h>
#include <BAN/StringView.h>
#include <kernel/FS/FileSystem.h>
#include <kernel/Storage/StorageController.h>

namespace Kernel
{

	class VirtualFileSystem : public FileSystem
	{
	public:
		static BAN::ErrorOr<void> initialize();
		static VirtualFileSystem& get();
		virtual ~VirtualFileSystem() {};

		virtual BAN::RefPtr<Inode> root_inode() override  { return m_root_inode; }

		BAN::ErrorOr<void> mount_test();

		struct File
		{
			BAN::RefPtr<Inode> inode;
			BAN::String canonical_path;
		};
		BAN::ErrorOr<File> file_from_absolute_path(BAN::StringView);

		struct MountPoint
		{
			BAN::RefPtr<Inode> inode;
			FileSystem* target;
		};
		const BAN::Vector<MountPoint>& mount_points() const { return m_mount_points; }

	private:
		VirtualFileSystem() = default;
		BAN::ErrorOr<void> initialize_impl();

	private:
		BAN::RefPtr<Inode>				m_root_inode;
		BAN::Vector<MountPoint>			m_mount_points;
		BAN::Vector<StorageController*>	m_storage_controllers;
	};

}