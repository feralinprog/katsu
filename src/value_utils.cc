#include "value_utils.h"

#include <cstring>

namespace Katsu
{
    Ref* make_ref(GC& gc, Value v_ref)
    {
        Ref* ref = gc.alloc<Ref>();
        ref->v_ref = v_ref;
        return ref;
    }

    Tuple* make_tuple(GC& gc, uint64_t length)
    {
        Tuple* tuple = gc.alloc<Tuple>(length);
        tuple->length = length;
        for (uint64_t i = 0; i < length; i++) {
            tuple->components()[i] = Value::null();
        }
        return tuple;
    }

    Tuple* make_tuple(GC& gc, uint64_t length, Value* components)
    {
        Tuple* tuple = gc.alloc<Tuple>(length);
        tuple->length = length;
        for (uint64_t i = 0; i < length; i++) {
            tuple->components()[i] = components[i];
        }
        return tuple;
    }

    Array* make_array(GC& gc, uint64_t length)
    {
        return make_array(gc, length, /* num_components */ 0, /* components */ nullptr);
    }

    Array* make_array(GC& gc, uint64_t length, Value* components)
    {
        return make_array(gc, length, /* num_components */ length, components);
    }

    Array* make_array(GC& gc, uint64_t length, uint64_t num_components, Value* components)
    {
        if (length < num_components) {
            throw std::invalid_argument("length must be at least num_components");
        }

        Array* array = gc.alloc<Array>(length);
        array->length = length;
        for (uint64_t i = 0; i < num_components; i++) {
            array->components()[i] = components[i];
        }
        for (uint64_t i = num_components; i < length; i++) {
            array->components()[i] = Value::null();
        }
        return array;
    }

    Vector* make_vector(GC& gc, uint64_t capacity)
    {
        return make_vector(gc, capacity, /* length */ 0, /* components */ nullptr);
    }

    Vector* make_vector(GC& gc, uint64_t capacity, uint64_t length, Value* components)
    {
        if (capacity < length) {
            throw std::invalid_argument("capacity must be at least length");
        }

        Root r_array(
            gc,
            Value::object(
                make_array(gc, /* length */ capacity, /* num_components */ length, components)));
        Vector* vec = gc.alloc<Vector>();
        vec->length = length;
        vec->v_array = r_array.get();
        return vec;
    }

    Module* make_module(GC& gc, Value v_base, uint64_t capacity)
    {
        if (!(v_base.is_obj_module() || v_base.is_null())) {
            throw std::invalid_argument("v_base must be a Module or null");
        }

        Module* module = gc.alloc<Module>(capacity);
        module->v_base = v_base;
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

    Code* make_code(GC& gc, Value v_module, uint32_t num_regs, uint32_t num_data, Value v_upreg_map,
                    Value v_insts, Value v_args)
    {
        if (!v_module.is_obj_module()) {
            throw std::invalid_argument("v_module must be a Module");
        }
        if (!(v_upreg_map.is_obj_array() || v_upreg_map.is_null())) {
            throw std::invalid_argument("v_upreg_map must be an Array or null");
        }
        if (!v_insts.is_obj_array()) {
            throw std::invalid_argument("v_insts must be an Array");
        }
        // TODO: check for fixnums of the right range?
        if (!v_args.is_obj_array()) {
            throw std::invalid_argument("v_args must be an Array");
        }

        Code* code = gc.alloc<Code>();
        code->v_module = v_module;
        code->num_regs = num_regs;
        code->num_data = num_data;
        code->v_upreg_map = v_upreg_map;
        code->v_insts = v_insts;
        code->v_args = v_args;
        return code;
    }

    Closure* make_closure(GC& gc, Value v_code, Value v_upregs)
    {
        if (!v_code.is_obj_code()) {
            throw std::invalid_argument("v_code must be a Code");
        }
        if (!v_upregs.is_obj_array()) {
            throw std::invalid_argument("v_upregs must be an Array");
        }

        Closure* closure = gc.alloc<Closure>();
        closure->v_code = v_code;
        closure->v_upregs = v_upregs;
        return closure;
    }

    Method* make_method(GC& gc, Value v_param_matchers, Value v_return_type, Value v_code,
                        Value v_attributes, NativeHandler native_handler)
    {
        // if (!v_param_matchers.is_obj_vector()) {
        //     throw std::invalid_argument("v_param_matchers must be a Vector");
        // }
        if (!(v_return_type.is_obj_type() || v_return_type.is_null())) {
            throw std::invalid_argument("v_return_type must be a Type or null");
        }
        if (!(v_code.is_obj_code() || v_code.is_null())) {
            throw std::invalid_argument("v_code must be a Code or null");
        }
        if (!v_attributes.is_obj_vector()) {
            throw std::invalid_argument("v_attributes must be a Vector");
        }
        // No way to check native_handler.
        {
            bool has_code = v_code.is_obj_code();
            bool has_native_handler = native_handler != nullptr;
            int options_selected = (has_code ? 1 : 0) + (has_native_handler ? 1 : 0);
            if (options_selected != 1) {
                throw std::invalid_argument(
                    "exactly one of v_code and native_handler must be instantiated");
            }
        }

        Method* method = gc.alloc<Method>();
        method->v_param_matchers = v_param_matchers;
        method->v_return_type = v_return_type;
        method->v_code = v_code;
        method->v_attributes = v_attributes;
        method->native_handler = native_handler;
        return method;
    }

    MultiMethod* make_multimethod(GC& gc, Value v_name, Value v_methods, Value v_attributes)
    {
        if (!v_name.is_obj_string()) {
            throw std::invalid_argument("v_name must be a String");
        }
        if (!v_methods.is_obj_vector()) {
            throw std::invalid_argument("v_methods must be a Vector");
        }
        // TODO: check for Method components?
        if (!v_attributes.is_obj_vector()) {
            throw std::invalid_argument("v_attributes must be a Vector");
        }

        MultiMethod* multimethod = gc.alloc<MultiMethod>();
        multimethod->v_name = v_name;
        multimethod->v_methods = v_methods;
        multimethod->v_attributes = v_attributes;
        return multimethod;
    }

    Type* make_type(GC& gc, Value v_name, Value v_bases, bool sealed, Value v_linearization,
                    Value v_subtypes, Type::Kind kind, Value v_slots)
    {
        if (!v_name.is_obj_string()) {
            throw std::invalid_argument("v_name must be a String");
        }
        if (!v_bases.is_obj_vector()) {
            throw std::invalid_argument("v_bases must be a Vector");
        }
        // TODO: check Type components
        // Nothing to check for `sealed`.
        if (!v_linearization.is_obj_vector()) {
            throw std::invalid_argument("v_linearization must be a Vector");
        }
        // TODO: check linearization? (at least some basic sanity checks)
        if (!v_subtypes.is_obj_vector()) {
            throw std::invalid_argument("v_subtypes must be a Vector");
        }
        // TODO check Type components
        if (!(kind == Type::Kind::MIXIN || kind == Type::Kind::DATACLASS)) {
            throw std::invalid_argument("kind must be MIXIN or DATACLASS");
        }
        if (kind == Type::Kind::MIXIN && !v_slots.is_null()) {
            throw std::invalid_argument("v_slots must be null for MIXIN type");
        }
        if (kind == Type::Kind::DATACLASS && !v_slots.is_obj_vector()) {
            throw std::invalid_argument("v_slots must be a Vector for DATACLASS type");
        }

        Type* type = gc.alloc<Type>();
        type->v_name = v_name;
        type->v_bases = v_bases;
        type->sealed = sealed;
        type->v_linearization = v_linearization;
        type->v_subtypes = v_subtypes;
        type->kind = kind;
        type->v_slots = v_slots;
        return type;
    }

    DataclassInstance* make_instance(GC& gc, Value v_type, Value* slots)
    {
        if (!v_type.is_obj_type()) {
            throw std::invalid_argument("v_type must be a Type");
        }

        Type* type = v_type.obj_type();
        if (type->kind != Type::Kind::DATACLASS) {
            throw std::invalid_argument("v_type must be a DATACLASS-kind type");
        }

        uint64_t num_slots = type->v_slots.obj_vector()->length;
        DataclassInstance* inst = gc.alloc<DataclassInstance>(num_slots);
        inst->v_type = v_type;
        for (uint64_t i = 0; i < num_slots; i++) {
            inst->slots()[i] = slots[i];
        }
        return inst;
    }

    void append(GC& gc, Vector* vector, Value v_value)
    {
        uint64_t capacity = vector->capacity();
        if (vector->length == capacity) {
            // Reallocate!
            // Make sure we keep the original vector alive while copying components over.
            Root r_vec(gc, Value::object(vector));
            uint64_t new_capacity = capacity == 0 ? 1 : capacity * 2;
            vector = make_vector(gc,
                                 new_capacity,
                                 vector->length,
                                 vector->v_array.obj_array()->components());
        }
        vector->v_array.obj_array()->components()[vector->length++] = v_value;
    }

    void append(GC& gc, Module* module, String* name, Value v_value)
    {
        if (module->length == module->capacity) {
            // Reallocate!
            // TODO: grow more slowly? modules probably don't need 2x growth, maaaaybe 1.5
            uint64_t new_capacity = module->capacity == 0 ? 1 : module->capacity * 2;
            Module* new_module = make_module(gc, module->v_base, new_capacity);
            for (uint64_t i = 0; i < module->length; i++) {
                new_module->entries()[i] = module->entries()[i];
            }
            // TODO: need to do similar thing to vectors, which have distinct backing array
            module = new_module;
        }
        Module::Entry& entry = module->entries()[module->length++];
        entry.v_key = Value::object(name);
        entry.v_value = v_value;
    }

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
