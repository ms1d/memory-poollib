#pragma once


#include <cstdint>
#include <cassert>



// Templated bump allocator with compile time capacity
template<typename obj, uint32_t pool_capacity>
class bump_pool {


public:


	bump_pool() {
		data = new obj[pool_capacity];
		head = data;
	}

	~bump_pool() {
        delete[] data;
    }


	obj *alloc(uint32_t count = 1) {
		assert(head + count <= data + pool_capacity);
		obj *p = head;
		head += count;
		return p;
	}

	void free() {
		head = data;
	}
	


private:
	
	obj *data;
	obj *head;


};
