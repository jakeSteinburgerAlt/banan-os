#include "LibGUI/Window.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/banan-os.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace LibGUI
{

	Window::~Window()
	{
		munmap(m_framebuffer, m_width * m_height * 4);
		close(m_server_fd);
	}

	BAN::ErrorOr<BAN::UniqPtr<Window>> Window::create(uint32_t width, uint32_t height, BAN::StringView title)
	{
		if (title.size() >= sizeof(WindowCreatePacket::title))
			return BAN::Error::from_errno(EINVAL);

		int server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (server_fd == -1)
			return BAN::Error::from_errno(errno);

		timespec start_time;
		clock_gettime(CLOCK_MONOTONIC, &start_time);

		for (;;)
		{
			sockaddr_un server_address;
			server_address.sun_family = AF_UNIX;
			strcpy(server_address.sun_path, s_window_server_socket.data());
			if (connect(server_fd, (sockaddr*)&server_address, sizeof(server_address)) == 0)
				break;

			timespec current_time;
			clock_gettime(CLOCK_MONOTONIC, &current_time);
			time_t duration_s = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_nsec >= start_time.tv_nsec);
			if (duration_s > 10)
			{
				close(server_fd);
				return BAN::Error::from_errno(ETIMEDOUT);
			}

			timespec sleep_time;
			sleep_time.tv_sec = 0;
			sleep_time.tv_nsec = 1'000'000;
			nanosleep(&sleep_time, nullptr);
		}

		WindowCreatePacket packet;
		packet.width = width;
		packet.height = height;
		strncpy(packet.title, title.data(), title.size());
		packet.title[title.size()] = '\0';
		if (send(server_fd, &packet, sizeof(packet), 0) != sizeof(packet))
		{
			close(server_fd);
			return BAN::Error::from_errno(errno);
		}

		WindowCreateResponse response;
		if (recv(server_fd, &response, sizeof(response), 0) != sizeof(response))
		{
			close(server_fd);
			return BAN::Error::from_errno(errno);
		}

		void* framebuffer_addr = smo_map(response.framebuffer_smo_key);
		if (framebuffer_addr == nullptr)
		{
			close(server_fd);
			return BAN::Error::from_errno(errno);
		}

		return TRY(BAN::UniqPtr<Window>::create(
			server_fd,
			static_cast<uint32_t*>(framebuffer_addr),
			width,
			height
		));
	}

	bool Window::invalidate()
	{
		WindowInvalidatePacket packet;
		packet.x = 0;
		packet.y = 0;
		packet.width = m_width;
		packet.height = m_height;
		return send(m_server_fd, &packet, sizeof(packet), 0) == sizeof(packet);
	}

	void Window::poll_events()
	{
		for (;;)
		{
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(m_server_fd, &fds);
			timeval timeout { .tv_sec = 0, .tv_usec = 0 };
			select(m_server_fd + 1, &fds, nullptr, nullptr, &timeout);

			if (!FD_ISSET(m_server_fd, &fds))
				break;

			EventPacket packet;
			if (recv(m_server_fd, &packet, sizeof(packet), 0) <= 0)
				break;

			switch (packet.type)
			{
				case EventPacket::Type::DestroyWindow:
					exit(1);
				case EventPacket::Type::CloseWindow:
					if (m_close_window_event_callback)
						m_close_window_event_callback();
					else
						exit(0);
					break;
				case EventPacket::Type::KeyEvent:
					if (m_key_event_callback)
						m_key_event_callback(packet.key_event);
					break;
				case EventPacket::Type::MouseButtonEvent:
					if (m_mouse_button_event_callback)
						m_mouse_button_event_callback(packet.mouse_button_event);
					break;
				case EventPacket::Type::MouseMoveEvent:
					if (m_mouse_move_event_callback)
						m_mouse_move_event_callback(packet.mouse_move_event);
					break;
				case EventPacket::Type::MouseScrollEvent:
					if (m_mouse_scroll_event_callback)
						m_mouse_scroll_event_callback(packet.mouse_scroll_event);
					break;
			}
		}
	}

}