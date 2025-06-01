#include <iostream>
#include <string>

#include "compile.h"
#include "katsu.h"
#include "parser.h"

void usage()
{
    std::cerr << "Usage: ./katsu <source.katsu>\n";
}

std::ostream& operator<<(std::ostream& s, const Katsu::SourceSpan& span)
{
    s << "<" << *span.file.path << "> (" << span.start.line + 1 << ":" << span.start.column + 1
      << " to " << span.end.line + 1 << ":" << span.end.column + 1 << ")\n";
    return s;
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
    } catch (const std::ios_base::failure& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "Could not execute file '" << path << "'.\n";
    } catch (const Katsu::parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "at " << e.span << "\n";
    } catch (const Katsu::compile_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "at " << e.span << "\n";
    }
    return EXIT_SUCCESS;
}
