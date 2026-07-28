#pragma once


#include "bump_pool.hpp"
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
		free();
	}


	obj *alloc(uint32_t count = 1) {
		if (count > chunk_capacity) return nullptr;

		auto &c = chunks[current_chunk];

		auto res = c.alloc(count);

		if (res == nullptr) {
			chunks.emplace_back();
			current_chunk++;
			return chunks[current_chunk].alloc(count);
		} else { return res; }
    }

	// Free n chunks (defaults to 1). If n is larger than current number of chunks, free all.
	void free(uint32_t n = 1) {
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


template<typename obj, uint32_t chunk_capacity>
class arena<obj, chunk_capacity, mp_type::thread_safe> {
	

public:



private:



};
