#include "value_utils.h"

#include <cstring>

namespace Katsu
{
    Ref* make_ref(GC& gc, ValueRoot& r_ref)
    {
        Ref* ref = gc.alloc<Ref>();
        ref->v_ref = *r_ref;
        return ref;
    }

    Tuple* make_tuple(GC& gc, uint64_t length)
    {
        Tuple* tuple = make_tuple_nofill(gc, length);
        for (uint64_t i = 0; i < length; i++) {
            tuple->components()[i] = Value::null();
        }
        return tuple;
    }

    Tuple* make_tuple_nofill(GC& gc, uint64_t length)
    {
        Tuple* tuple = gc.alloc<Tuple>(length);
        tuple->length = length;
        return tuple;
    }

    Array* make_array(GC& gc, uint64_t length)
    {
        Array* array = make_array_nofill(gc, length);
        for (uint64_t i = 0; i < length; i++) {
            array->components()[i] = Value::null();
        }
        return array;
    }

    Array* make_array_nofill(GC& gc, uint64_t length)
    {
        Array* array = gc.alloc<Array>(length);
        array->length = length;
        return array;
    }

    Vector* make_vector(GC& gc, uint64_t capacity)
    {
        Root<Array> r_array(gc, make_array(gc, /* length */ capacity));
        return make_vector(gc, /* length */ 0, r_array);
    }

    Vector* make_vector(GC& gc, uint64_t length, Root<Array>& r_array)
    {
        Vector* vec = gc.alloc<Vector>();
        Array* array = *r_array;
        if (length > array->length) {
            throw std::invalid_argument("length must be at most r_array length");
        }
        vec->length = length;
        vec->v_array = r_array.value();
        return vec;
    }

    Vector* make_vector(GC& gc, uint64_t length, Array* array)
    {
        Root<Array> r_array(gc, std::move(array));
        return make_vector(gc, length, r_array);
    }

    Module* make_module(GC& gc, OptionalRoot<Module>& r_base, uint64_t capacity)
    {
        Module* module = gc.alloc<Module>(capacity);
        module->v_base = r_base.value();
        module->capacity = capacity;
        module->length = 0;
        // No need to initialize entries; GC doesn't look at entries beyond the (zero) length.
        return module;
    }

    String* make_string(GC& gc, const std::string& src)
    {
        size_t length = src.size();
        String* str = gc.alloc<String>(length);
        str->length = length;
        memcpy(str->contents(), src.c_str(), length);
        return str;
    }

    Code* make_code(GC& gc, Root<Module>& r_module, uint32_t num_regs, uint32_t num_data,
                    OptionalRoot<Array>& r_upreg_map, Root<Array>& r_insts, Root<Array>& r_args)
    {
        // TODO: check that insts are fixnums of the right range?
        Code* code = gc.alloc<Code>();
        code->v_module = r_module.value();
        code->num_regs = num_regs;
        code->num_data = num_data;
        code->v_upreg_map = r_upreg_map.value();
        code->v_insts = r_insts.value();
        code->v_args = r_args.value();
        return code;
    }

    Closure* make_closure(GC& gc, Root<Code>& r_code, Root<Array>& r_upregs)
    {
        Closure* closure = gc.alloc<Closure>();
        closure->v_code = r_code.value();
        closure->v_upregs = r_upregs.value();
        return closure;
    }

    Method* make_method(GC& gc, ValueRoot& r_param_matchers, OptionalRoot<Type>& r_return_type,
                        OptionalRoot<Code>& r_code, Root<Vector>& r_attributes,
                        NativeHandler native_handler)
    {
        // if (!r_param_matchers->is_obj_vector()) {
        //     throw std::invalid_argument("r_param_matchers must be a Vector");
        // }
        // No way to check native_handler.
        {
            bool has_code = r_code;
            bool has_native_handler = native_handler != nullptr;
            int options_selected = (has_code ? 1 : 0) + (has_native_handler ? 1 : 0);
            if (options_selected != 1) {
                throw std::invalid_argument(
                    "exactly one of r_code and native_handler must be instantiated");
            }
        }

        Method* method = gc.alloc<Method>();
        method->v_param_matchers = *r_param_matchers;
        method->v_return_type = r_return_type.value();
        method->v_code = r_code.value();
        method->v_attributes = r_attributes.value();
        method->native_handler = native_handler;
        return method;
    }

    MultiMethod* make_multimethod(GC& gc, Root<String>& r_name, Root<Vector>& r_methods,
                                  Root<Vector>& r_attributes)
    {
        // TODO: check for Method components in r_methods?
        MultiMethod* multimethod = gc.alloc<MultiMethod>();
        multimethod->v_name = r_name.value();
        multimethod->v_methods = r_methods.value();
        multimethod->v_attributes = r_attributes.value();
        return multimethod;
    }

    Type* make_type(GC& gc, Root<String>& r_name, Root<Vector>& r_bases, bool sealed,
                    Root<Vector>& r_linearization, Root<Vector>& r_subtypes, Type::Kind kind,
                    OptionalRoot<Vector>& r_slots)
    {
        // TODO: check Type components in r_bases
        // Nothing to check for `sealed`.
        // TODO: check linearization? (at least some basic sanity checks)
        // TODO check Type components in r_subtypes
        if (!(kind == Type::Kind::MIXIN || kind == Type::Kind::DATACLASS)) {
            throw std::invalid_argument("kind must be MIXIN or DATACLASS");
        }
        if (kind == Type::Kind::MIXIN && r_slots) {
            throw std::invalid_argument("r_slots must be null for MIXIN type");
        }
        if (kind == Type::Kind::DATACLASS && !r_slots) {
            throw std::invalid_argument("r_slots must be a Vector for DATACLASS type");
        }

        Type* type = gc.alloc<Type>();
        type->v_name = r_name.value();
        type->v_bases = r_bases.value();
        type->sealed = sealed;
        type->v_linearization = r_linearization.value();
        type->v_subtypes = r_subtypes.value();
        type->kind = kind;
        type->v_slots = r_slots.value();
        return type;
    }

    DataclassInstance* make_instance_nofill(GC& gc, Root<Type>& r_type)
    {
        Type* type = *r_type;
        if (type->kind != Type::Kind::DATACLASS) {
            throw std::invalid_argument("r_type must be a DATACLASS-kind type");
        }

        uint64_t num_slots = type->v_slots.obj_vector()->length;
        DataclassInstance* inst = gc.alloc<DataclassInstance>(num_slots);
        inst->v_type = r_type.value();
        return inst;
    }

    Vector* append(GC& gc, Root<Vector>& r_vector, ValueRoot& r_value)
    {
        Vector* vector = *r_vector;

        uint64_t capacity = vector->capacity();
        if (vector->length == capacity) {
            // Reallocate! The original vector and backing array is kept alive by the r_vector root
            // while we copy components over.
            uint64_t new_capacity = capacity == 0 ? 1 : capacity * 2;
            Array* new_array = make_array_nofill(gc, new_capacity);
            vector = *r_vector;
            // Copy components and null-fill the rest.
            {
                Array* array = vector->v_array.obj_array();
                for (uint64_t i = 0; i < capacity; i++) {
                    new_array->components()[i] = array->components()[i];
                }
                for (uint64_t i = capacity; i < new_capacity; i++) {
                    new_array->components()[i] = Value::null();
                }
            }
            // Pin the new_array while allocating the new vector.
            Root<Array> r_new_array(gc, std::move(new_array));
            Vector* new_vector = make_vector(gc, vector->length, r_new_array);

            vector = new_vector;
        }

        vector->v_array.obj_array()->components()[vector->length++] = *r_value;
        return vector;
    }

    void append(GC& gc, Root<Module>& r_module, Root<String>& r_name, ValueRoot& r_value)
    {
        throw std::logic_error("module append not implemented");
        // if (module->length == module->capacity) {
        //     // Reallocate!
        //     // TODO: grow more slowly? modules probably don't need 2x growth, maaaaybe 1.5
        //     uint64_t new_capacity = module->capacity == 0 ? 1 : module->capacity * 2;
        //     Module* new_module = make_module(gc, module->v_base, new_capacity);
        //     for (uint64_t i = 0; i < module->length; i++) {
        //         new_module->entries()[i] = module->entries()[i];
        //     }
        //     // TODO: need to do similar thing to vectors, which have distinct backing array
        //     module = new_module;
        // }
        // Module::Entry& entry = module->entries()[module->length++];
        // entry.v_key = Value::object(name);
        // entry.v_value = v_value;
    }

    Value* module_lookup(Module* module, String* name)
    {
        uint64_t name_length = name->length;
        // TODO: check against size_t?

        while (module) {
            uint64_t num_entries = module->length;
            for (uint64_t i = 0; i < num_entries; i++) {
                Module::Entry& entry = module->entries()[i];
                String* entry_name = entry.v_key.obj_string();
                if (entry_name->length != name_length) {
                    continue;
                }
                if (memcmp(entry_name->contents(), name->contents(), name_length) != 0) {
                    continue;
                }
                // Match!
                return &entry.v_value;
            }
            module = module->v_base.is_null() ? nullptr : module->v_base.obj_module();
        }

        return nullptr;
    }

    bool string_eq(String* a, String* b)
    {
        // TODO: store hashes?
        if (a->length != b->length) {
            return false;
        }
        // TODO: compare length against size_t?
        return memcmp(a->contents(), b->contents(), a->length) == 0;
    }

    bool string_eq(String* a, const std::string& b)
    {
        size_t b_length = b.size();
        if (a->length != b_length) {
            return false;
        }
        return memcmp(a->contents(), b.c_str(), b_length) == 0;
    }

    String* concat(GC& gc, const std::vector<std::string>& parts)
    {
        size_t total_len = 0;
        for (const std::string& part : parts) {
            total_len += part.size();
        }

        String* cat = gc.alloc<String>(total_len);
        cat->length = total_len;

        size_t offset = 0;
        for (const std::string& part : parts) {
            const size_t part_size = part.size();
            memcpy(cat->contents() + offset, part.c_str(), part_size);
            offset += part_size;
        }

        return cat;
    }

    String* concat_with_suffix(GC& gc, const std::vector<std::string>& parts,
                               const std::string& each_suffix)
    {
        size_t suffix_size = each_suffix.size();

        size_t total_len = 0;
        for (const std::string& part : parts) {
            total_len += part.size() + suffix_size;
        }

        String* cat = gc.alloc<String>(total_len);
        cat->length = total_len;

        size_t offset = 0;
        for (const std::string& part : parts) {
            const size_t part_size = part.size();
            memcpy(cat->contents() + offset, part.c_str(), part_size);
            offset += part_size;
            memcpy(cat->contents() + offset, each_suffix.c_str(), suffix_size);
            offset += suffix_size;
        }

        return cat;
    }
};
