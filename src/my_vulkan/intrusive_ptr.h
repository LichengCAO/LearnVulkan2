#pragma once

#include "ref_counted.h"

#include <cstddef>
#include <type_traits>
#include <utility>

template <typename T>
class IntrusivePtr final
{
private:
	static_assert(std::is_base_of_v<RefCounted, T>, "IntrusivePtr<T> requires T to derive from RefCounted!");

	T* m_pointer = nullptr;

	auto _AddRef(T* inPointer) noexcept->void
	{
		if (inPointer != nullptr)
		{
			inPointer->AddRef();
		}
	}

	auto _Release() noexcept->void
	{
		if (m_pointer != nullptr)
		{
			m_pointer->Release();
			m_pointer = nullptr;
		}
	}

public:
	using ElementType = T;

	constexpr IntrusivePtr() noexcept = default;
	constexpr IntrusivePtr(std::nullptr_t) noexcept {}

	explicit IntrusivePtr(T* inPointer) noexcept
		: m_pointer(inPointer)
	{
		_AddRef(m_pointer);
	}

	IntrusivePtr(const IntrusivePtr& inOther) noexcept
		: m_pointer(inOther.m_pointer)
	{
		_AddRef(m_pointer);
	}

	template <typename U>
	requires std::is_convertible_v<U*, T*>
	IntrusivePtr(const IntrusivePtr<U>& inOther) noexcept
		: m_pointer(inOther.m_pointer)
	{
		_AddRef(m_pointer);
	}

	IntrusivePtr(IntrusivePtr&& inOther) noexcept
		: m_pointer(inOther.m_pointer)
	{
		inOther.m_pointer = nullptr;
	}

	template <typename U>
	requires std::is_convertible_v<U*, T*>
	IntrusivePtr(IntrusivePtr<U>&& inOther) noexcept
		: m_pointer(inOther.m_pointer)
	{
		inOther.m_pointer = nullptr;
	}

	~IntrusivePtr()
	{
		_Release();
	}

	auto operator=(std::nullptr_t) noexcept->IntrusivePtr&
	{
		_Release();
		return *this;
	}

	auto operator=(const IntrusivePtr& inOther) noexcept->IntrusivePtr&
	{
		if (this != &inOther)
		{
			IntrusivePtr copy(inOther);
			swap(copy);
		}
		return *this;
	}

	template <typename U>
	requires std::is_convertible_v<U*, T*>
	auto operator=(const IntrusivePtr<U>& inOther) noexcept->IntrusivePtr&
	{
		IntrusivePtr copy(inOther);
		swap(copy);
		return *this;
	}

	auto operator=(IntrusivePtr&& inOther) noexcept->IntrusivePtr&
	{
		if (this != &inOther)
		{
			_Release();
			m_pointer = inOther.m_pointer;
			inOther.m_pointer = nullptr;
		}
		return *this;
	}

	template <typename U>
	requires std::is_convertible_v<U*, T*>
	auto operator=(IntrusivePtr<U>&& inOther) noexcept->IntrusivePtr&
	{
		_Release();
		m_pointer = inOther.m_pointer;
		inOther.m_pointer = nullptr;
		return *this;
	}

	auto reset(T* inPointer = nullptr) noexcept->void
	{
		IntrusivePtr replacement(inPointer);
		swap(replacement);
	}

	auto swap(IntrusivePtr& inOther) noexcept->void
	{
		std::swap(m_pointer, inOther.m_pointer);
	}

	auto get() const noexcept->T* { return m_pointer; }
	auto operator->() const noexcept->T* { return m_pointer; }
	auto operator*() const noexcept->T& { return *m_pointer; }
	explicit operator bool() const noexcept { return m_pointer != nullptr; }

	template <typename U>
	friend class IntrusivePtr;
};

template <typename T, typename... Arguments>
requires std::is_base_of_v<RefCounted, T>
auto MakeIntrusive(Arguments&&... inArguments)->IntrusivePtr<T>
{
	return IntrusivePtr<T>(new T(std::forward<Arguments>(inArguments)...));
}
