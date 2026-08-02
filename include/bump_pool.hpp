#pragma once


#include <atomic>
#include <cstdint>



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
	obj *alloc(uint32_t count = 1) {
		if (head + count > data + pool_capacity) return nullptr;
		obj *p = head;
		head += count;
		return p;
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
		head = data;
	}

	~bump_pool() {
        delete[] data;
    }


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint32_t count = 1) {
		auto head_local = head.load(std::memory_order_relaxed);
		if (head_local + count > data + pool_capacity) return nullptr;

		while(!head.compare_exchange_weak(head_local, head_local + count, std::memory_order_relaxed, std::memory_order_relaxed)) {
			if (head_local + count > data + pool_capacity) return nullptr;
		}

		return head_local;
	}

	void free() {
		head.store(data, std::memory_order_relaxed);
	}
	


private:
	
	obj *data;
	std::atomic<obj*> head;


};
