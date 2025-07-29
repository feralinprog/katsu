#pragma once

#include "gc.h"
#include "value.h"

#include <optional>

namespace Katsu
{
    // Move this all to Value...
    // just declare class GC;

    // TODO: make all these just return Roots in the first place... maybe have separate (e.g.)
    // make_ref_ptr for raw pointer results.

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
    // Make an Assoc with the given capacity (and zero length).
    Assoc* make_assoc(GC& gc, uint64_t capacity);
    // Make a String with contents copied from a source string.
    String* make_string(GC& gc, const std::string& src);
    // Make a String of the given length, with contents uninitialized.
    String* make_string_nofill(GC& gc, uint64_t length);
    // Make a Code with specified fields.
    Code* make_code(GC& gc, Root<Assoc>& r_module, uint32_t num_params, uint32_t num_regs,
                    uint32_t num_data, OptionalRoot<Array>& r_upreg_map, Root<Array>& r_insts,
                    Root<Array>& r_args, Root<Tuple>& r_span, Root<Array>& r_inst_spans);
    // Make a Closure with specified fields.
    Closure* make_closure(GC& gc, Root<Code>& r_code, Root<Array>& r_upregs);
    // Make a Method with specified fields.
    // `native_handler` and `intrinsic_handler` are optional.
    Method* make_method(GC& gc, Root<Array>& r_param_matchers, OptionalRoot<Type>& r_return_type,
                        OptionalRoot<Code>& r_code, Root<Vector>& r_attributes,
                        NativeHandler native_handler, IntrinsicHandler intrinsic_handler);
    // Make a MultiMethod with specified fields.
    MultiMethod* make_multimethod(GC& gc, Root<String>& r_name, uint32_t num_params,
                                  Root<Vector>& r_methods, Root<Vector>& r_attributes);
    // Make a Type with specified fields. This doesn't calculate the linearization or ensure that
    // each supertype has the new type as a subtype.
    Type* make_type_raw(GC& gc, Root<String>& r_name, Root<Array>& r_bases, bool sealed,
                        Root<Array>& r_linearization, Root<Vector>& r_subtypes, Type::Kind kind,
                        OptionalRoot<Array>& r_slots, std::optional<uint32_t> num_total_slots);
    // Make a DataclassInstance with specified dataclass, with slots uninitialized.
    DataclassInstance* make_instance_nofill(GC& gc, Root<Type>& r_type);

    struct Frame;
    // Make a CallSegment with the specified lowest frame and total length (in bytes) of all frames
    // in the segment.
    CallSegment* make_call_segment(GC& gc, Frame* segment_bottom, uint64_t total_length);

    // Make a foreign value with the specified field.
    ForeignValue* make_foreign(GC& gc, void* value);

    // Make a ByteArray of the given length, filled with zeros.
    ByteArray* make_byte_array(GC& gc, uint64_t length);
    // Make a ByteArray of the given length, with contents uninitialized.
    ByteArray* make_byte_array_nofill(GC& gc, uint64_t length);

    // Append a value to a vector, reallocating if necessary to expand the vector.
    // For convenience, this returns a pointer to the resulting Vector (which may have been moved
    // due to reallocation).
    Vector* append(GC& gc, Root<Vector>& r_vector, ValueRoot& r_value);
    // Append a key/value pair to an assoc, reallocating if necessary to expand the assoc.
    // For convenience, this returns a pointer to the resulting Assoc (which may have been moved
    // due to reallocation).
    Assoc* append(GC& gc, Root<Assoc>& r_assoc, ValueRoot& r_key, ValueRoot& r_value);

    Array* vector_to_array(GC& gc, Root<Vector>& r_vector);

    // Looks up an assoc entry by name. Returns a pointer into the relevant Assoc::Entry value, or
    // nullptr if not found.
    Value* assoc_lookup(Assoc* assoc, String* name);

    // Determine if two Strings are equal, i.e. have the same contents.
    bool string_eq(String* a, String* b);
    // Determine if a String and string are equal, i.e. have the same contents.
    bool string_eq(String* a, const std::string& b);

    // Copy a String into a newly allocated native string.
    std::string native_str(String* s);
    // Concatenate two Strings.
    String* concat(GC& gc, Root<String>& r_a, Root<String>& r_b);
    // Concatenate a String and native string.
    String* concat(GC& gc, Root<String>& r_a, const std::string& b);
    // Concatenate a native string and String.
    String* concat(GC& gc, const std::string& a, Root<String>& r_b);
    // Concatenate all the given strings.
    String* concat(GC& gc, const std::vector<std::string>& parts);
    // Concatenate all the given strings (each with a given suffix applied -- commonly ":").
    String* concat_with_suffix(GC& gc, const std::vector<std::string>& parts,
                               const std::string& each_suffix);
    // Concatenate all the given strings (each with a given suffix applied -- commonly ":");
    String* concat_with_suffix(GC& gc, Root<Vector>& r_strings, const std::string& each_suffix);

    // Pretty-print a value (to stdout), with an optional initial indent and an initial indentation
    // depth.
    void pprint(Value value, bool initial_indent = true, int depth = 0);

    // Check if an array contains a given value, by value equality (e.g. object identity, not deep
    // equality of any sort).
    bool array_contains(Array* array, Value value);

    // Check if an array contains a given value, by value equality (e.g. object identity, not deep
    // equality of any sort), starting at the given start_index. (This start index may be past the
    // end of the vector.)
    bool array_contains_starting_at(Array* array, Value value, uint64_t start_index);

    // Calculate combined linearization from an array of linearization arrays, appending to the
    // provided r_merged vector. Returns true on success, or else false if C3 linearization is not
    // possible (and in this case, the merge result may only be partial; r_merged is not restored to
    // its initial value).
    // Does not modify any of the provided linearizations, or the vector of linearizations.
    bool c3_merge(GC& gc, Root<Array>& r_linearizations, Root<Vector>& r_merged);

    // Calculate the C3 linearization of the type and its bases. The value in
    // r_type->v_linearization is ignored.
    // See https://www.python.org/download/releases/2.3/mro/ for more on C3 linearization.
    Array* c3_linearization(GC& gc, Root<Type>& r_type);

    // Make a Type with specified fields. This also calculates the type's linearization and ensures
    // that each supertype has the new type as a subtype.
    Type* make_type(GC& gc, Root<String>& r_name, Root<Array>& r_bases, bool sealed,
                    Type::Kind kind, OptionalRoot<Array>& r_slots,
                    std::optional<uint32_t> num_total_slots);

    // Add a method to a multimethod, failing if require_unique and the method conflicts with a
    // previous definition. If !require_unique, the method overwrites any previous definition.
    void add_method(GC& gc, Root<MultiMethod>& r_multimethod, Root<Method>& r_method,
                    bool require_unique);

    // Iterators for arrays / vectors. These are invalidated by any GC collection!
    // ===========================================================================
    Value* begin(Array* array);
    Value* end(Array* array);

    Value* begin(Root<Array>& r_array);
    Value* end(Root<Array>& r_array);

    Value* begin(Vector* vector);
    Value* end(Vector* vector);

    Value* begin(Root<Vector>& r_vector);
    Value* end(Root<Vector>& r_vector);
    // ===========================================================================

    class VM;
    // Doesn't allocate!
    Value type_of(VM& vm, Value value);
    bool is_subtype(Type* a, Type* b);
    // Doesn't allocate!
    bool is_instance(VM& vm, Value value, Type* type);

    void use_default_imports(VM& vm, Root<Vector>& r_imports);
};