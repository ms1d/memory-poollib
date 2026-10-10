#pragma once


#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>



enum mp_type {
	thread_unsafe = 0,
	thread_safe = 1
};


template<typename obj, uint32_t pool_capacity, mp_type pool_type = mp_type::thread_unsafe>
class bump_pool;


// Templated bump allocator with compile time capacity (thread_unsafe)
template<typename obj, uint32_t pool_capacity>
class bump_pool<obj, pool_capacity, mp_type::thread_unsafe> {


public:


	bump_pool() {
		data = new obj[pool_capacity];
		head = data;
	}

	bump_pool(const bump_pool& other) = delete;

	bump_pool operator=(const bump_pool& other) = delete;

	bump_pool(bump_pool&& other) noexcept {
		data = other.data;
		head = other.head;
		other.data = nullptr;
		other.head = nullptr;
	}

	bump_pool& operator=(bump_pool&& other) noexcept {
		if (this != &other) {
			delete[] data;

			data = other.data;
            head = other.head;

			other.data = nullptr;
			other.head = nullptr;
		}
		return *this;
	}

	~bump_pool() {
        delete[] data;
    }


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint32_t count = 1) requires(!std::is_same<obj, char>::value) {
		assert(count <= pool_capacity);
		if (head > data + pool_capacity - count) return nullptr;
		obj *p = head;
		head += count;
		return p;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint32_t count = 1, uint32_t align = 1) requires(std::is_same<obj, char>::value) {
		assert(count <= pool_capacity);
		if (align == 0) return nullptr;
		
		auto addr = reinterpret_cast<std::uintptr_t>(head);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);

		if (aligned_head > data + pool_capacity - count) return nullptr;
		
		head = aligned_head + count;
		return aligned_head;
	}

	void free() {
		head = data;
	}

	uint64_t remaining_capacity() {
		return pool_capacity - (head - data);
	}
	


private:
	
	obj *data;
	obj *head;


};



// Templated bump allocator with compile time capacity (thread_safe)
template<typename obj, uint32_t pool_capacity>
class bump_pool<obj, pool_capacity, mp_type::thread_safe> {


public:

	bump_pool() {
		data = new obj[pool_capacity];
		head = data.load();
	}

	~bump_pool() {
        delete[] data;
    }


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint32_t count = 1) requires(!std::is_same<obj, char>::value) {
		auto head_local = head.load(std::memory_order_relaxed);
		if ((head_local - data.load(std::memory_order_relaxed)) > pool_capacity - count) return nullptr;

		while(!head.compare_exchange_weak(head_local, head_local + count, std::memory_order_relaxed, std::memory_order_relaxed)) {
			if ((head_local - data) > pool_capacity - count) return nullptr;
		}

		return head_local;
	}

	// char specialisation allows for additional align argument
	obj *alloc(uint32_t count = 1, uint32_t align = 1) requires(std::is_same<obj, char>::value) {
		if (align == 0) return nullptr;
		auto head_local = head.load(std::memory_order_relaxed);

		auto addr = reinterpret_cast<std::uintptr_t>(head_local);
		auto aligned_addr = addr + (align - (addr % align)) % align;
		auto aligned_head = reinterpret_cast<obj*>(aligned_addr);

		if (aligned_head > data_local + pool_capacity - count) return nullptr;

		while(!head.compare_exchange_weak(head_local, aligned_head + count, std::memory_order_relaxed, std::memory_order_relaxed)) {
			addr = reinterpret_cast<std::uintptr_t>(head_local);
			aligned_addr = addr + (align - (addr % align)) % align;
			aligned_head = reinterpret_cast<obj*>(aligned_addr);

			if (aligned_head > data_local + pool_capacity - count) return nullptr;
		}

		return aligned_head;
	}

	void free() {
		head.store(data, std::memory_order_relaxed);
	}
	


private:

	// Data should be atomic so that loading it is guaranteed to be atomic
	std::atomic<obj*> data, head;


};
