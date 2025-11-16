#pragma once

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

template <typename TElement>
class HotColdList
{
private:
    struct BlockRegion
    {
        bool Valid;
        int BlockId;
        int Offset;
        int Length;
    };

    struct Block 
    {
        TElement* Buffer;
        int Size;

        int HotBuffers;
        int TotalBuffers;
    };

    std::vector<Block> m_Blocks;
    std::vector<BlockRegion> m_BufferRegions;

public:
    struct AllocationId
    {
        int RegionId;
        int RegionOffset;
    };


    TElement* GetBuffer(AllocationId id)
    {
        BlockRegion& allocation = m_BufferRegions[id.RegionId];
        Block& block = m_Blocks[allocation.BlockId];

        TElement* atlasBuffer = block.Buffer;
        TElement* allocationBegin = atlasBuffer + allocation.Offset;
        TElement* buffer = allocationBegin + id.RegionOffset;

        return buffer;
    }

    // create a new hot block or make an existing cold block hot (container doesn't track the buffers within the same block)
    // NOTE: this method returns the allocation ID of the first sub-buffer
    AllocationId CreateHotBuffers(int newSize, int bufferCount)
    {
        // find the first free allocation slot
        int allocationId;
        for (allocationId = 0; allocationId < m_BufferRegions.size(); allocationId ++)
        {
            if (!m_BufferRegions[allocationId].Valid)
                break;
        }
        if (allocationId >= m_BufferRegions.size())
        {
            m_BufferRegions.push_back({ false, 0, 0, 0 });
        }
        BlockRegion& outAllocation = m_BufferRegions[allocationId];
        outAllocation.Valid = true;

        // try to find a cold buffer
        int blockId;
        for (blockId = 0; blockId < m_Blocks.size(); blockId++)
        {
            if (m_Blocks[blockId].HotBuffers <= 0)
                break;
        }
        outAllocation.BlockId = blockId;
        outAllocation.Length = newSize;

        // no cold buffer, add a new one
        if (blockId == m_Blocks.size())
        {
            TElement* newHotBuffer = static_cast<TElement*>(malloc(newSize * sizeof(TElement)));
            m_Blocks.push_back({
                newHotBuffer,
                newSize,
                bufferCount,
                bufferCount
            });

            outAllocation.Offset = 0;
            return { allocationId, 0 };
        }
        else 
        {
            Block& targetBlock = m_Blocks[blockId];
            
            // allocate/reallocate
            int totalSize = targetBlock.Size + newSize;
            if (targetBlock.Buffer != nullptr)
            {
                targetBlock.Buffer = static_cast<TElement*>(realloc(targetBlock.Buffer, totalSize * sizeof(TElement)));
            }
            else
            {
                targetBlock.Buffer = static_cast<TElement*>(malloc(totalSize));
            }

            // update
            outAllocation.Offset = targetBlock.Size;

            targetBlock.Size = totalSize;
            targetBlock.HotBuffers += bufferCount;
            targetBlock.TotalBuffers += bufferCount;

            return { allocationId, 0 };
        }
    }

    void MakeBufferCold(AllocationId bufferId)
    {
        BlockRegion& allocation = m_BufferRegions[bufferId.RegionId];
        Block& block = m_Blocks[allocation.BlockId];

        assert(block.HotBuffers > 0);
        block.HotBuffers --;

        // try to consolidate this buffer
        if (block.HotBuffers > 0)
            return;

        int consolidationSize = block.Size;
        int consolidationBufferCount = block.TotalBuffers;

        // first pass: rightward consolidation
        int end = allocation.BlockId + 1;
        for (int i = allocation.BlockId + 1; i < m_Blocks.size(); i++)
        {
            if (m_Blocks[i].HotBuffers > 0)
                break;

            end = i + 1;

            // move block#i to block
            consolidationSize += m_Blocks[i].Size;
            consolidationBufferCount += m_Blocks[i].TotalBuffers;
        }

        // second pass: leftward consolidation
        int begin = allocation.BlockId;
        for (int i = allocation.BlockId - 1; i >= 0; i--)
        {
            if (m_Blocks[i].HotBuffers > 0)
                break;

            begin = i;

            // move block#i to block
            consolidationSize += m_Blocks[i].Size;
            consolidationBufferCount += m_Blocks[i].TotalBuffers;
        }

        if (begin == allocation.BlockId && end == allocation.BlockId + 1)
            return;

        // reallocate a larger buffer and add all the children counts in there
        TElement* newBuffer = static_cast<TElement*>(realloc(m_Blocks[begin].Buffer, consolidationSize * sizeof(TElement)));
        m_Blocks[begin].TotalBuffers = consolidationBufferCount;
        m_Blocks[begin].Buffer = newBuffer;
        
        // copy all blocks into the new buffer (and before we wipe the consolidated blocks their spaces can be reused to make something funny happen)
        // NOTE: copyOffset is the offset in the new consolidated buffer
        int copyOffset = m_Blocks[begin].Size;
        for (int i = begin + 1; i < end; i++)
        {
            memcpy((void*)(newBuffer + copyOffset), (void*)m_Blocks[i].Buffer, m_Blocks[i].Size * sizeof(TElement));
            m_Blocks[i].Size = copyOffset;
            copyOffset += m_Blocks[i].Size;
            free(m_Blocks[i].Buffer);
        }

        // update buffer regions
        for (BlockRegion& bufferRegion : m_BufferRegions)
        {
            if (!bufferRegion.Valid || bufferRegion.BlockId <= begin || bufferRegion.BlockId >= end)
                continue;

            bufferRegion.Offset = m_Blocks[bufferRegion.BlockId].Size + bufferRegion.Offset;
            bufferRegion.BlockId = begin;
        }

        // wipe the consolidated blocks
        for (int i = begin + 1; i < end; i++)
        {
            m_Blocks[i] = { nullptr, 0, 0, 0 };
        }
    }
    
    void FreeBuffer(AllocationId bufferId)
    {
        BlockRegion& allocation = m_BufferRegions[bufferId.RegionId];
        Block& block = m_Blocks[allocation.BlockId];

        assert(block.TotalBuffers > 0);
        block.TotalBuffers --;

        // deallocate this block
        if (block.TotalBuffers > 0)
            return;

        free(block.Buffer);
        
        // unlink that block
        for (int i = allocation.BlockId + 1; i < m_Blocks.size(); i++)
        {
            m_Blocks[i - 1] = m_Blocks[i];
        }
        m_Blocks.resize(m_Blocks.size() - 1);

        // update allocated regions
        for (BlockRegion& bufferRegion : m_BufferRegions)
        {
            if (bufferRegion.BlockId > allocation.BlockId)
            {
                bufferRegion.BlockId --;
            }
        }
    }
};