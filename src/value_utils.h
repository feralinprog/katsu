#pragma once

#include "gc.h"
#include "value.h"

namespace Katsu
{
    // Move this all to Value...
    // just declare class GC;

    // Make a Ref with the desired v_ref.
    Ref* make_ref(GC& gc, Value v_ref);
    // Make a Tuple of the given length, filled with nulls.
    Tuple* make_tuple(GC& gc, uint64_t length);
    // Make a Tuple with the specified length and components.
    Tuple* make_tuple(GC& gc, uint64_t length, Value* components);
    // Make an Array of the given length, filled with nulls.
    Array* make_array(GC& gc, uint64_t length);
    // Make an Array with the specified length and components.
    Array* make_array(GC& gc, uint64_t length, Value* components);
    // Make an Array with the specified length and components (filling with nulls after
    // num_components).
    Array* make_array(GC& gc, uint64_t length, uint64_t num_components, Value* components);
    // Make a Vector with the given capacity and zero length (filling the backing array with nulls).
    Vector* make_vector(GC& gc, uint64_t capacity);
    // Make a Vector with the given capacity and length, filled with the specified components
    // (followed by a tail of nulls).
    Vector* make_vector(GC& gc, uint64_t capacity, uint64_t length, Value* components);
    // Make a Module with the given capacity (and zero length).
    Module* make_module(GC& gc, Value v_base, uint64_t capacity);
    // Make a String with contents copied from a source string.
    String* make_string(GC& gc, const std::string& src);
    // Make a Code with specified fields.
    Code* make_code(GC& gc, Value v_module, uint32_t num_regs, uint32_t num_data, Value v_upreg_map,
                    Value v_insts, Value v_args);
    // Make a Closure with specified fields.
    Closure* make_closure(GC& gc, Value v_code, Value v_upregs);
    // Make a Method with specified fields.
    Method* make_method(GC& gc, Value v_param_matchers, Value v_return_type, Value v_code,
                        Value v_attributes, NativeHandler native_handler);
    // Make a MultiMethod with specified fields.
    MultiMethod* make_multimethod(GC& gc, Value v_name, Value v_methods, Value v_attributes);
    // Make a Type with specified fields.
    Type* make_type(GC& gc, Value v_name, Value v_bases, bool sealed, Value v_linearization,
                    Value v_subtypes, Type::Kind kind, Value v_slots);
    // Make a DataclassInstance with specified fields.
    DataclassInstance* make_instance(GC& gc, Value v_type, Value* slots);

    // Append a value to a vector, reallocating if necessary to expand the vector.
    void append(GC& gc, Vector* vector, Value v_value);
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