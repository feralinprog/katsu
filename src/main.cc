#include <iostream>
#include <string>

#include "compile.h"
#include "katsu.h"
#include "parser.h"

void usage()
{
    std::cerr << "Usage: ./katsu <source.katsu>\n";
}

int main(int argc, char** argv)
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
    } catch (Katsu::parse_error e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "at "
                  << "<" << *e.span.file.path << "> (" << e.span.start.line + 1 << ":"
                  << e.span.start.column + 1 << " to " << e.span.end.line + 1 << ":"
                  << e.span.end.column + 1 << ")\n";
    } catch (Katsu::compile_error e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "at "
                  << "<" << *e.span.file.path << "> (" << e.span.start.line + 1 << ":"
                  << e.span.start.column + 1 << " to " << e.span.end.line + 1 << ":"
                  << e.span.end.column + 1 << ")\n";
    }
    return EXIT_SUCCESS;
}
