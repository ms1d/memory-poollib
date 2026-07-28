#pragma once


#include <cstddef>
#include <cassert>

// Templated memory pool with compile time size
template<typename obj, size_t pool_size>
class memory_pool {


public:

	memory_pool() {
		data = new obj[pool_size];
	}

	~memory_pool() {
		delete[] data;
	}


	obj *alloc(const size_t size) {
		assert(pool_size >= size);
		static_assert(false, "Not implemented");
	}

	void free(const obj *ptr) {
		static_assert(false, "Not implemented");
	}
	


private:
	
	obj *data;


};
