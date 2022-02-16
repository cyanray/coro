#pragma once
#include <coroutine>
#include <atomic>
#include <exception>
#include <cassert>

namespace coro
{

	template<typename T> class task;

	class task_promise_base
	{
		friend struct final_awaitable;

		struct final_awaitable
		{
			bool await_ready() const noexcept { return false; }

			template<typename PROMISE>
			std::coroutine_handle<> await_suspend(
				std::coroutine_handle<PROMISE> coro) noexcept
			{
				return coro.promise().m_continuation;
			}

			void await_resume() noexcept {}
		};

	public:

		task_promise_base() noexcept
			: m_state(false)
		{}

		auto initial_suspend() noexcept
		{
			return std::suspend_always{};
		}

		auto final_suspend() noexcept
		{
			return final_awaitable{};
		}

		void set_continuation(std::coroutine_handle<> continuation) noexcept
		{
			m_continuation = continuation;
		}

	private:
		std::coroutine_handle<> m_continuation;
		std::atomic<bool> m_state;

	};

	template<typename T>
	class task_promise final : public task_promise_base
	{
	public:

		task_promise() noexcept {}

		~task_promise()
		{
			// 析构 union，union的析构函数默认为空，需要自己实现析构
			switch (m_resultType)
			{
			case result_type::value:
				m_value.~T();
				break;
			case result_type::exception:
				m_exception.~exception_ptr();
				break;
			default:
				break;
			}
		}

		task<T> get_return_object() noexcept;

		void unhandled_exception() noexcept
		{
			// 在成员 m_exception 处 new 新的 exception_ptr 对象
			::new (static_cast<void*>(std::addressof(m_exception))) std::exception_ptr(
				std::current_exception());
			m_resultType = result_type::exception;
		}

		template<
			typename VALUE,
			typename = std::enable_if_t<std::is_convertible_v<VALUE&&, T>>>
			void return_value(VALUE&& value)
			noexcept(std::is_nothrow_constructible_v<T, VALUE&&>)
		{
			::new (static_cast<void*>(std::addressof(m_value))) T(std::forward<VALUE>(value));
			m_resultType = result_type::value;
		}

		// 左值结果
		T& result()&
		{
			if (m_resultType == result_type::exception)
			{
				std::rethrow_exception(m_exception);
			}

			assert(m_resultType == result_type::value);

			return m_value;
		}

		// HACK: Need to have co_await of task<int> return prvalue rather than
		// rvalue-reference to work around an issue with MSVC where returning
		// rvalue reference of a fundamental type from await_resume() will
		// cause the value to be copied to a temporary. This breaks the
		// sync_wait() implementation.
		// See https://github.com/lewissbaker/cppcoro/issues/40#issuecomment-326864107
		using rvalue_type = std::conditional_t<
			std::is_arithmetic_v<T> || std::is_pointer_v<T>,
			T,
			T&&>;

		// 右值结果
		rvalue_type result()&&
		{
			if (m_resultType == result_type::exception)
			{
				std::rethrow_exception(m_exception);
			}

			assert(m_resultType == result_type::value);

			return std::move(m_value);
		}

	private:

		enum class result_type { empty, value, exception };

		// 返回类型：empty, value, exception
		result_type m_resultType = result_type::empty;

		union
		{
			T m_value;
			std::exception_ptr m_exception;
		};

	};

	template<>
	class task_promise<void> : public task_promise_base
	{
	public:

		task_promise() noexcept = default;

		task<void> get_return_object() noexcept;

		void return_void() noexcept
		{}

		void unhandled_exception() noexcept
		{
			m_exception = std::current_exception();
		}

		void result()
		{
			if (m_exception)
			{
				std::rethrow_exception(m_exception);
			}
		}

	private:

		std::exception_ptr m_exception;

	};

	template<typename T>
	class task_promise<T&> : public task_promise_base
	{
	public:

		task_promise() noexcept = default;

		task<T&> get_return_object() noexcept;

		void unhandled_exception() noexcept
		{
			m_exception = std::current_exception();
		}

		void return_value(T& value) noexcept
		{
			m_value = std::addressof(value);
		}

		T& result()
		{
			if (m_exception)
			{
				std::rethrow_exception(m_exception);
			}

			return *m_value;
		}

	private:

		T* m_value = nullptr;
		std::exception_ptr m_exception;

	};

	template<typename T = void>
	class [[nodiscard]] task
	{
	public:

		using promise_type = task_promise<T>;

		using value_type = T;

	private:

		struct awaitable_base
		{
			std::coroutine_handle<promise_type> m_coroutine;

			awaitable_base(std::coroutine_handle<promise_type> coroutine) noexcept
				: m_coroutine(coroutine)
			{}

			bool await_ready() const noexcept
			{
				return !m_coroutine || m_coroutine.done();
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
			{
				m_coroutine.promise().set_continuation(awaitingCoroutine);
				return m_coroutine;
			}

		};

	public:

		task() noexcept
			: m_coroutine(nullptr)
		{}

		explicit task(std::coroutine_handle<promise_type> coroutine)
			: m_coroutine(coroutine)
		{}

		task(task&& t) noexcept
			: m_coroutine(t.m_coroutine)
		{
			t.m_coroutine = nullptr;
		}

		/// Disable copy construction/assignment.
		task(const task&) = delete;
		task& operator=(const task&) = delete;

		/// Frees resources used by this task.
		~task()
		{
			if (m_coroutine)
			{
				m_coroutine.destroy();
			}
		}

		task& operator=(task&& other) noexcept
		{
			if (std::addressof(other) != this)
			{
				if (m_coroutine)
				{
					m_coroutine.destroy();
				}

				m_coroutine = other.m_coroutine;
				other.m_coroutine = nullptr;
			}

			return *this;
		}

		bool is_ready() const noexcept
		{
			return !m_coroutine || m_coroutine.done();
		}

		auto operator co_await() const& noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				decltype(auto) await_resume()
				{
					if (!this->m_coroutine)
					{
						//throw broken_promise{};
						throw std::runtime_error("broken_promise");
					}

					return this->m_coroutine.promise().result();
				}
			};

			return awaitable{ m_coroutine };
		}

		auto operator co_await() const&& noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				decltype(auto) await_resume()
				{
					if (!this->m_coroutine)
					{
						//throw broken_promise{};
						throw std::runtime_error("broken_promise");
					}

					return std::move(this->m_coroutine.promise()).result();
				}
			};

			return awaitable{ m_coroutine };
		}

		auto when_ready() const noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				void await_resume() const noexcept {}
			};

			return awaitable{ m_coroutine };
		}

	private:
		std::coroutine_handle<promise_type> m_coroutine;

	};

	template<typename T>
	task<T> task_promise<T>::get_return_object() noexcept
	{
		return task<T>{ std::coroutine_handle<task_promise>::from_promise(*this) };
	}

	inline task<void> task_promise<void>::get_return_object() noexcept
	{
		return task<void>{ std::coroutine_handle<task_promise>::from_promise(*this) };
	}

	template<typename T>
	task<T&> task_promise<T&>::get_return_object() noexcept
	{
		return task<T&>{ std::coroutine_handle<task_promise>::from_promise(*this) };
	}
}