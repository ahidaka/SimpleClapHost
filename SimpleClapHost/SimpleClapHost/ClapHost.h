#pragma once

#include <Windows.h>
#include <iostream>

#define BUFFER_SIZE 9600

class ClapHostBuffer {
public:
    ClapHostBuffer();
    ~ClapHostBuffer();

    BYTE** pReorderedBuffer = nullptr;;
    const int buffer_size = BUFFER_SIZE;
};
