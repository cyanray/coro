#pragma once
#include <coroutine>
#include <atomic>

namespace coro
{
	struct async_action
	{
		struct promise_type
		{
			async_action get_return_object() { return { *this }; }
			std::suspend_never initial_suspend()
			{
				return {};
			}
			std::suspend_never final_suspend() noexcept
			{
				m_finish = true;
				m_finish.notify_one();
				return {};
			}
			void return_void() {}
			void unhandled_exception() {}
		private:
			friend struct async_action;
			std::atomic<bool> m_finish = false;
		};

		struct awaitable
		{
			promise_type& m_promise;
			awaitable(promise_type& p) :m_promise(p)
			{
			}

			bool await_ready() const noexcept
			{
				return m_promise.m_finish;
			}

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				m_promise.m_finish.wait(false, std::memory_order::relaxed);
				handle.resume();
			}
			void await_resume() const noexcept {}

		};

		async_action(promise_type& promise) :m_promise(promise)
		{
		}

		async_action(async_action&& a) noexcept :m_promise(a.m_promise)
		{
		}

		/// Disable copy construction/assignment.
		async_action(const async_action&) = delete;
		async_action& operator=(const async_action&) = delete;

		auto operator co_await() noexcept
		{
			return awaitable{ this->m_promise };
		}

		template<typename...T>
		static async_action when_all(T&&...actions)
		{
			((co_await actions), ...);
		}

	private:
		promise_type& m_promise;
	};

}