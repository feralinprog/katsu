#include <iostream>
#include <string>

#include "katsu.h"

void usage()
{
    std::cerr << "Usage: ./katsu <source.katsu>\n";
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage();
        return EXIT_FAILURE;
    }
    std::string path(argv[1]);
    try {
        Katsu::execute_file(path);
    } catch (std::ios_base::failure e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "Could not execute file '" << path << "'.\n";
    }
    return EXIT_SUCCESS;
}
