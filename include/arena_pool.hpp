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
	chunk *next = nullptr;
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
			auto next = curr->next;
			delete curr;
			curr = next;
		}
	}


	// Do NOT assume that if the linked list is grown, the act of growing
	// is atomic. There WILL exist a point in the program where chunks_end
	// is NOT the `next` of any node in the linked list.
	obj *alloc(uint32_t count = 1) {
		auto chunks_end_local = chunks_end.load(std::memory_order_relaxed);
		auto res = chunks_end_local->pool.alloc(count);
		if (res != nullptr) return res;

		// Need to add a new chunk
		auto new_chunk = new chunk, chunks_end_copy = chunks_end_local;

		while (res == nullptr && !chunks_end.compare_exchange_weak(
					chunks_end_local,
					new_chunk,
					std::memory_order_relaxed,
					std::memory_order_relaxed)) {
			res = chunks_end_local->pool.alloc(count);
		}

		if (res != nullptr) {
			delete new_chunk; return res; // Could perhaps re-use new_chunk?
		}

		// CAS succeeded, need to link nodes
		chunks_end_copy->next = new_chunk;
		return alloc(count);
	}

	void free() {

	}



private:
	chunk *chunks = nullptr;
	std::atomic<chunk*> chunks_end = nullptr;



};
