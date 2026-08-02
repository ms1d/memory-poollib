#pragma once


#include "bump_pool.hpp"
#include <atomic>
#include <vector>



template<typename obj, uint32_t chunk_capacity = 1024, mp_type pool_type = mp_type::thread_unsafe>
class arena;


template<typename obj, uint32_t chunk_capacity>
class arena<obj, chunk_capacity, mp_type::thread_unsafe> {
	

public:

	arena() {
		chunks.emplace_back();
	}

	~arena() {
		free(-1);
	}


	obj *alloc(uint32_t count = 1) {
		if (count > chunk_capacity) return nullptr;

		auto &c = chunks[current_chunk];

		auto res = c.alloc(count);

		if (res == nullptr) {
			chunks.emplace_back();
			current_chunk++;
			return chunks[current_chunk].alloc(count);
		} else return res;
    }

	// Free n chunks (defaults to 1). If n is larger than current number of chunks, free all.
	// NOT atomic. Intended to be called at synchronisation points ONLY
	void free(uint32_t n = 1) {
		if (current_chunk == 0) {
			chunks[0].free();
			return;
		}
		while (n > 0) {
			chunks[current_chunk].free();
			chunks.pop_back();
			n--;

			if (current_chunk == 0) break;
			current_chunk--;
		}
	}


private:

	std::vector<bump_pool<obj, chunk_capacity, mp_type::thread_unsafe>> chunks;
	uint32_t current_chunk = 0;


};


// Will need a linked list rather than a vector
template<typename obj, uint32_t chunk_capacity>
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
	obj *alloc(uint32_t count = 1) {
		assert(count <= chunk_capacity);
		auto chunks_end_local = chunks_end.load(std::memory_order_seq_cst);
		auto res = chunks_end_local->pool.alloc(count);
		if (res != nullptr) return res;

		auto add_chunk_success = false;
		// Try to cycle chunks_end forward if its next is not nullptr
		while (chunks_end_local->next.load(std::memory_order_seq_cst) != nullptr && res == nullptr && !add_chunk_success) {
			add_chunk_success = chunks_end.compare_exchange_weak(
				chunks_end_local,
				chunks_end_local->next.load(std::memory_order_seq_cst),
				std::memory_order_seq_cst,
				std::memory_order_seq_cst
			);
			if (add_chunk_success) break;
			res = chunks_end_local->pool.alloc(count);
		}

		if (res != nullptr) return res;
		if (add_chunk_success) return alloc(count);

		// Need to add a new chunk now
		auto new_chunk = new chunk, chunks_end_copy = chunks_end_local;

		while (res == nullptr && !chunks_end.compare_exchange_weak(
					chunks_end_local,
					new_chunk,
					std::memory_order_seq_cst,
					std::memory_order_seq_cst)) {
			res = chunks_end_local->pool.alloc(count);
		}

		if (res != nullptr) {
			delete new_chunk; return res; // Could perhaps re-use new_chunk?
		}

		// CAS succeeded, need to link nodes
		chunks_end_copy->next.store(new_chunk, std::memory_order_seq_cst);
		return alloc(count);
	}

	// Free n chunks (defaults to 1). If n is larger than current number of chunks, free all.
	// NOT atomic. Intended to be called at synchronisation points ONLY
	void free(uint32_t n = 1) {
		auto tmp = chunks;
		while (tmp != nullptr) {
			tmp->pool.free();
			tmp = tmp->next;
		}
		chunks_end = chunks;
	}



private:
	chunk *chunks = nullptr;
	std::atomic<chunk*> chunks_end = nullptr;



};
