#pragma once


#include <atomic>
#include <cassert>
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
		assert(count <= pool_capacity);
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
		head = data.load();
	}

	~bump_pool() {
        delete[] data;
    }


	// Returns ptr to allocated obj, or nullptr if failed
	obj *alloc(uint32_t count = 1) {
		auto head_local = head.load(std::memory_order_relaxed);
		if ((head_local - data.load(std::memory_order_relaxed)) + count > pool_capacity) return nullptr;

		while(!head.compare_exchange_weak(head_local, head_local + count, std::memory_order_relaxed, std::memory_order_relaxed)) {
			if ((head_local - data) + count > pool_capacity) return nullptr;
		}

		return head_local;
	}

	void free() {
		head.store(data, std::memory_order_relaxed);
	}
	


private:

	// Data should be atomic so that loading it is guaranteed to be atomic
	std::atomic<obj*> data, head;


};

// Specialisations for char (raw byte pools)

template<uint32_t pool_capacity>
class bump_pool<char, pool_capacity, mp_type::thread_unsafe> {


public:


	bump_pool() {
		data = new char[pool_capacity];
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
	char *alloc(uint32_t count = 1, uint32_t align = 1) {
		assert(count <= pool_capacity);
		auto new_head = head + align - (head - data) % align + count;
		if (new_head > data + pool_capacity) return nullptr;
		char *p = head;
		head = new_head;
		return p;
	}

	void free() {
		head = data;
	}

	uint64_t remaining_capacity() {
		return pool_capacity - (head - data);
	}
	


private:
	
	char *data;
	char *head;


};



// Templated bump allocator with compile time capacity (thread_safe)
template<uint32_t pool_capacity>
class bump_pool<char, pool_capacity, mp_type::thread_safe> {


public:

	bump_pool() {
		data = new char[pool_capacity];
		head = data.load();
	}

	~bump_pool() {
        delete[] data;
    }


	// Returns ptr to allocated char, or nullptr if failed
	char *alloc(uint32_t count = 1, uint32_t align = 1) {
		auto head_local = head.load(std::memory_order_relaxed),
			 data_local = data.load(std::memory_order_relaxed);

		auto new_head = head + align - (head - data) % align + count;
		if (new_head + count > data_local + pool_capacity) return nullptr;

		while(!head.compare_exchange_weak(head_local, new_head, std::memory_order_relaxed, std::memory_order_relaxed)) {
			new_head = head + align - (head - data) % align + count;
			if (new_head > data_local + pool_capacity) return nullptr;
		}

		return head_local;
	}

	void free() {
		head.store(data, std::memory_order_relaxed);
	}
	


private:

	// Data should be atomic so that loading it is guaranteed to be atomic
	std::atomic<char*> data, head;


};
