#include "value_utils.h"

#include <cstring>

namespace Katsu
{
    Ref* make_ref(GC& gc, Root& r_ref)
    {
        Ref* ref = gc.alloc<Ref>();
        ref->v_ref = *r_ref;
        return ref;
    }

    Ref* make_ref(GC& gc, Value v_ref)
    {
        Root r_ref(gc, std::move(v_ref));
        return make_ref(gc, r_ref);
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
        Root r_array(gc, Value::object(make_array(gc, /* length */ capacity)));
        return make_vector(gc, /* length */ 0, r_array);
    }

    Vector* make_vector(GC& gc, uint64_t length, Root& r_array)
    {
        if (!r_array->is_obj_array()) {
            throw std::invalid_argument("r_array must be an Array");
        }

        Vector* vec = gc.alloc<Vector>();
        Array* array = r_array->obj_array();
        if (length > array->length) {
            throw std::invalid_argument("length must be at most r_array length");
        }
        vec->length = length;
        vec->v_array = *r_array;
        return vec;
    }

    Vector* make_vector(GC& gc, uint64_t length, Array* array)
    {
        Root r_array(gc, Value::object(array));
        return make_vector(gc, length, r_array);
    }

    Module* make_module(GC& gc, Root& r_base, uint64_t capacity)
    {
        if (!(r_base->is_obj_module() || r_base->is_null())) {
            throw std::invalid_argument("r_base must be a Module or null");
        }

        Module* module = gc.alloc<Module>(capacity);
        module->v_base = *r_base;
        module->capacity = capacity;
        module->length = 0;
        // No need to initialize entries; GC doesn't look at entries beyond the (zero) length.
        return module;
    }

    Module* make_module(GC& gc, Module* base, uint64_t capacity)
    {
        Value v_base = base ? Value::object(base) : Value::null();
        Root r_base(gc, std::move(v_base));
        return make_module(gc, r_base, capacity);
    }

    String* make_string(GC& gc, const std::string& src)
    {
        size_t length = src.size();
        String* str = gc.alloc<String>(length);
        str->length = length;
        memcpy(str->contents(), src.c_str(), length);
        return str;
    }

    Code* make_code(GC& gc, Root& r_module, uint32_t num_regs, uint32_t num_data, Root& r_upreg_map,
                    Root& r_insts, Root& r_args)
    {
        if (!r_module->is_obj_module()) {
            throw std::invalid_argument("r_module must be a Module");
        }
        if (!(r_upreg_map->is_obj_array() || r_upreg_map->is_null())) {
            throw std::invalid_argument("r_upreg_map must be an Array or null");
        }
        if (!r_insts->is_obj_array()) {
            throw std::invalid_argument("r_insts must be an Array");
        }
        // TODO: check for fixnums of the right range?
        if (!r_args->is_obj_array()) {
            throw std::invalid_argument("r_args must be an Array");
        }

        Code* code = gc.alloc<Code>();
        code->v_module = *r_module;
        code->num_regs = num_regs;
        code->num_data = num_data;
        code->v_upreg_map = *r_upreg_map;
        code->v_insts = *r_insts;
        code->v_args = *r_args;
        return code;
    }

    Code* make_code(GC& gc, Module* module, uint32_t num_regs, uint32_t num_data, Array* upreg_map,
                    Array* insts, Array* args)
    {
        Root r_module(gc, Value::object(module));
        Value v_upreg_map = upreg_map ? Value::object(upreg_map) : Value::null();
        Root r_upreg_map(gc, std::move(v_upreg_map));
        Root r_insts(gc, Value::object(insts));
        Root r_args(gc, Value::object(args));
        return make_code(gc, r_module, num_regs, num_data, r_upreg_map, r_insts, r_args);
    }

    Closure* make_closure(GC& gc, Root& r_code, Root& r_upregs)
    {
        if (!r_code->is_obj_code()) {
            throw std::invalid_argument("r_code must be a Code");
        }
        if (!r_upregs->is_obj_array()) {
            throw std::invalid_argument("r_upregs must be an Array");
        }

        Closure* closure = gc.alloc<Closure>();
        closure->v_code = *r_code;
        closure->v_upregs = *r_upregs;
        return closure;
    }

    Closure* make_closure(GC& gc, Code* code, Array* upregs)
    {
        Root r_code(gc, Value::object(code));
        Root r_upregs(gc, Value::object(upregs));
        return make_closure(gc, r_code, r_upregs);
    }

    Method* make_method(GC& gc, Root& r_param_matchers, Root& r_return_type, Root& r_code,
                        Root& r_attributes, NativeHandler native_handler)
    {
        // if (!r_param_matchers->is_obj_vector()) {
        //     throw std::invalid_argument("r_param_matchers must be a Vector");
        // }
        if (!(r_return_type->is_obj_type() || r_return_type->is_null())) {
            throw std::invalid_argument("r_return_type must be a Type or null");
        }
        if (!(r_code->is_obj_code() || r_code->is_null())) {
            throw std::invalid_argument("r_code must be a Code or null");
        }
        if (!r_attributes->is_obj_vector()) {
            throw std::invalid_argument("r_attributes must be a Vector");
        }
        // No way to check native_handler.
        {
            bool has_code = r_code->is_obj_code();
            bool has_native_handler = native_handler != nullptr;
            int options_selected = (has_code ? 1 : 0) + (has_native_handler ? 1 : 0);
            if (options_selected != 1) {
                throw std::invalid_argument(
                    "exactly one of r_code and native_handler must be instantiated");
            }
        }

        Method* method = gc.alloc<Method>();
        method->v_param_matchers = *r_param_matchers;
        method->v_return_type = *r_return_type;
        method->v_code = *r_code;
        method->v_attributes = *r_attributes;
        method->native_handler = native_handler;
        return method;
    }

    Method* make_method(GC& gc, Value v_param_matchers, Type* return_type, Code* code,
                        Vector* attributes, NativeHandler native_handler)
    {
        Root r_param_matchers(gc, std::move(v_param_matchers));
        Value v_return_type = return_type ? Value::object(return_type) : Value::null();
        Root r_return_type(gc, std::move(v_return_type));
        Value v_code = code ? Value::object(code) : Value::null();
        Root r_code(gc, std::move(v_code));
        Root r_attributes(gc, Value::object(attributes));
        return make_method(gc,
                           r_param_matchers,
                           r_return_type,
                           r_code,
                           r_attributes,
                           native_handler);
    }

    MultiMethod* make_multimethod(GC& gc, Root& r_name, Root& r_methods, Root& r_attributes)
    {
        if (!r_name->is_obj_string()) {
            throw std::invalid_argument("r_name must be a String");
        }
        if (!r_methods->is_obj_vector()) {
            throw std::invalid_argument("r_methods must be a Vector");
        }
        // TODO: check for Method components?
        if (!r_attributes->is_obj_vector()) {
            throw std::invalid_argument("r_attributes must be a Vector");
        }

        MultiMethod* multimethod = gc.alloc<MultiMethod>();
        multimethod->v_name = *r_name;
        multimethod->v_methods = *r_methods;
        multimethod->v_attributes = *r_attributes;
        return multimethod;
    }

    MultiMethod* make_multimethod(GC& gc, String* name, Vector* methods, Vector* attributes)
    {
        Root r_name(gc, Value::object(name));
        Root r_methods(gc, Value::object(methods));
        Root r_attributes(gc, Value::object(attributes));
        return make_multimethod(gc, r_name, r_methods, r_attributes);
    }

    Type* make_type(GC& gc, Root& r_name, Root& r_bases, bool sealed, Root& r_linearization,
                    Root& r_subtypes, Type::Kind kind, Root& r_slots)
    {
        if (!r_name->is_obj_string()) {
            throw std::invalid_argument("r_name must be a String");
        }
        if (!r_bases->is_obj_vector()) {
            throw std::invalid_argument("r_bases must be a Vector");
        }
        // TODO: check Type components
        // Nothing to check for `sealed`.
        if (!r_linearization->is_obj_vector()) {
            throw std::invalid_argument("r_linearization must be a Vector");
        }
        // TODO: check linearization? (at least some basic sanity checks)
        if (!r_subtypes->is_obj_vector()) {
            throw std::invalid_argument("r_subtypes must be a Vector");
        }
        // TODO check Type components
        if (!(kind == Type::Kind::MIXIN || kind == Type::Kind::DATACLASS)) {
            throw std::invalid_argument("kind must be MIXIN or DATACLASS");
        }
        if (kind == Type::Kind::MIXIN && !r_slots->is_null()) {
            throw std::invalid_argument("r_slots must be null for MIXIN type");
        }
        if (kind == Type::Kind::DATACLASS && !r_slots->is_obj_vector()) {
            throw std::invalid_argument("r_slots must be a Vector for DATACLASS type");
        }

        Type* type = gc.alloc<Type>();
        type->v_name = *r_name;
        type->v_bases = *r_bases;
        type->sealed = sealed;
        type->v_linearization = *r_linearization;
        type->v_subtypes = *r_subtypes;
        type->kind = kind;
        type->v_slots = *r_slots;
        return type;
    }

    Type* make_type(GC& gc, String* name, Vector* bases, bool sealed, Vector* linearization,
                    Vector* subtypes, Type::Kind kind, Vector* slots)
    {
        Root r_name(gc, Value::object(name));
        Root r_bases(gc, Value::object(bases));
        Root r_linearization(gc, Value::object(linearization));
        Root r_subtypes(gc, Value::object(subtypes));
        Value v_slots = slots ? Value::object(slots) : Value::null();
        Root r_slots(gc, std::move(v_slots));
        return make_type(gc, r_name, r_bases, sealed, r_linearization, r_subtypes, kind, r_slots);
    }

    DataclassInstance* make_instance_nofill(GC& gc, Root& r_type)
    {
        if (!r_type->is_obj_type()) {
            throw std::invalid_argument("r_type must be a Type");
        }

        Type* type = r_type->obj_type();
        if (type->kind != Type::Kind::DATACLASS) {
            throw std::invalid_argument("r_type must be a DATACLASS-kind type");
        }

        uint64_t num_slots = type->v_slots.obj_vector()->length;
        DataclassInstance* inst = gc.alloc<DataclassInstance>(num_slots);
        inst->v_type = *r_type;
        return inst;
    }

    DataclassInstance* make_instance_nofill(GC& gc, Type* type)
    {
        Root r_type(gc, Value::object(type));
        return make_instance_nofill(gc, r_type);
    }

    Vector* append(GC& gc, Root& r_vector, Root& r_value)
    {
        if (!r_vector->is_obj_vector()) {
            throw std::invalid_argument("r_vector must be a Vector");
        }

        Vector* vector = r_vector->obj_vector();

        uint64_t capacity = vector->capacity();
        if (vector->length == capacity) {
            // Reallocate! The original vector and backing array is kept alive by the r_vector root
            // while we copy components over.
            uint64_t new_capacity = capacity == 0 ? 1 : capacity * 2;
            Array* new_array = make_array_nofill(gc, new_capacity);
            vector = r_vector->obj_vector();
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
            Root r_new_array(gc, Value::object(new_array));
            Vector* new_vector = make_vector(gc, vector->length, r_new_array);

            vector = new_vector;
        }

        vector->v_array.obj_array()->components()[vector->length++] = *r_value;
        return vector;
    }

    Vector* append(GC& gc, Vector* vector, Value v_value)
    {
        Root r_vector(gc, Value::object(vector));
        Root r_value(gc, std::move(v_value));
        return append(gc, r_vector, r_value);
    }

    // TODO: handle as part of Module cleanup.
    // void append(GC& gc, Module* module, String* name, Root& r_value)
    // {
    //     if (module->length == module->capacity) {
    //         // Reallocate!
    //         // TODO: grow more slowly? modules probably don't need 2x growth, maaaaybe 1.5
    //         uint64_t new_capacity = module->capacity == 0 ? 1 : module->capacity * 2;
    //         Module* new_module = make_module(gc, module->v_base, new_capacity);
    //         for (uint64_t i = 0; i < module->length; i++) {
    //             new_module->entries()[i] = module->entries()[i];
    //         }
    //         // TODO: need to do similar thing to vectors, which have distinct backing array
    //         module = new_module;
    //     }
    //     Module::Entry& entry = module->entries()[module->length++];
    //     entry.v_key = Value::object(name);
    //     entry.v_value = v_value;
    // }

    Value* module_lookup(Module* module, String* name)
    {
        uint64_t name_length = name->length;
        // TODO: check against size_t?

        Value v_module = Value::object(module);
        while (!v_module.is_null()) {
            Module* module = v_module.obj_module();
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
            v_module = module->v_base;
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
};
