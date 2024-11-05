#pragma once

#include <Windows.h>
#include <iostream>

#define BUFFER_SIZE 9600
//#define BUFFER_SIZE 19200 // just for test

class ClapHostBuffer {
public:
    ClapHostBuffer();
    ~ClapHostBuffer();

    DWORD** pReorderedBuffer = nullptr;;
    const int buffer_size = BUFFER_SIZE;
};
