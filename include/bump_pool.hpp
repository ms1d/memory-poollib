#pragma once


#include "common.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
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
	

	void *const data;
	obj *data_aligned;
	ptr head;


public:


	static_assert(pool_capacity <= (SIZE_MAX - alignof(obj) + 1) / sizeof(obj));
	// Allocate extra bytes to account for alignment
	bump_pool_base() : data((void *const)std::malloc(sizeof(obj) * (pool_capacity) + alignof(obj) - 1)) {
		if (!data) throw std::bad_alloc();

		auto data_addr = reinterpret_cast<std::uintptr_t>(data);
		auto data_addr_aligned = data_addr + (alignof(obj) - (data_addr % alignof(obj))) % alignof(obj);
		data_aligned = reinterpret_cast<obj*>(data_addr_aligned);

		head.store(data_aligned);
	}


	~bump_pool_base() {
		std::free(data);
	}

	bump_pool_base(const bump_pool_base&) = delete;
	bump_pool_base& operator=(const bump_pool_base&) = delete;
	bump_pool_base(bump_pool_base&&) noexcept = delete;
	bump_pool_base& operator=(bump_pool_base&&) noexcept = delete;

	void free() noexcept {
		head.store(data_aligned);
	}

	// Can optionally write the loaded value to a pointer to reduce loads required
	// Or re-use a the existing ptr value 
	uint64_t remaining_capacity(obj **head_local = nullptr, bool reuse = false) const {
		if (head_local && reuse) return pool_capacity - (*head_local - data_aligned);

		auto lcl = head.load();
		if (head_local) *head_local = lcl;
		return pool_capacity - (lcl - data_aligned);
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
	using base::data_aligned;
	using base::free;
	using base::remaining_capacity;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) requires(!std::is_same<obj, char>::value) {
		obj *head_local;
		if (count > remaining_capacity(&head_local)) [[unlikely]] return nullptr;
		head.store(head_local + count);
		return head_local;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) [[unlikely]] return nullptr;

		obj *p;
		auto remaining = remaining_capacity(&p);
		
		auto addr = reinterpret_cast<std::uintptr_t>(p);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);
		auto padding = aligned_head - p;

		if (padding > remaining) [[unlikely]] return nullptr;
		if (count > remaining - padding) [[unlikely]] return nullptr;
		
		head.store(aligned_head + count);
		return aligned_head;
	}

	template<typename T, typename... arg_Ts>
	T *alloc_and_cast(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_same<obj, char>::value && std::is_trivially_destructible_v<T>) {
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		auto ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, std::forward<arg_Ts>(args)...);
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
	using base::data_aligned;
	using base::free;
	using base::remaining_capacity;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		obj *head_local;
		if (count > remaining_capacity(&head_local)) [[unlikely]] return nullptr;

		while(!head.try_advance(*head_local, *head_local + count)) {
			if (count > remaining_capacity(&head_local, true)) [[unlikely]] return nullptr;
		}

		return head_local;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) [[unlikely]] return nullptr;

		obj *head_local;
		auto remaining = remaining_capacity(&head_local);

		auto addr = reinterpret_cast<std::uintptr_t>(*head_local);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);
		auto padding = aligned_head - *head_local;

		if (padding > remaining) [[unlikely]] return nullptr;
		if (count > remaining - padding) [[unlikely]] return nullptr;

		while(!head.try_advance(*head_local, aligned_head + count)) {
			remaining = remaining(&head_local, true);
			addr = reinterpret_cast<std::uintptr_t>(*head_local);
			aligned_addr = addr + (align - (addr % align)) % align;
			aligned_head = reinterpret_cast<obj*>(aligned_addr);
			padding = aligned_head - *head_local;

			if (padding > remaining) [[unlikely]] return nullptr;
			if (count > remaining - padding) [[unlikely]] return nullptr;
		}

		return aligned_head;
	}

	template<typename T, typename... arg_Ts>
	T *alloc_and_cast(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_same<obj, char>::value && std::is_trivially_destructible_v<T>) {
		// Protects against overflow
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		auto ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, std::forward<arg_Ts>(args)...);
		}
		
		return res;
	}

};
