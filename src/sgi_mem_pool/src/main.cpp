#include "SGIAllocator.h"

#include <iostream>
#include <vector>

int main(int argc, char *argv[])
{
    std::cout << "hello world" << std::endl;

    std::vector<int, SGIAllocator<int>> list;

    for (int i = 0; i < 100; ++i)
    {
        int data = rand() % 1000;
        list.emplace_back(data);
    }

    for (auto val : list)
    {
        std::cout << val << " ";
    }

    return 0;
}
