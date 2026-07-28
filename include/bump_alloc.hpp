#pragma once


#include <cassert>
#include <cstdint>
#include <cstdlib>



// Templated bump allocator with compile time capacity
template<typename obj, uint32_t pool_capacity>
class bump_pool {


public:


	bump_pool() {
		data = malloc(sizeof(obj) * pool_capacity);
		head = data;
	}

	~bump_pool() {
        free(data);
    }


	obj *alloc() {
		return head++;

	}

	void free() {
		head = data;
	}
	


private:
	
	const obj *data;
	obj *head;


};
