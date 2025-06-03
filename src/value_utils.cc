#include "value_utils.h"

#include "assertions.h"
#include "vm.h"

#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>

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
#if DEBUG_ASSERTIONS
        Array* array = *r_array;
        ASSERT_ARG(length <= array->length);
#endif
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
        Root<Array> r_array(gc, make_array(gc, /* length */ capacity * 2));
        Module* module = gc.alloc<Module>();
        module->v_base = r_base.value();
        module->length = 0;
        module->v_array = r_array.value();
        return module;
    }

    String* make_string(GC& gc, const std::string& src)
    {
        size_t length = src.size();
        String* str = make_string_nofill(gc, length);
        memcpy(str->contents(), src.c_str(), length);
        return str;
    }

    String* make_string_nofill(GC& gc, uint64_t length)
    {
        String* str = gc.alloc<String>(length);
        str->length = length;
        return str;
    }

    Code* make_code(GC& gc, Root<Module>& r_module, uint32_t num_params, uint32_t num_regs,
                    uint32_t num_data, OptionalRoot<Array>& r_upreg_map, Root<Array>& r_insts,
                    Root<Array>& r_args)
    {
        ASSERT_ARG(num_params <= num_regs);
        // TODO: check that insts are fixnums of the right range?
        Code* code = gc.alloc<Code>();
        code->v_module = r_module.value();
        code->num_params = num_params;
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
                        NativeHandler native_handler, IntrinsicHandler intrinsic_handler)
    {
        // No way to check native_handler or intrinsic_handler.
#if DEBUG_ASSERTIONS
        {
            bool has_code = r_code;
            bool has_native_handler = native_handler != nullptr;
            bool has_intrinsic_handler = intrinsic_handler != nullptr;
            int options_selected =
                (has_code ? 1 : 0) + (has_native_handler ? 1 : 0) + (has_intrinsic_handler ? 1 : 0);
            ASSERT_ARG_MSG(options_selected == 1,
                           "exactly one of r_code, native_handler, and intrinsic_handler must be "
                           "instantiated");
        }
#endif

        Method* method = gc.alloc<Method>();
        method->v_param_matchers = *r_param_matchers;
        method->v_return_type = r_return_type.value();
        method->v_code = r_code.value();
        method->v_attributes = r_attributes.value();
        method->native_handler = native_handler;
        method->intrinsic_handler = intrinsic_handler;
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

    Type* make_type_raw(GC& gc, Root<String>& r_name, Root<Array>& r_bases, bool sealed,
                        Root<Array>& r_linearization, Root<Vector>& r_subtypes, Type::Kind kind,
                        OptionalRoot<Vector>& r_slots)
    {
        // TODO: check Type components in r_bases
        // Nothing to check for `sealed`.
        // TODO: check linearization? (at least some basic sanity checks)
        // TODO check Type components in r_subtypes
        ASSERT_ARG(kind == Type::Kind::PRIMITIVE || kind == Type::Kind::DATACLASS ||
                   kind == Type::Kind::MIXIN);
        if (kind == Type::Kind::PRIMITIVE) {
            ASSERT_ARG_MSG(!r_slots, "PRIMITIVE type must have no slots");
        }
        if (kind == Type::Kind::DATACLASS) {
            ASSERT_ARG_MSG(r_slots, "DATACLASS type must have a Vector of slots");
        }
        if (kind == Type::Kind::MIXIN) {
            ASSERT_ARG_MSG(!r_slots, "MIXIN type must have no slots");
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
        ASSERT_ARG(type->kind == Type::Kind::DATACLASS);

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
            // Reallocate the backing array! The original backing array (and vector) are kept alive
            // by the r_vector root while we copy components over.
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
            vector->v_array = Value::object(new_array);
        }

        vector->v_array.obj_array()->components()[vector->length++] = *r_value;
        return vector;
    }

    Module* append(GC& gc, Root<Module>& r_module, Root<String>& r_name, ValueRoot& r_value)
    {
        Module* module = *r_module;

        uint64_t array_capacity = module->v_array.obj_array()->length;
        ASSERT_MSG(array_capacity % 2 == 0, "module backing array should have even length");
        uint64_t entries_capacity = array_capacity / 2;
        if (module->length == entries_capacity) {
            // Reallocate the backing array! The original backing array (and module) are kept alive
            // by the r_vector root while we copy components over.
            // TODO: probably don't need to double. Maybe 1.5x?
            uint64_t new_entries_capacity = entries_capacity == 0 ? 1 : entries_capacity * 2;
            uint64_t new_array_capacity = new_entries_capacity * 2;
            Array* new_array = make_array_nofill(gc, new_array_capacity);
            module = *r_module;
            // Copy components and null-fill the rest.
            {
                Array* array = module->v_array.obj_array();
                for (uint64_t i = 0; i < array_capacity; i++) {
                    new_array->components()[i] = array->components()[i];
                }
                for (uint64_t i = array_capacity; i < new_array_capacity; i++) {
                    new_array->components()[i] = Value::null();
                }
            }
            module->v_array = Value::object(new_array);
        }

        Module::Entry& entry = module->entries()[module->length++];
        entry.v_key = r_name.value();
        entry.v_value = *r_value;
        return module;
    }

    Array* vector_to_array(GC& gc, Root<Vector>& r_vector)
    {
        Array* array = make_array_nofill(gc, r_vector->length);
        Array* src = r_vector->v_array.obj_array();
        for (uint64_t i = 0; i < array->length; i++) {
            array->components()[i] = src->components()[i];
        }
        return array;
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

    void pprint(std::vector<Object*>& objects_seen, Value value, int depth,
                const std::string& prefix, bool initial_indent = true)
    {
        auto indent = [](int depth) {
            for (int i = 0; i < depth; i++) {
                std::cout << "  ";
            }
        };
        auto pchild = [&objects_seen, depth](Value child,
                                             const std::string& prefix = "",
                                             bool initial_indent = true,
                                             int extra_depth = 1) {
            pprint(objects_seen, child, depth + extra_depth, prefix, initial_indent);
        };
        auto pnative = [&indent, depth]() -> std::ostream& {
            indent(depth + 1);
            return std::cout;
        };

        if (initial_indent) {
            indent(depth);
        }
        std::cout << prefix;

        if (value.is_fixnum()) {
            std::cout << "fixnum " << value.fixnum() << "\n";
        } else if (value.is_float()) {
            std::cout << "float " << value._float() << "\n";
        } else if (value.is_bool()) {
            std::cout << "bool " << value._bool() << "\n";
        } else if (value.is_null()) {
            std::cout << "null\n";
        } else if (value.is_object()) {
            Object* obj = value.object();
            for (size_t i = 0; i < objects_seen.size(); i++) {
                if (objects_seen[i] == obj) {
                    std::cout << "^up " << (objects_seen.size() - i) << "\n";
                    return;
                }
            }
            objects_seen.push_back(obj);

            if (value.is_obj_ref()) {
                Ref* o = value.obj_ref();
                std::cout << "*ref:\n";
                pchild(o->v_ref);
            } else if (value.is_obj_tuple()) {
                Tuple* o = value.obj_tuple();
                std::cout << "*tuple: length=" << o->length << " (\n";
                for (uint64_t i = 0; i < o->length; i++) {
                    std::stringstream ss;
                    ss << i << " = ";
                    pchild(o->components()[i], ss.str());
                }
                indent(depth);
                std::cout << ")\n";
            } else if (value.is_obj_array()) {
                Array* o = value.obj_array();
                std::cout << "*array: length=" << o->length << "\n";
                for (uint64_t i = 0; i < o->length; i++) {
                    std::stringstream ss;
                    ss << i << " = ";
                    pchild(o->components()[i], ss.str());
                }
            } else if (value.is_obj_vector()) {
                Vector* o = value.obj_vector();
                std::cout << "*vector: length=" << o->length << " [\n";
                pchild(o->v_array, "v_array = ");
                indent(depth);
                std::cout << "]\n";
            } else if (value.is_obj_module()) {
                Module* o = value.obj_module();
                std::cout << "*module: length=" << o->length << "\n";
                pchild(o->v_array, "v_array = ");
                pchild(o->v_base, "v_base = ");
            } else if (value.is_obj_string()) {
                String* o = value.obj_string();
                std::cout << "*string: \"";
                for (uint8_t* c = o->contents(); c < o->contents() + o->length; c++) {
                    std::cout << (char)*c;
                }
                std::cout << "\"\n";
            } else if (value.is_obj_code()) {
                Code* o = value.obj_code();
                std::cout << "*code\n";
                // TODO: add back if there's some way to fold. By default, too noisy.
                // pchild(o->v_module, "v_module = ");
                pnative() << "num_params = " << o->num_params << "\n";
                pnative() << "num_regs = " << o->num_regs << "\n";
                pnative() << "num_data = " << o->num_data << "\n";
                pchild(o->v_upreg_map, "v_upreg_map = ");
                // Special pretty-printing for code: decode the instructions and arguments!
                // TODO: better error handling in case of any nonexpected values.
                pnative() << "bytecode:\n";
                uint32_t arg_spot = 0;
                Array* args = o->v_args.obj_array();
                for (uint32_t inst_spot = 0; inst_spot < o->v_insts.obj_array()->length;
                     inst_spot++) {
                    pnative() << "[" << inst_spot << "]: ";
                    int64_t inst = o->v_insts.obj_array()->components()[inst_spot].fixnum();
                    switch (inst) {
                        case LOAD_REG: {
                            std::cout << "load_reg @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case STORE_REG: {
                            std::cout << "store_reg @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_REF: {
                            std::cout << "load_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case STORE_REF: {
                            std::cout << "store_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_VALUE: {
                            std::cout << "load_value: ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case INIT_REF: {
                            std::cout << "init_ref @" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case LOAD_MODULE: {
                            std::cout << "load_module ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case STORE_MODULE: {
                            std::cout << "store_module ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        case INVOKE:
                        case INVOKE_TAIL: {
                            std::cout << "invoke" << (inst == INVOKE ? "" : "-tail") << " #"
                                      << args->components()[arg_spot + 1].fixnum() << " ";
                            pchild(args->components()[arg_spot],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            arg_spot += 2;
                            break;
                        }
                        case DROP: {
                            std::cout << "drop\n";
                            break;
                        }
                        case MAKE_TUPLE: {
                            std::cout << "make-tuple #" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case MAKE_VECTOR: {
                            std::cout << "make-vector #" << args->components()[arg_spot++].fixnum()
                                      << "\n";
                            break;
                        }
                        case MAKE_CLOSURE: {
                            std::cout << "make-closure: ";
                            pchild(args->components()[arg_spot++],
                                   "",
                                   /* initial_indent */ false,
                                   /* extra_depth */ +1);
                            break;
                        }
                        default: {
                            std::cout << "??? (inst=" << inst << ")\n";
                            break;
                        }
                    }
                }
                // pchild(o->v_insts, "v_insts = ");
                // pchild(o->v_args, "v_args = ");
            } else if (value.is_obj_closure()) {
                Closure* o = value.obj_closure();
                std::cout << "*closure\n";
                pchild(o->v_code, "v_code = ");
                pchild(o->v_upregs, "v_upregs = ");
            } else if (value.is_obj_method()) {
                Method* o = value.obj_method();
                std::cout << "*method\n";
                pchild(o->v_param_matchers, "v_param_matchers = ");
                pchild(o->v_return_type, "v_return_type = ");
                pchild(o->v_code, "v_code = ");
                pchild(o->v_attributes, "v_attributes = ");
                pnative() << "native_handler = " << (void*)o->native_handler << "\n";
                pnative() << "intrinsic_handler = " << (void*)o->intrinsic_handler << "\n";
            } else if (value.is_obj_multimethod()) {
                MultiMethod* o = value.obj_multimethod();
                std::cout << "*multimethod\n";
                pchild(o->v_name, "v_name = ");
                pchild(o->v_methods, "v_methods = ");
                pchild(o->v_attributes, "v_attributes = ");
            } else if (value.is_obj_type()) {
                Type* o = value.obj_type();
                std::cout << "*type\n";
                pchild(o->v_name, "v_name = ");
                pchild(o->v_bases, "v_bases = ");
                pnative() << "sealed = " << o->sealed << "\n";
                pchild(o->v_linearization, "v_linearization = ");
                pchild(o->v_subtypes, "v_subtypes = ");
                pnative() << "kind = ";
                switch (o->kind) {
                    case Type::Kind::PRIMITIVE: std::cout << "primitive\n"; break;
                    case Type::Kind::DATACLASS: std::cout << "dataclass\n"; break;
                    case Type::Kind::MIXIN: std::cout << "mixin\n"; break;
                    default: std::cout << "??? (raw=" << static_cast<int>(o->kind) << ")\n"; break;
                }
                pchild(o->v_slots, "v_slots = ");
            } else if (value.is_obj_instance()) {
                DataclassInstance* o = value.obj_instance();
                std::cout << "*instance\n";
                pchild(o->v_type, "v_type = ");
                pnative() << "slots: (TODO)\n";
            } else {
                std::cout << "object: ??? (object tag = " << static_cast<int>(value.object()->tag())
                          << ")\n";
            }

            objects_seen.pop_back();
        } else {
            std::cout << "??? (tag = " << static_cast<int>(value.tag())
                      << ", raw value = " << value.raw_value() << "(0x" << std::hex
                      << value.raw_value() << std::dec << "))\n";
        }
    }

    void pprint(Value value, bool initial_indent, int depth)
    {
        std::vector<Object*> objects_seen{};
        pprint(objects_seen, value, depth, "", initial_indent);
    }

    bool array_contains(Array* array, Value value)
    {
        Value* components = array->components();
        for (uint64_t i = 0; i < array->length; i++) {
            if (components[i] == value) {
                return true;
            }
        }
        return false;
    }

    bool array_contains_starting_at(Array* array, Value value, uint64_t start_index)
    {
        Value* components = array->components();
        for (uint64_t i = start_index; i < array->length; i++) {
            if (components[i] == value) {
                return true;
            }
        }
        return false;
    }

    /*
     * Python (writable pseudocode!) for C3 linearization:
     *
     * def c3_linearization(type: TypeValue) -> list[TypeValue]:
     *     # Calculate linearization, or None if not possible.
     *     def c3_merge(linearizations: list[list[TypeValue]]) -> Optional[list[TypeValue]]:
     *         # Should start with all nonempty linearizations.
     *         assert all(linearizations), linearizations
     *         merged = []
     *         while any(linearizations):
     *             head = None
     *             for lin in linearizations:
     *                 candidate = lin[0]
     *                 if all(candidate not in lin[1:] for lin in linearizations):
     *                     head = candidate
     *                     break
     *             if head:
     *                 merged.append(head)
     *                 for lin in linearizations:
     *                     if lin and lin[0] == head:
     *                         lin.remove(head)
     *                 linearizations = [lin for lin in linearizations if lin]
     *             else:
     *                 return None
     *         return merged
     *
     *     for base in type.bases:
     *         if type in base.linearization:
     *             raise ValueError(f"Inheritance cycle starting from {type}")
     *
     *     if type.bases:
     *         base_linearization = c3_merge(
     *             [list(base.linearization) for base in type.bases] + [list(type.bases)]
     *         )
     *         if base_linearization is None:
     *             raise ValueError(f"Could not determine linearization of {type}")
     *     else:
     *         base_linearization = []
     *
     *     return [type] + base_linearization
     */

    bool c3_merge(GC& gc, Root<Array>& r_linearizations, Root<Vector>& r_merged)
    {
        // Start at the left of each linearization.
        size_t spots[r_linearizations->length];
        for (size_t i = 0; i < r_linearizations->length; i++) {
            spots[i] = 0;
        }

        while (true) {
            // Find the first head possible from any linearization.
            // "candidate" = any linearization[current spot] value.
            // "head" = any candidate which for every linearization is _not_ in
            //     linearization[current spot + 1:] (using Pythonish syntax).
            bool candidates_remaining = false;
            std::optional<Value> head = std::nullopt;
            for (size_t i = 0; i < r_linearizations->length; i++) {
                Array* linearization = r_linearizations->components()[i].obj_array();
                if (spots[i] == linearization->length) {
                    // Already consumed this full linearization.
                    continue;
                }
                // There are still candidates to consider.
                candidates_remaining = true;

                Value candidate = linearization->components()[spots[i]];
                // Check if this candidate is a head.
                bool is_head = true;
                for (size_t j = 0; j < r_linearizations->length; j++) {
                    Array* other_linearization = r_linearizations->components()[j].obj_array();
                    if (array_contains_starting_at(other_linearization, candidate, spots[j] + 1)) {
                        // Not a head!
                        is_head = false;
                        break;
                    }
                }

                if (is_head) {
                    head = candidate;
                    break;
                }
            }

            if (!candidates_remaining) {
                // Successfully merged.
                return true;
            }

            if (head.has_value()) {
                Value v_head = *head;
                ValueRoot r_head(gc, std::move(v_head));
                append(gc, r_merged, r_head);
                v_head = *r_head;
                // Ratchet past this head as the candidate in any linearization.
                for (size_t i = 0; i < r_linearizations->length; i++) {
                    Array* linearization = r_linearizations->components()[i].obj_array();
                    if (spots[i] == linearization->length) {
                        // Already consumed this full linearization.
                        continue;
                    }
                    Value candidate = linearization->components()[spots[i]];
                    if (candidate == v_head) {
                        spots[i]++;
                    }
                }
            } else {
                // C3 linearization is not possible.
                return false;
            }
        }
    }

    Array* c3_linearization(GC& gc, Root<Type>& r_type)
    {
        Array* bases = r_type->v_bases.obj_array();
        for (uint64_t i = 0; i < bases->length; i++) {
            Type* base = bases->components()[i].obj_type();
            if (array_contains(base->v_linearization.obj_array(), r_type.value())) {
                // TODO: let katsu deal with it, and also provide r_type info.
                throw std::runtime_error("inheritance cycle starting from {type}");
            }
        }

        // Best guess for initial capacity. Doesn't have to be exact.
        Root<Vector> r_merged(gc, make_vector(gc, 1 + bases->length + 1));
        ValueRoot rv_type(gc, r_type.value());
        append(gc, r_merged, rv_type);

        bases = r_type->v_bases.obj_array();
        Root<Array> r_linearizations(gc, make_array_nofill(gc, bases->length + 1));
        Value* linearizations = r_linearizations->components();
        bases = r_type->v_bases.obj_array();
        for (uint64_t i = 0; i < bases->length; i++) {
            Type* base = bases->components()[i].obj_type();
            linearizations[i] = base->v_linearization;
        }
        linearizations[bases->length] = r_type->v_bases;

        bool merged = c3_merge(gc, r_linearizations, r_merged);
        if (!merged) {
            // TODO: let katsu deal with it, and also provide r_type info.
            throw std::runtime_error("could not determine linearization of {type}");
        }

        return vector_to_array(gc, r_merged);
    }

    Type* make_type(GC& gc, Root<String>& r_name, Root<Array>& r_bases, bool sealed,
                    Type::Kind kind, OptionalRoot<Vector>& r_slots)
    {
        // TODO: the r_linearization will just be thrown away later. Ideally don't even allocate it.
        Root<Array> r_init_linearization(gc, make_array(gc, /* length */ 0));
        Root<Vector> r_subtypes(gc, make_vector(gc, 0));
        Root<Type> r_type(gc,
                          make_type_raw(gc,
                                        r_name,
                                        r_bases,
                                        sealed,
                                        r_init_linearization,
                                        r_subtypes,
                                        kind,
                                        r_slots));

        Root<Array> r_linearization(gc, c3_linearization(gc, r_type));
        r_type->v_linearization = r_linearization.value();

        uint32_t linearization_length = r_linearization->length;
        // Ensure r_type is in the subtypes of each type in the linearization.
        // (Don't add r_type to its own bases, though. It's always the first in the linearization)
        for (uint32_t i = 1; i < linearization_length; i++) {
            Value v_base = r_linearization->components()[i];
            Type* base = v_base.obj_type();
            Root<Vector> r_base_subtypes(gc, base->v_subtypes.obj_vector());
            ValueRoot rv_type(gc, r_type.value());
            append(gc, r_base_subtypes, rv_type);
        }

        return *r_type;
    }

    // NOTE: when porting over try_set_bases(), make sure to also update supertypes' subtype
    // vectors...
};
