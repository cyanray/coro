#pragma once
#include <atomic>
#include <coroutine>
#include <optional>

namespace coro
{
	template<typename T>
	class async_queue
	{
	public:
		template<typename TValue>
		class dequeue_operation
		{
		public:
			using value_type = std::remove_reference<TValue>::type;

			dequeue_operation(async_queue<TValue>& queue) noexcept
				: m_next(nullptr), m_queue(queue)
			{
			}

			bool await_ready() const noexcept { return false; }

			void await_suspend(std::coroutine_handle<> handle) noexcept
			{
				m_handle = handle;
				this->m_next = m_queue.m_list.load(std::memory_order::relaxed);
				while (!m_queue.m_list.compare_exchange_weak(this->m_next, this,
					std::memory_order::release,
					std::memory_order::relaxed));
			}

			value_type await_resume() const noexcept
			{
				return m_result.value();
			}
		private:
			friend class async_queue<TValue>;
			async_queue<TValue>& m_queue;
			std::optional<value_type> m_result;
			std::coroutine_handle<> m_handle;
			dequeue_operation<TValue>* m_next;
		};

		[[nodiscard]]
		dequeue_operation<T> dequeue()
		{
			return { *this };
		}

		template<typename TEnqueue>
		void enqueue(TEnqueue&& value)
		{
			auto current = m_list.load(std::memory_order::relaxed);
			do
			{
				if (current == nullptr) break;

			} while (!m_list.compare_exchange_weak(current, current->m_next,
				std::memory_order::release,
				std::memory_order::relaxed));
			if (current != nullptr)
			{
				current->m_result = std::forward<TEnqueue>(value);
				current->m_handle.resume();
			}
		}
	private:
		std::atomic<dequeue_operation<T>*> m_list;
	};

}