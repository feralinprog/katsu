#pragma once

#include "gc.h"
#include "value.h"

namespace Katsu
{
    // Move this all to Value...
    // just declare class GC;

    // Make a Ref with the desired v_ref.
    Ref* make_ref(GC& gc, ValueRoot& r_ref);
    // Make a Tuple of the given length, filled with nulls.
    Tuple* make_tuple(GC& gc, uint64_t length);
    // Make a Tuple of the given length, with components uninitialized.
    Tuple* make_tuple_nofill(GC& gc, uint64_t length);
    // Make an Array of the given length, filled with nulls.
    Array* make_array(GC& gc, uint64_t length);
    // Make an Array of the given length, with components uninitialized.
    Array* make_array_nofill(GC& gc, uint64_t length);
    // Make a Vector of the given capacity and zero length (filling the backing array with nulls).
    Vector* make_vector(GC& gc, uint64_t capacity);
    // Make a Vector of the given length, with a given backing array.
    Vector* make_vector(GC& gc, uint64_t length, Root<Array>& r_array);
    Vector* make_vector(GC& gc, uint64_t length, Array* array);
    // Make a Module with the given capacity (and zero length).
    Module* make_module(GC& gc, OptionalRoot<Module>& r_base, uint64_t capacity);
    // Make a String with contents copied from a source string.
    String* make_string(GC& gc, const std::string& src);
    // Make a Code with specified fields.
    Code* make_code(GC& gc, Root<Module>& r_module, uint32_t num_regs, uint32_t num_data,
                    OptionalRoot<Array>& r_upreg_map, Root<Array>& r_insts, Root<Array>& r_args);
    // Make a Closure with specified fields.
    Closure* make_closure(GC& gc, Root<Code>& r_code, Root<Array>& r_upregs);
    // Make a Method with specified fields.
    // `native_handler` is optional.
    Method* make_method(GC& gc, ValueRoot& r_param_matchers, OptionalRoot<Type>& r_return_type,
                        OptionalRoot<Code>& r_code, Root<Vector>& r_attributes,
                        NativeHandler native_handler);
    // Make a MultiMethod with specified fields.
    MultiMethod* make_multimethod(GC& gc, Root<String>& r_name, Root<Vector>& r_methods,
                                  Root<Vector>& r_attributes);
    // Make a Type with specified fields.
    Type* make_type(GC& gc, Root<String>& r_name, Root<Vector>& r_bases, bool sealed,
                    Root<Vector>& r_linearization, Root<Vector>& r_subtypes, Type::Kind kind,
                    OptionalRoot<Vector>& r_slots);
    // Make a DataclassInstance with specified dataclass, with slots uninitialized.
    DataclassInstance* make_instance_nofill(GC& gc, Root<Type>& r_type);

    // Append a value to a vector, reallocating if necessary to expand the vector.
    // For convenience, this returns a pointer to the resulting Vector (which may have been moved
    // due to reallocation).
    Vector* append(GC& gc, Root<Vector>& r_vector, ValueRoot& r_value);
    // Append a key/value pair to a module, reallocating if necessary to expand the module.
    void append(GC& gc, Root<Module>& r_module, Root<String>& r_name, ValueRoot& r_value);

    // Looks up a module entry by name, following the module's v_base until reaching null.
    // Returns a pointer into the relevant Module::Entry value, or nullptr if not found.
    Value* module_lookup(Module* module, String* name);

    // Determine if two Strings are equal, i.e. have the same contents.
    bool string_eq(String* a, String* b);
    // Determine if a String and string are equal, i.e. have the same contents.
    bool string_eq(String* a, const std::string& b);

    // Concatenate all the given strings.
    String* concat(GC& gc, const std::vector<std::string>& parts);
    // Concate all the given strings (each with a given suffix applied -- commonly ":").
    String* concat_with_suffix(GC& gc, const std::vector<std::string>& parts,
                               const std::string& each_suffix);
};