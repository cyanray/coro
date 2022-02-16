#include "io_service.h"
#include <atomic>
using namespace std;

namespace coro
{
	io_service::schdule_operation io_service::schedule() noexcept
	{
		return schdule_operation{ *this };
	}

	void io_service::stop() noexcept
	{
		m_stop = true;
	}

	void io_service::process_events()
	{
		while (!m_stop)
		{
			m_schduleList.wait(nullptr, std::memory_order::relaxed);
			auto current = m_schduleList.load(std::memory_order::relaxed);
			do
			{
				if (current == nullptr) break;

			} while (!m_schduleList.compare_exchange_weak(current, nullptr,
				std::memory_order::release,
				std::memory_order::relaxed));

			while (current != nullptr)
			{
				auto tmp = current->m_next;	// after resume, current will be destroyed
				current->m_handle.resume();
				current = tmp;
			}
		};
	}

	void io_service::schdule_operation::await_suspend(std::coroutine_handle<> handle) noexcept
	{
		m_handle = handle;
		this->m_next = m_service.m_schduleList.load(std::memory_order::relaxed);
		while (!m_service.m_schduleList.compare_exchange_weak(this->m_next, this,
			std::memory_order::release,
			std::memory_order::relaxed));
		m_service.m_schduleList.notify_one();
	}
}