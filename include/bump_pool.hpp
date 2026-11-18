#pragma once


#include "common.hpp"
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>



enum mp_type {
	thread_unsafe = 0,
	thread_safe = 1
};


template<typename obj, uint64_t pool_capacity, typename ptr>
requires (std::same_as<ptr, ptr_thread_unsafe<obj>> || std::same_as<ptr, ptr_thread_safe<obj>>)
class bump_pool_base {
protected:
	

	void *const data;
	obj *data_aligned;
	ptr head;
	
	// Use deprecated as a workaround to warn users about non-trivial destructors
	[[deprecated("`obj` is NOT trivially destructible - you are responsible for its destruction")]]
	constexpr void warn() {};
	template<typename T>
	constexpr void warn_trivial_destructibility() { if constexpr (!std::is_trivially_destructible_v<T>) warn(); }


public:


	// Allocate extra bytes to account for alignment
	static_assert(pool_capacity <= (SIZE_MAX - alignof(obj) + 1) / sizeof(obj) && pool_capacity > 0);
	bump_pool_base() : data((void *const)std::malloc(sizeof(obj) * (pool_capacity) + alignof(obj) - 1)) {
		warn_trivial_destructibility<obj>();
		if (!data) throw std::bad_alloc();

		auto data_addr = reinterpret_cast<std::uintptr_t>(data);
		auto data_addr_aligned = data_addr + (alignof(obj) - (data_addr % alignof(obj))) % alignof(obj);
		data_aligned = reinterpret_cast<obj*>(data_addr_aligned);

		data_aligned = std::start_lifetime_as_array<obj>(data_aligned, pool_capacity);

		head.store(data_aligned);
	}


	~bump_pool_base() {
		std::free(data);
	}

	bump_pool_base(const bump_pool_base&) = delete;
	bump_pool_base& operator=(const bump_pool_base&) = delete;
	bump_pool_base(bump_pool_base&&) noexcept = delete;
	bump_pool_base& operator=(bump_pool_base&&) noexcept = delete;

	// NOT designed to be run concurrently in the thread safe variant
	void free() noexcept {
		head.store(data_aligned);
	}


	static_assert(pool_capacity <= PTRDIFF_MAX); // Ensure pool_capacity - ptrdiff is safe
	uint64_t remaining_capacity() const noexcept {
		return pool_capacity - (head.load() - data_aligned);
	}

	// Can optionally write the loaded value to a pointer to reduce loads required
	// Or re-use a the existing ptr value 
	static_assert(pool_capacity <= PTRDIFF_MAX); // Ensure pool_capacity - ptrdiff is safe
	uint64_t remaining_capacity(obj *&head_local, bool reuse = false) const noexcept {
		if (reuse) return pool_capacity - (head_local - data_aligned);

		auto lcl = head.load();
		head_local = lcl;
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
	using base::warn_trivial_destructibility;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		obj *head_local;
		if (count > remaining_capacity(head_local)) [[unlikely]] return nullptr;
		head.store(head_local + count);
		return head_local;
	}

	// char specialisation allows for additional align argument
	// Does NOT construct objects - only provides storage
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) [[unlikely]] return nullptr;

		obj *head_local;
		auto remaining = remaining_capacity(head_local);

		auto addr = reinterpret_cast<std::uintptr_t>(head_local);
		auto padding = (align - (addr % align)) % align;

		if (padding > remaining) [[unlikely]] return nullptr;
		if (count > remaining - padding) [[unlikely]] return nullptr;
		if (padding > UINTPTR_MAX - addr) [[unlikely]] return nullptr;

		auto aligned_head = reinterpret_cast<obj*>(addr + padding);
	
		head.store(aligned_head + count);
		return aligned_head;
	}
	

	// Orchestrates `alloc()` to also construct objects (does NOT support perfect forwarding, will copy args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename... arg_Ts>
	obj *alloc_and_construct(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_nothrow_constructible_v<obj, arg_Ts&...>) {
		auto ptr = alloc(count);
		if (ptr == nullptr) [[unlikely]] return nullptr;

		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(ptr + i, args...);
		}
		
		return ptr;
	}

	// Orchestrates `alloc()` to also construct 1 object (supports perfect forwarding of rvalue args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename... arg_Ts>
	obj *alloc_and_construct_one(arg_Ts&&... args) noexcept
	requires(std::is_nothrow_constructible_v<obj, arg_Ts...>) {
		auto ptr = alloc(1);
		if (ptr == nullptr) [[unlikely]] return nullptr;

		std::construct_at(ptr, std::forward<arg_Ts>(args)...);
		
		return ptr;
	}


	// Orchestrates `alloc()` to also construct objects (does NOT support perfect forwarding, will copy args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename T, typename... arg_Ts>
	T *alloc_and_construct(uint64_t count = 1, arg_Ts&&... args) noexcept 
	requires (std::is_same_v<obj, char> && std::is_nothrow_constructible_v<T, arg_Ts&...>) {
		this->template warn_trivial_destructibility<T>();
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		obj *ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = std::start_lifetime_as_array<T>(ptr, count);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, args...);
		}
		
		return res;
	}

	// Orchestrates `alloc()` to also construct 1 object (supports perfect forwarding of rvalue args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename T, typename... arg_Ts>
	T *alloc_and_construct_one(arg_Ts&&... args) noexcept
	requires(std::is_same_v<obj, char> && std::is_nothrow_constructible_v<T, arg_Ts...>) {
		this->template warn_trivial_destructibility<T>();

		auto ptr = alloc(1 * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		std::construct_at(res, std::forward<arg_Ts>(args)...);
		
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
	using base::warn_trivial_destructibility;


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		obj *head_local;
		if (count > remaining_capacity(head_local)) [[unlikely]] return nullptr;

		while(!head.try_advance(head_local, head_local + count)) {
			if (count > remaining_capacity(head_local, true)) [[unlikely]] return nullptr;
		}

		return head_local;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		if (align == 0) [[unlikely]] return nullptr;

		while (1) {
			obj *head_local;
			auto remaining = remaining_capacity(head_local);

			auto addr = reinterpret_cast<std::uintptr_t>(head_local);
			auto padding = (align - (addr % align)) % align;

			if (padding > remaining) [[unlikely]] return nullptr;
			if (count > remaining - padding) [[unlikely]] return nullptr;
			if (padding > UINTPTR_MAX - addr) [[unlikely]] return nullptr;

			auto aligned_head = reinterpret_cast<obj*>(addr + padding);
			
			if (head.try_advance(head_local, aligned_head + count)) return aligned_head;
		}
	}


	// Orchestrates `alloc()` to also construct objects (does NOT support perfect forwarding, will copy args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename... arg_Ts>
	obj *alloc_and_construct(uint64_t count = 1, arg_Ts&&... args) noexcept
	requires(std::is_nothrow_constructible_v<obj, arg_Ts&...>) {
		auto ptr = alloc(count);
		if (ptr == nullptr) [[unlikely]] return nullptr;

		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(ptr + i, args...);
		}
		
		return ptr;
	}

	// Orchestrates `alloc()` to also construct 1 object (supports perfect forwarding of rvalue args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename... arg_Ts>
	obj *alloc_and_construct_one(arg_Ts&&... args) noexcept
	requires(std::is_nothrow_constructible_v<obj, arg_Ts...>) {
		auto ptr = alloc(1);
		if (ptr == nullptr) [[unlikely]] return nullptr;

		std::construct_at(ptr, std::forward<arg_Ts>(args)...);
		
		return ptr;
	}


	// Orchestrates `alloc()` to also construct objects (does NOT support perfect forwarding, will copy args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename T, typename... arg_Ts>
	T *alloc_and_construct(uint64_t count = 1, arg_Ts&&... args) noexcept 
	requires (std::is_same_v<obj, char> && std::is_nothrow_constructible_v<T, arg_Ts&...>) {
		this->template warn_trivial_destructibility<T>();
		if (count > SIZE_MAX / sizeof(T)) [[unlikely]] return nullptr;

		obj *ptr = alloc(count * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = std::start_lifetime_as_array<T>(ptr, count);
		
		for (uint64_t i = 0; i < count; i++) {
			std::construct_at(res + i, args...);
		}
		
		return res;
	}

	// Orchestrates `alloc()` to also construct 1 object (supports perfect forwarding of rvalue args)
	// Users are responsible for destroying said objects if they are not trivially destructible
	template<typename T, typename... arg_Ts>
	T *alloc_and_construct_one(arg_Ts&&... args) noexcept
	requires(std::is_same_v<obj, char> && std::is_nothrow_constructible_v<T, arg_Ts...>) {
		this->template warn_trivial_destructibility<T>();

		auto ptr = alloc(1 * sizeof(T), alignof(T));
		if (ptr == nullptr) [[unlikely]] return nullptr;

		auto res = reinterpret_cast<T*>(ptr);
		std::construct_at(res, std::forward<arg_Ts>(args)...);
		
		return res;
	}

};
