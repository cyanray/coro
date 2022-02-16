#pragma once
#include <coroutine>
#include <atomic>
namespace coro
{
	class io_service
	{
	public:
		class schdule_operation;

		[[nodiscard]]
		schdule_operation schedule() noexcept;

		void stop() noexcept;

		void process_events();

	private:
		bool m_stop = false;
		std::atomic<schdule_operation*> m_schduleList;
	};

	class io_service::schdule_operation
	{
	public:
		schdule_operation(io_service& service) noexcept
			: m_service(service), m_next(nullptr)
		{
		}
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<> handle) noexcept;
		void await_resume() const noexcept {}
	private:
		friend class io_service;
		io_service& m_service;
		std::coroutine_handle<> m_handle;
		schdule_operation* m_next;
	};
}