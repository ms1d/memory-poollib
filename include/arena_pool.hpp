#pragma once


#include "bump_pool.hpp"
#include <atomic>
#include <memory>
#include <vector>



template<typename obj, uint64_t chunk_capacity = 1024, mp_type pool_type = mp_type::thread_unsafe>
class arena;


template<typename obj, uint64_t chunk_capacity>
requires(std::is_trivially_destructible_v<obj>)
class arena<obj, chunk_capacity, mp_type::thread_unsafe> {
	

public:

	arena() {
		chunks.emplace_back(new bump_pool<obj, chunk_capacity, mp_type::thread_unsafe>);
	}

	~arena() {
		free(-1);
	}


	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		// Prevents infinite resizes + division by 0
		if (count > chunk_capacity) return nullptr;

		auto &c = chunks[current_chunk];

		auto res = c->alloc(count);

		if (res == nullptr) {
			// Re-use chunks where possible
			if (current_chunk >= chunks.size() - 1) chunks.emplace_back(new bump_pool<obj, chunk_capacity, mp_type::thread_unsafe>);
			current_chunk++;
			res = chunks[current_chunk]->alloc(count);
		}

		return res;
    }
	
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		// Prevents infinite resizes + division by 0
		if (count > chunk_capacity || align == 0) return nullptr;

		auto &c = chunks[current_chunk];

		auto res = c->alloc(count, align);

		if (res == nullptr) {
			// Re-use chunks where possible
			if (current_chunk >= chunks.size() - 1) chunks.emplace_back(new bump_pool<obj, chunk_capacity, mp_type::thread_unsafe>);
			current_chunk++;
			res = chunks[current_chunk]->alloc(count, align);
		}

		return res;
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


	// Free `n` chunks (defaults to 1). If `n` is larger than current number of chunks, free all.
	// NOT atomic. Intended to be called at synchronisation points ONLY
	// If `del` is true, it will delete chunks. If false, it will re-use them later
	void free(uint64_t n = 1, bool del = false) noexcept {
		while (n > 0) {
			if (current_chunk == 0) {
				chunks[0]->free();
				return;
			}

			if (del) delete chunks[current_chunk];
			else chunks[current_chunk]->free();

			chunks.pop_back();

			n--;
			current_chunk--;
		}
	}

	// Templated version of the above (1 less runtime branch)
	template<bool del = false>
	void free(uint64_t n = 1) noexcept {
		while (n > 0) {
			if (current_chunk == 0) {
				chunks[0]->free();
				return;
			}

			if constexpr (del) delete chunks[current_chunk];
			else chunks[current_chunk]->free();

			chunks.pop_back();

			n--;
			current_chunk--;
		}
	}


private:

	std::vector<bump_pool<obj, chunk_capacity, mp_type::thread_unsafe>*> chunks;
	uint64_t current_chunk = 0;


};


// Will need a linked list rather than a vector
template<typename obj, uint64_t chunk_capacity>
requires(std::is_trivially_destructible_v<obj>)
class arena<obj, chunk_capacity, mp_type::thread_safe> {


struct chunk {
	bump_pool<obj, chunk_capacity, mp_type::thread_safe> pool;
	std::atomic<chunk*> next = nullptr;
};


public:
	arena() {
		chunks = new chunk;
		chunks_end = chunks;
	}

	// Obviously no one should be using the pool once this starts to run
	~arena() {
		chunk *curr = chunks;
		while (curr != nullptr) {
			auto next = curr->next.load(std::memory_order_relaxed);
			delete curr;
			curr = next;
		}
	}


	// Do NOT assume that the linked list is grown atomically.
	// There WILL exist a point in the program where chunks_end
	// is NOT the `next` of any node in the linked list.
	obj *alloc(uint64_t count = 1) noexcept
	requires(!std::is_same<obj, char>::value) {
		assert(count <= chunk_capacity);
		auto chunks_end_local = chunks_end.load(std::memory_order_acquire);
		auto res = chunks_end_local->pool.alloc(count);
		if (res != nullptr) return res;

		auto add_chunk_success = false;
		// Try to cycle chunks_end forward if its next is not nullptr
		while (chunks_end_local->next.load(std::memory_order_acquire) != nullptr && res == nullptr && !add_chunk_success) {
			add_chunk_success = chunks_end.compare_exchange_weak(
				chunks_end_local,
				chunks_end_local->next.load(std::memory_order_acquire),
				std::memory_order_acq_rel,
				std::memory_order_acquire
			);
			if (add_chunk_success) break;
			res = chunks_end_local->pool.alloc(count);
		}

		if (res != nullptr) return res;
		if (add_chunk_success) return alloc(count);

		// Need to add a new chunk now
		auto new_chunk = new chunk;

		while (res == nullptr && !chunks_end.compare_exchange_weak(
					chunks_end_local,
					new_chunk,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
			res = chunks_end_local->pool.alloc(count);
		}

		if (res != nullptr) {
			delete new_chunk; return res; // Could perhaps re-use new_chunk?
		}

		// CAS succeeded, need to link nodes
		chunks_end_local->next.store(new_chunk, std::memory_order_release);
		return alloc(count);
	}
	
	obj *alloc(uint64_t count = 1, uint64_t align = 1) noexcept
	requires(std::is_same<obj, char>::value) {
		assert(count <= chunk_capacity);
		if (align == 0) return nullptr;
		auto chunks_end_local = chunks_end.load(std::memory_order_acquire);
		auto res = chunks_end_local->pool.alloc(count, align);
		if (res != nullptr) return res;

		auto add_chunk_success = false;
		// Try to cycle chunks_end forward if its next is not nullptr
		while (chunks_end_local->next.load(std::memory_order_acquire) != nullptr && res == nullptr && !add_chunk_success) {
			add_chunk_success = chunks_end.compare_exchange_weak(
				chunks_end_local,
				chunks_end_local->next.load(std::memory_order_acquire),
				std::memory_order_acq_rel,
				std::memory_order_acquire
			);
			if (add_chunk_success) break;
			res = chunks_end_local->pool.alloc(count, align);
		}

		if (res != nullptr) return res;
		if (add_chunk_success) return alloc(count, align);

		// Need to add a new chunk now
		auto new_chunk = new chunk;

		while (res == nullptr && !chunks_end.compare_exchange_weak(
					chunks_end_local,
					new_chunk,
					std::memory_order_acq_rel,
					std::memory_order_acquire)) {
			res = chunks_end_local->pool.alloc(count, align);
		}

		if (res != nullptr) {
			delete new_chunk; return res; // Could perhaps re-use new_chunk?
		}

		// CAS succeeded, need to link nodes
		chunks_end_local->next.store(new_chunk, std::memory_order_release);
		return alloc(count, align);
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

	// Free `n` chunks (defaults to 1). If `n` is larger than current number of chunks, free all.
	// NOT atomic. Intended to be called at synchronisation points ONLY
	// If `del` is true, it will delete chunks. If false, it will re-use them later
	void free(uint64_t n = 1, bool del = false) noexcept {
		std::vector<chunk*> stack;
		int current_chunk = -1;

		auto start = chunks;

		while (start != nullptr) {
			stack.push_back(start);
			start = start->next;
			current_chunk++;
		}

		while (n > 0) {
			if (current_chunk == 0) {
				chunks[0].pool.free();
				return;
			}

			if (del) {
				delete stack.back();
				(stack.back()-1)->next = nullptr;
			}
			else stack.back()->pool.free();

			stack.pop_back();

			n--;
			current_chunk--;
		}
	}

	// Templated version of the above
	template<bool del = false>
	void free(uint64_t n = 1) noexcept {
		std::vector<chunk*> stack;
		int current_chunk = -1;

		auto start = chunks;

		while (start != nullptr) {
			stack.push_back(start);
			start = start->next;
			current_chunk++;
		}

		while (n > 0) {
			if (current_chunk == 0) {
				chunks[0].pool.free();
				return;
			}

			if constexpr (del) {
				delete stack.back();
				(stack.back()-1)->next = nullptr;
			}
			else stack.back()->pool.free();

			stack.pop_back();

			n--;
			current_chunk--;
		}
	}



private:
	chunk *chunks = nullptr;
	std::atomic<chunk*> chunks_end = nullptr;



};
