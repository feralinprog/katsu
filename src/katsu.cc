#include "katsu.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace Katsu
{
    void execute_file(const std::string& filepath)
    {
        std::ifstream file_stream;
        // Raise exceptions on logical error or read/write error.
        file_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        file_stream.open(filepath.c_str());

        std::stringstream str_stream;
        str_stream << file_stream.rdbuf();
        std::string file_contents = str_stream.str();

        std::cout << file_contents;
    }
};
