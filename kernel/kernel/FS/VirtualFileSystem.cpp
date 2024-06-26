#include <BAN/ScopeGuard.h>
#include <BAN/StringView.h>
#include <kernel/FS/DevFS/FileSystem.h>
#include <kernel/FS/ProcFS/FileSystem.h>
#include <kernel/FS/TmpFS/FileSystem.h>
#include <kernel/FS/VirtualFileSystem.h>
#include <kernel/Lock/LockGuard.h>
#include <fcntl.h>

namespace Kernel
{

	static BAN::RefPtr<VirtualFileSystem> s_instance;

	void VirtualFileSystem::initialize(BAN::StringView root_path)
	{
		ASSERT(!s_instance);
		s_instance = MUST(BAN::RefPtr<VirtualFileSystem>::create());

		ASSERT(root_path.size() >= 5 && root_path.substring(0, 5) == "/dev/"_sv);;
		root_path = root_path.substring(5);

		auto root_inode = MUST(DevFileSystem::get().root_inode()->find_inode(root_path));
		if (!root_inode->mode().ifblk())
			Kernel::panic("Specified root '/dev/{}' does not name a block device", root_path);
		s_instance->m_root_fs = MUST(FileSystem::from_block_device(static_cast<BlockDevice*>(root_inode.ptr())));

		Credentials root_creds { 0, 0, 0, 0 };
		MUST(s_instance->mount(root_creds, &DevFileSystem::get(), "/dev"_sv));

		MUST(s_instance->mount(root_creds, &ProcFileSystem::get(), "/proc"_sv));

		auto tmpfs = MUST(TmpFileSystem::create(1024, 0777, 0, 0));
		MUST(s_instance->mount(root_creds, tmpfs, "/tmp"_sv));
	}

	VirtualFileSystem& VirtualFileSystem::get()
	{
		ASSERT(s_instance);
		return *s_instance;
	}

	BAN::ErrorOr<void> VirtualFileSystem::mount(const Credentials& credentials, BAN::StringView block_device_path, BAN::StringView target)
	{
		auto block_device_file = TRY(file_from_absolute_path(credentials, block_device_path, true));
		if (!block_device_file.inode->is_device())
			return BAN::Error::from_errno(ENOTBLK);

		auto* device = static_cast<Device*>(block_device_file.inode.ptr());
		if (!device->mode().ifblk())
			return BAN::Error::from_errno(ENOTBLK);

		auto file_system = TRY(FileSystem::from_block_device(static_cast<BlockDevice*>(device)));
		return mount(credentials, file_system, target);
	}

	BAN::ErrorOr<void> VirtualFileSystem::mount(const Credentials& credentials, BAN::RefPtr<FileSystem> file_system, BAN::StringView path)
	{
		auto file = TRY(file_from_absolute_path(credentials, path, true));
		if (!file.inode->mode().ifdir())
			return BAN::Error::from_errno(ENOTDIR);

		LockGuard _(m_mutex);
		TRY(m_mount_points.push_back({ file_system, file  }));
		return {};
	}

	VirtualFileSystem::MountPoint* VirtualFileSystem::mount_from_host_inode(BAN::RefPtr<Inode> inode)
	{
		LockGuard _(m_mutex);
		for (MountPoint& mount : m_mount_points)
			if (*mount.host.inode == *inode)
				return &mount;
		return nullptr;
	}

	VirtualFileSystem::MountPoint* VirtualFileSystem::mount_from_root_inode(BAN::RefPtr<Inode> inode)
	{
		LockGuard _(m_mutex);
		for (MountPoint& mount : m_mount_points)
			if (*mount.target->root_inode() == *inode)
				return &mount;
		return nullptr;
	}

	BAN::ErrorOr<VirtualFileSystem::File> VirtualFileSystem::file_from_absolute_path(const Credentials& credentials, BAN::StringView path, int flags)
	{
		LockGuard _(m_mutex);

		ASSERT(path.front() == '/');

		auto inode = root_inode();
		ASSERT(inode);

		BAN::String canonical_path;

		BAN::Vector<BAN::String> path_parts;

		{
			auto temp = TRY(path.split('/'));
			for (size_t i = 0; i < temp.size(); i++)
				TRY(path_parts.emplace_back(temp[temp.size() - i - 1]));
		}

		size_t link_depth = 0;

		while (!path_parts.empty())
		{
			const auto& path_part = path_parts.back();
			auto orig = inode;

			if (path_part.empty() || path_part == "."_sv)
			{

			}
			else if (path_part == ".."_sv)
			{
				if (auto* mount_point = mount_from_root_inode(inode))
					inode = TRY(mount_point->host.inode->find_inode(".."_sv));
				else
					inode = TRY(inode->find_inode(".."_sv));

				if (!canonical_path.empty())
				{
					ASSERT(canonical_path.front() == '/');
					while (canonical_path.back() != '/')
						canonical_path.pop_back();
					canonical_path.pop_back();
				}
			}
			else
			{
				if (!inode->can_access(credentials, O_SEARCH))
					return BAN::Error::from_errno(EACCES);

				inode = TRY(inode->find_inode(path_part));

				if (auto* mount_point = mount_from_host_inode(inode))
					inode = mount_point->target->root_inode();

				TRY(canonical_path.push_back('/'));
				TRY(canonical_path.append(path_part));
			}

			path_parts.pop_back();

			if (inode->mode().iflnk() && (!(flags & O_NOFOLLOW) || !path_parts.empty()))
			{
				auto target = TRY(inode->link_target());
				if (target.empty())
					return BAN::Error::from_errno(ENOENT);

				if (target.front() == '/')
				{
					inode = root_inode();
					canonical_path.clear();

					auto temp = TRY(target.sv().split('/'));
					for (size_t i = 0; i < temp.size(); i++)
						TRY(path_parts.emplace_back(temp[temp.size() - i - 1]));
				}
				else
				{
					inode = orig;

					while (canonical_path.back() != '/')
						canonical_path.pop_back();
					canonical_path.pop_back();

					auto new_parts = TRY(target.sv().split('/'));
					for (size_t i = 0; i < new_parts.size(); i++)
						TRY(path_parts.emplace_back(new_parts[new_parts.size() - i - 1]));
				}

				link_depth++;
				if (link_depth > 100)
					return BAN::Error::from_errno(ELOOP);
			}
		}

		if (!inode->can_access(credentials, flags))
			return BAN::Error::from_errno(EACCES);

		if (canonical_path.empty())
			TRY(canonical_path.push_back('/'));

		File file;
		file.inode = inode;
		file.canonical_path = BAN::move(canonical_path);

		if (file.canonical_path.empty())
			TRY(file.canonical_path.push_back('/'));

		return file;
	}

}
