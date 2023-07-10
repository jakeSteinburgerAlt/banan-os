#include <kernel/FS/Pipe.h>
#include <kernel/FS/VirtualFileSystem.h>
#include <kernel/OpenFileDescriptorSet.h>

#include <fcntl.h>

namespace Kernel
{


	OpenFileDescriptorSet::OpenFileDescriptorSet(const Credentials& credentials)
		: m_credentials(credentials)
	{

	}

	OpenFileDescriptorSet::~OpenFileDescriptorSet()
	{
		close_all();
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::clone_from(const OpenFileDescriptorSet& other)
	{
		close_all();

		for (int fd = 0; fd < (int)other.m_open_files.size(); fd++)
		{
			if (other.validate_fd(fd).is_error())
				continue;
			
			auto& open_file = other.m_open_files[fd];

			auto result = BAN::RefPtr<OpenFileDescription>::create(open_file->inode, open_file->path, open_file->offset, open_file->flags);

			if (result.is_error())
			{
				close_all();
				return result.error();
			}

			m_open_files[fd] = result.release_value();

			if (m_open_files[fd]->flags & O_WRONLY && m_open_files[fd]->inode->is_pipe())
				((Pipe*)m_open_files[fd]->inode.ptr())->clone_writing();
		}

		return {};
	}

	BAN::ErrorOr<int> OpenFileDescriptorSet::open(BAN::StringView absolute_path, int flags)
	{
		if (flags & ~(O_RDONLY | O_WRONLY | O_NOFOLLOW | O_SEARCH | O_CLOEXEC))
			return BAN::Error::from_errno(ENOTSUP);

		auto file = TRY(VirtualFileSystem::get().file_from_absolute_path(m_credentials, absolute_path, flags));

		int fd = TRY(get_free_fd());
		m_open_files[fd] = TRY(BAN::RefPtr<OpenFileDescription>::create(file.inode, BAN::move(file.canonical_path), 0, flags));

		return fd;
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::pipe(int fds[2])
	{
		TRY(get_free_fd_pair(fds));

		auto pipe = TRY(Pipe::create(m_credentials));
		m_open_files[fds[0]] = TRY(BAN::RefPtr<OpenFileDescription>::create(pipe, ""sv, 0, O_RDONLY));
		m_open_files[fds[1]] = TRY(BAN::RefPtr<OpenFileDescription>::create(pipe, ""sv, 0, O_WRONLY));

		return {};
	}

	BAN::ErrorOr<int> OpenFileDescriptorSet::dup2(int fildes, int fildes2)
	{
		if (fildes2 < 0 || fildes2 >= (int)m_open_files.size())
			return BAN::Error::from_errno(EBADF);

		TRY(validate_fd(fildes));
		if (fildes == fildes2)
			return fildes;

		(void)close(fildes2);
		
		m_open_files[fildes2] = m_open_files[fildes];
		m_open_files[fildes2]->flags &= ~O_CLOEXEC;

		if (m_open_files[fildes]->flags & O_WRONLY && m_open_files[fildes]->inode->is_pipe())
			((Pipe*)m_open_files[fildes]->inode.ptr())->clone_writing();

		return fildes;
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::seek(int fd, off_t offset, int whence)
	{
		TRY(validate_fd(fd));

		off_t new_offset = 0;

		switch (whence)
		{
			case SEEK_CUR:
				new_offset = m_open_files[fd]->offset + offset;
				break;
			case SEEK_END:
				new_offset = m_open_files[fd]->inode->size() - offset;
				break;
			case SEEK_SET:
				new_offset = offset;
				break;
			default:
				return BAN::Error::from_errno(EINVAL);
		}

		if (new_offset < 0)
			return BAN::Error::from_errno(EINVAL);

		m_open_files[fd]->offset = new_offset;

		return {};
	}

	BAN::ErrorOr<off_t> OpenFileDescriptorSet::tell(int fd) const
	{
		TRY(validate_fd(fd));
		return m_open_files[fd]->offset;
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::fstat(int fd, struct stat* out) const
	{
		TRY(validate_fd(fd));

		auto inode = m_open_files[fd]->inode;
		out->st_dev		= inode->dev();
		out->st_ino		= inode->ino();
		out->st_mode	= inode->mode().mode;
		out->st_nlink	= inode->nlink();
		out->st_uid		= inode->uid();
		out->st_gid		= inode->gid();
		out->st_rdev	= inode->rdev();
		out->st_size	= inode->size();
		out->st_atim	= inode->atime();
		out->st_mtim	= inode->mtime();
		out->st_ctim	= inode->ctime();
		out->st_blksize	= inode->blksize();
		out->st_blocks	= inode->blocks();

		return {};
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::close(int fd)
	{
		TRY(validate_fd(fd));

		if (m_open_files[fd]->flags & O_WRONLY && m_open_files[fd]->inode->is_pipe())
			((Pipe*)m_open_files[fd]->inode.ptr())->close_writing();
		
		m_open_files[fd].clear();

		return {};
	}

	void OpenFileDescriptorSet::close_all()
	{
		for (int fd = 0; fd < (int)m_open_files.size(); fd++)
			(void)close(fd);
	}

	void OpenFileDescriptorSet::close_cloexec()
	{
		for (int fd = 0; fd < (int)m_open_files.size(); fd++)
		{
			if (validate_fd(fd).is_error())
				continue;
			if (m_open_files[fd]->flags & O_CLOEXEC)
				(void)close(fd);
		}
	}

	BAN::ErrorOr<size_t> OpenFileDescriptorSet::read(int fd, void* buffer, size_t count)
	{
		TRY(validate_fd(fd));
		auto& open_file = m_open_files[fd];
		size_t nread = TRY(open_file->inode->read(open_file->offset, buffer, count));
		open_file->offset += nread;
		return nread;
	}

	BAN::ErrorOr<size_t> OpenFileDescriptorSet::write(int fd, const void* buffer, size_t count)
	{
		TRY(validate_fd(fd));
		auto& open_file = m_open_files[fd];
		size_t nwrite = TRY(open_file->inode->write(open_file->offset, buffer, count));
		open_file->offset += nwrite;
		return nwrite;
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::read_dir_entries(int fd, DirectoryEntryList* list, size_t list_size)
	{
		TRY(validate_fd(fd));
		auto& open_file = m_open_files[fd];
		TRY(open_file->inode->directory_read_next_entries(open_file->offset, list, list_size));
		open_file->offset++;
		return {};
	}


	BAN::ErrorOr<BAN::StringView> OpenFileDescriptorSet::path_of(int fd) const
	{
		TRY(validate_fd(fd));
		return m_open_files[fd]->path.sv();
	}


	BAN::ErrorOr<void> OpenFileDescriptorSet::validate_fd(int fd) const
	{
		if (fd < 0 || fd >= (int)m_open_files.size())
			return BAN::Error::from_errno(EBADF);
		if (!m_open_files[fd])
			return BAN::Error::from_errno(EBADF);
		return {};
	}

	BAN::ErrorOr<int> OpenFileDescriptorSet::get_free_fd() const
	{
		for (int fd = 0; fd < (int)m_open_files.size(); fd++)
			if (!m_open_files[fd])
				return fd;
		return BAN::Error::from_errno(EMFILE);
	}

	BAN::ErrorOr<void> OpenFileDescriptorSet::get_free_fd_pair(int fds[2]) const
	{
		size_t found = 0;
		for (int fd = 0; fd < (int)m_open_files.size(); fd++)
		{
			if (!m_open_files[fd])
				fds[found++] = fd;
			if (found == 2)
				return {};
		}
		return BAN::Error::from_errno(EMFILE);
	}

}