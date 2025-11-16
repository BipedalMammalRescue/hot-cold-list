#include "hot_cold_list.h"
#include <iostream>

int main()
{
    std::cout << "hello world" << std::endl;
    HotColdList<int> myList;


    // create 16 data points in two sub-buffers
    HotColdList<int>::AllocationId buffer1 = myList.CreateHotBuffers(16, 1);
    HotColdList<int>::AllocationId buffer2 = myList.CreateHotBuffers(16, 1);

    {
        int* buffer = myList.GetBuffer(buffer2);
        for (int i = 0; i < 16; i++)
        {
            buffer[i] = i;
        }

        // will not consolidate
        myList.MakeBufferCold(buffer2);
    }

    {
        int* buffer = myList.GetBuffer(buffer1);
        for (int i = 0; i < 16; i++)
        {
            buffer[i] = i;
        }

        // will trigger a consolidation
        myList.MakeBufferCold(buffer1);
    }

    // read the data
    {
        int* buffer = myList.GetBuffer(buffer1);
        for (int i = 0; i < 32; i++)
        {
            std::cout << buffer[i] << std::endl;
        }
    }
}