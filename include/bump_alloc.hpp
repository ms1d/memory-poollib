#pragma once


#include <cstdint>



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


	obj *alloc() {
		return head++;

	}

	void free() {
		head = data;
	}
	


private:
	
	obj *data;
	obj *head;


};
