#include <iostream>
#include <string>

#include "compile.h"
#include "condition.h"
#include "katsu.h"
#include "parser.h"

void usage()
{
    std::cerr << "Usage: ./katsu <module.name> <path/to/source.katsu>\n";
}

std::ostream& operator<<(std::ostream& s, const Katsu::SourceSpan& span)
{
    s << "<" << *span.file.path << ":";
    s << span.start.line + 1 << ":" << span.start.column + 1;
    s << "-";
    // For convenience, follow vscode format (well, one of them...) for links with spans. Yes, the
    // format is inconsistent between start/end formatting :(
    // https://github.com/microsoft/vscode/blob/05edb9ac5410ee1d4118f2be70f019fb68937751/src/vs/workbench/contrib/terminalContrib/links/browser/terminalLinkParsing.ts#L79
    s << span.end.line + 1 << "." << span.end.column + 1;
    s << ">";
    return s;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        usage();
        return EXIT_FAILURE;
    }
    // TODO: determine path from module_name.
    std::string module_name(argv[1]);
    std::string path(argv[2]);
    try {
        Katsu::bootstrap_and_run_file(path, module_name);
        return EXIT_SUCCESS;
    } catch (const std::ios_base::failure& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "Could not execute file '" << path << "'.\n";
    } catch (const Katsu::parse_error& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        std::cerr << "at " << e.span << "\n";
    } catch (const Katsu::compile_error& e) {
        std::cerr << "Compilation error: " << e.what() << "\n";
        std::cerr << "at " << e.span << "\n";
    } catch (const Katsu::terminate_error& e) {
        std::cerr << "terminating program: " << e.what() << "\n";
        std::cerr.flush();
    }
    return EXIT_FAILURE;
}
