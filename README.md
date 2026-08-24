# memory-poollib

Several lightweight, specialised implementations of memory pools in C++

## Types

- Bump Allocator (`include/bump_pool.hpp`)

- Arena Allocator (`include/arena_pool.hpp` - orchestrates `bump_pool`)

## Any other types?

I'm not trying to rebuild malloc. If I have a need for another type of allocator,
I'll add it here. For the time being, arenas and bumps are fast enough for my purposes.
