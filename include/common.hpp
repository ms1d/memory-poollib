#pragma once



#include <atomic>



template<typename T>
struct ptr_thread_unsafe {
	T* ptr;

	T* load() const noexcept {
		return ptr;
	}

	void store(T* new_ptr) noexcept {
		ptr = new_ptr;
	}

	bool try_advance(T*& expected, T* desired) noexcept {
		ptr = desired;
		return true;
	}
};

template<typename T>
struct ptr_thread_safe {
	std::atomic<T*> ptr;

	T* load() const noexcept {
		return ptr.load(std::memory_order_relaxed);
	}

	void store(T* new_ptr) noexcept {
		ptr.store(new_ptr, std::memory_order_relaxed);
	}

	bool try_advance(T*& expected, T* desired) noexcept {
		return ptr.compare_exchange_weak(expected, desired, std::memory_order_relaxed, std::memory_order_relaxed);
	}
};
