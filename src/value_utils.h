#pragma once

#include "gc.h"
#include "value.h"

namespace Katsu
{
    // Move this all to Value...
    // just declare class GC;

    // Make a Ref with the desired v_ref.
    Ref* make_ref(GC& gc, Root& r_ref);
    Ref* make_ref(GC& gc, Value v_ref);
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
    Vector* make_vector(GC& gc, uint64_t length, Root& r_array);
    Vector* make_vector(GC& gc, uint64_t length, Array* array);
    // Make a Module with the given capacity (and zero length).
    // `base` is optional, with nullptr mapping to a null `v_base`.
    Module* make_module(GC& gc, Root& r_base, uint64_t capacity);
    Module* make_module(GC& gc, Module* base, uint64_t capacity);
    // Make a String with contents copied from a source string.
    String* make_string(GC& gc, const std::string& src);
    // Make a Code with specified fields.
    // `upreg_map` is optional, with nullptr mapping to a null `v_upreg_map`.
    Code* make_code(GC& gc, Root& r_module, uint32_t num_regs, uint32_t num_data, Root& r_upreg_map,
                    Root& r_insts, Root& r_args);
    Code* make_code(GC& gc, Module* module, uint32_t num_regs, uint32_t num_data, Array* upreg_map,
                    Array* insts, Array* args);
    // Make a Closure with specified fields.
    Closure* make_closure(GC& gc, Root& r_code, Root& r_upregs);
    Closure* make_closure(GC& gc, Code* code, Array* upregs);
    // Make a Method with specified fields.
    // `return_type` is optional, with nullptr mapping to a null `v_return_type`.
    // `code` is optional, with nullptr mapping to a null `v_code`.
    // `native_handler` is optional.
    Method* make_method(GC& gc, Root& r_param_matchers, Root& r_return_type, Root& r_code,
                        Root& r_attributes, NativeHandler native_handler);
    Method* make_method(GC& gc, Value v_param_matchers, Type* return_type, Code* code,
                        Vector* attributes, NativeHandler native_handler);
    // Make a MultiMethod with specified fields.
    MultiMethod* make_multimethod(GC& gc, Root& r_name, Root& r_methods, Root& r_attributes);
    MultiMethod* make_multimethod(GC& gc, String* name, Vector* methods, Vector* attributes);
    // Make a Type with specified fields.
    // `slots` is optional, with nullptr mapping to a null `v_slots`.
    Type* make_type(GC& gc, Root& r_name, Root& r_bases, bool sealed, Root& r_linearization,
                    Root& r_subtypes, Type::Kind kind, Root& r_slots);
    Type* make_type(GC& gc, String* name, Vector* bases, bool sealed, Vector* linearization,
                    Vector* subtypes, Type::Kind kind, Vector* slots);
    // Make a DataclassInstance with specified dataclass, with slots uninitialized.
    DataclassInstance* make_instance_nofill(GC& gc, Root& r_type);
    DataclassInstance* make_instance_nofill(GC& gc, Type* type);

    // Append a value to a vector, reallocating if necessary to expand the vector.
    // For convenience, this returns a pointer to the resulting Vector (which may have been moved
    // due to reallocation).
    Vector* append(GC& gc, Root& r_vector, Root& r_value);
    Vector* append(GC& gc, Vector* vector, Value v_value);
    // TODO: handle as part of Module cleanup.
    // // Append a key/value pair to a module, reallocating if necessary to expand the module.
    // void append(GC& gc, Module* module, String* name, Value v_value);

    // Looks up a module entry by name, following the module's v_base until reaching null.
    // Returns a pointer into the relevant Module::Entry value, or nullptr if not found.
    Value* module_lookup(Module* module, String* name);

    // Determine if two Strings are equal, i.e. have the same contents.
    bool string_eq(String* a, String* b);
    // Determine if a String and string are equal, i.e. have the same contents.
    bool string_eq(String* a, const std::string& b);
};