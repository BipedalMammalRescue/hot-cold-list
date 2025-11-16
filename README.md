# Hot-Cold List

A thought experiement of an algorithm to keep a best-effort de-fragmented collection of memory buffers used in a situation where:

1. cache performance is important (closer the memory buffers are to each other the better)
1. consolidation can only happen after/during certain timeframs, so the data structure needs to be aware of multiple fragmented regions of memory and perform fragment collection on them from time to time

An example of this weirdly specific use case is for asynchronous resource loading in a game engine: the engine system would need to create memory buffers for incoming files when the gameplay logic tells it to, and before that asynchronous IO event is complete another resource, maybe the same type, is requested.
During this interaction the second load can't consolidate with the first load's memory buffer because data is still written into it.
Therefore, before both operations are finished the two buffers needs to be separate allocations.

This algorithm proposes a solution through abstracting the buffers into hot (actively used) and cold (immutable) buffers.

## Hot vs. Cold

A hot buffer is one that's being actively operated on. 
In the example setting, this is the buffer that the asynchronous IO operation is actively writing to.
The operation is out of the main thread's control and needs to be protected until synchronization or IO completion happens.

On the other hand, a cold buffer is one whose opertions are completely within control of the owner of the list.

## Blocks, Regions, and Allocations

A block is the smallest unit of continuous data region physically allocated in a hot-cold list.
Every allocation request to the hot-cold list either extends an existing block or allocates a new one.

A region is a subset of a block.

An allocation is what the client code sees when they request a new buffer from the list.
Each allocation contains a pointer to a region and an offset inside that region.
Every request to allocate memory can contain at lest one sub-buffers, hence the need in the allocation to have an offset.

## The Algorithm

On allocation, memory is always allocated hot.
The list looks for the first cold block, and extends it to include this new buffer.
If an existing cold buffer is found, the list allocates a new block containing only one buffer region.
The client gets an index into an array of buffer regions instead of the raw regions themselves.

When the client code makes an allocation cold, the list checks for the count of sub-buffers in the block that's hot and decrements it.
If the block becomes cold, i.e. no hot sub-buffers on record, the list tries to consolidate the blocks on the left and right of it that are also cold, then move the new, larger block to the leftmost block that's found to be cold during consolidation.

On deallocation, the list checks for the total sub-buffer count and decrements it.
If the total sub-buffer count drops to zero, the list collects the buffer's memory and moves every block on the right of it one block to the left.

After either consolidation or deallocation, the list needs to scan all allocated regions and update their reference to the new location by either sweeping them into the consolidated block or decrementing their block id.

## Obvious drawbacks

This proposed implementation of the hot-cold list has several drawbacks, most obviously that during consolidation and deallocation the list needs to scan the entire list of allocated regions and reassign their values.
This narrows its use case, namely when the event that triggers such memory-realignment actions are dominated by some other obvious dominating factor, such as disk reads or network synchronization.
Therefore, I'm expecting it to work fine for use cases such as managing disk IO load buffers in a game engine where resources are loaded in asynchronously.

## Limitations due to this project being a demo

I used `malloc`/`realloc` and `std::vector<>` directly in the code.
They aren't supposed to be present in a production setting, as you'd be using custom allocators to manage these buffer creations, and since solutions for an allocator that only does buffers and buffer resizes is very well known (e.g. [this bigsquid blog post](https://bitsquid.blogspot.com/2015/08/allocation-adventures-3-buddy-allocator.html)), I opted for not including an allocator in this repo.