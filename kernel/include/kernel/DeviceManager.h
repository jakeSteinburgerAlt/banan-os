#pragma once

#include <BAN/StringView.h>
#include <kernel/FS/FileSystem.h>
#include <kernel/Device.h>
#include <kernel/SpinLock.h>

namespace Kernel
{

	class DeviceManager final : public FileSystem, public Inode
	{
		BAN_NON_COPYABLE(DeviceManager);
		BAN_NON_MOVABLE(DeviceManager);

	public:
		static void initialize();
		static DeviceManager& get();

		void update();
		void add_device(Device*);

		virtual BAN::RefPtr<Inode> root_inode() override { return this; }

		virtual InodeType inode_type() const override { return InodeType::Device; }

		virtual BAN::StringView name() const override { return "device-manager"; }

		virtual BAN::ErrorOr<BAN::RefPtr<Inode>> read_directory_inode(BAN::StringView) override;
		virtual BAN::ErrorOr<BAN::Vector<BAN::String>> read_directory_entries(size_t) override;

	private:
		DeviceManager() = default;

	private:
		SpinLock m_lock;
		BAN::Vector<Device*> m_devices;

		friend class BAN::RefPtr<DeviceManager>;

	public:
		virtual ino_t ino() const override { return 0; }
		virtual Mode mode() const override { return { Mode::IFDIR | Mode::IRUSR | Mode::IWUSR | Mode::IXUSR | Mode::IRGRP | Mode::IXGRP | Mode::IROTH | Mode::IXOTH }; }
		virtual nlink_t nlink() const override { return 1; }
		virtual uid_t uid() const override { return 0; }
		virtual gid_t gid() const override { return 0; }
		virtual off_t size() const override { return 0; }
		virtual timespec atime() const override { return { 0, 0 }; }
		virtual timespec mtime() const override { return { 0, 0 }; }
		virtual timespec ctime() const override { return { 0, 0 }; }
		virtual blksize_t blksize() const override { return 0; }
		virtual blkcnt_t blocks() const override { return 0; }
		virtual dev_t dev() const override { return 0x4900; }
		virtual dev_t rdev() const override { return 0x7854; }

		virtual BAN::ErrorOr<size_t> read(size_t, void*, size_t)        { ASSERT_NOT_REACHED(); }
		virtual BAN::ErrorOr<void> create_file(BAN::StringView, mode_t) { ASSERT_NOT_REACHED(); }
	};

}