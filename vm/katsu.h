#pragma once

#include "gc.h"
#include "span.h"
#include "value.h"

#include <string>

namespace Katsu
{
    void bootstrap_and_run_file(const std::string& filepath, const std::string& module_name);
    Value bootstrap_and_run_source(const SourceFile source, const std::string& module_name, GC& gc,
                                   uint64_t call_stack_size);
};
