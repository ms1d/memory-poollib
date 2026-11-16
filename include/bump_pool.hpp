#pragma once


#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>



enum mp_type {
	thread_unsafe = 0,
	thread_safe = 1
};


template<typename obj, uint64_t pool_capacity, typename ptr>
requires(
		// allocator does NOT manually destroy objects, so `obj` must be trivially destructible
		std::is_trivially_destructible_v<obj> &&
		(std::is_same_v<ptr, ptr_thread_safe<obj>> || std::is_same_v<ptr, ptr_thread_unsafe<obj>>)
		)
class bump_pool_base {
protected:
	

	obj *const data;
	ptr head;


public:


	bump_pool_base() : data(new obj[pool_capacity]) {
		head.store(data);
	}


	~bump_pool_base() {
		delete[] data;
	}

	bump_pool_base(const bump_pool_base&) = delete;
	bump_pool_base& operator=(const bump_pool_base&) = delete;
	bump_pool_base(bump_pool_base&&) noexcept = delete;
	bump_pool_base& operator=(bump_pool_base&&) noexcept = delete;

	void free() noexcept {
		head.store(data);
	}

	uint64_t remaining_capacity() const {
		return pool_capacity - (head.load() - data);
	}
};


template<typename obj, uint64_t pool_capacity, mp_type pool_type = mp_type::thread_unsafe>
class bump_pool;


// Templated bump allocator with compile time capacity (thread_unsafe)
template<typename obj, uint64_t pool_capacity>
class bump_pool<obj, pool_capacity, mp_type::thread_unsafe> : public bump_pool_base<obj, pool_capacity, ptr_thread_unsafe<obj>> {


public:


	using base = bump_pool_base<obj, pool_capacity, ptr_thread_unsafe<obj>>;
	using base::head;
	using base::data;
	using base::free;
	using base::remaining_capacity;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) requires(!std::is_same<obj, char>::value) {
		obj *p = head.load();
		if (p - data > remaining_capacity()) return nullptr;
		head.store(p + count);
		return p;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) return nullptr;
		
		auto addr = reinterpret_cast<std::uintptr_t>(head);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);

		if (aligned_head - data > remaining_capacity()) return nullptr;
		
		head = aligned_head + count;
		return aligned_head;
	}

	template<typename T, typename... arg_Ts>
	T *alloc_and_cast(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_same<obj, char>::value && std::is_trivially_destructible_v<T>) {
		// Protects against overflow
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		auto ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, std::forward<arg_Ts...>(args)...);
		}
		
		return res;
	}
};



// Templated bump allocator with compile time capacity (thread_safe)
template<typename obj, uint64_t pool_capacity>
class bump_pool<obj, pool_capacity, mp_type::thread_safe> : public bump_pool_base<obj, pool_capacity, ptr_thread_safe<obj>> {


public:


	using base = bump_pool_base<obj, pool_capacity, ptr_thread_safe<obj>>;
	using base::head;
	using base::data;
	using base::free;
	using base::remaining_capacity;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		auto head_local = head.load();
		if (head_local - data > remaining_capacity()) return nullptr;

		while(!head.try_advance(head_local, head_local + count)) {
			if (head_local - data > remaining_capacity()) return nullptr;
		}

		return head_local;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) return nullptr;
		auto head_local = head.load();

		auto addr = reinterpret_cast<std::uintptr_t>(head_local);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);

		if (aligned_head - data > remaining_capacity()) return nullptr;

		while(!head.try_advance(head_local, aligned_head + count)) {
			addr = reinterpret_cast<std::uintptr_t>(head_local);
			aligned_addr = addr + (align - (addr % align)) % align;
			aligned_head = reinterpret_cast<obj*>(aligned_addr);

			if (aligned_head - data > remaining_capacity()) return nullptr;
		}

		return aligned_head;
	}

	template<typename T, typename... arg_Ts>
	T *alloc_and_cast(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_same<obj, char>::value && std::is_trivially_destructible_v<T>) {
		// Protects against overflow
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		auto ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, std::forward<arg_Ts...>(args)...);
		}
		
		return res;
	}

};
