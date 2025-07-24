#include "builtin_ffi.h"

#include "builtin.h"
#include "condition.h"
#include "value_utils.h"

// libffi:
#include "ffi.h"

// Dynamic linking:
#include <dlfcn.h>

#include <errno.h>

namespace Katsu
{
    Value ffi__malloc_(VM& vm, int64_t nargs, Value* args)
    {
        // _ malloc: size
        ASSERT(nargs == 2);
        ASSERT(args[1].fixnum() >= 0);
        // TODO: check against size_t
        void* p = malloc(args[1].fixnum());
        if (!p) {
            throw condition_error("out-of-memory", "could not allocate requsted memory");
        }
        return Value::object(make_foreign(vm.gc, p));
    }
    Value ffi__malloc_aligned_(VM& vm, int64_t nargs, Value* args)
    {
        // _ malloc: size aligned: bytes
        ASSERT(nargs == 3);
        ASSERT(args[1].fixnum() >= 0);
        ASSERT(args[2].fixnum() >= 1);
        // TODO: check against size_t
        void* p = aligned_alloc(args[2].fixnum(), args[1].fixnum());
        if (!p) {
            throw condition_error("out-of-memory", "could not allocate requsted memory");
        }
        return Value::object(make_foreign(vm.gc, p));
    }
    Value ffi__free(VM& vm, int64_t nargs, Value* args)
    {
        // foreign free
        ASSERT(nargs == 1);
        void* p = args[0].obj_foreign()->value;
        ASSERT(p);
        free(p);
        return Value::null();
    }

    Value ffi__malloc_foreign_array_(VM& vm, int64_t nargs, Value* args)
    {
        // _ malloc-foreign-array: <vector-of-foreign>
        ASSERT(nargs == 2);
        Vector* src = args[1].obj_vector();
        for (Value entry : src) {
            if (!entry.is_obj_foreign()) {
                throw condition_error("invalid-argument", "array must contain Foreign entries");
            }
        }
        // TODO: check against size_t
        void** ps = reinterpret_cast<void**>(malloc(src->length * sizeof(void*)));
        if (!ps) {
            throw condition_error("out-of-memory", "could not allocate requsted memory");
        }
        // Fill!
        Array* src_arr = src->v_array.obj_array();
        for (size_t i = 0; i < src->length; i++) {
            ps[i] = src_arr->components()[i].obj_foreign()->value;
        }
        return Value::object(make_foreign(vm.gc, ps));
    }
    Value ffi__malloc_foreign_array_aligned_(VM& vm, int64_t nargs, Value* args)
    {
        // _ malloc-foreign-array: <vector-of-foreign> aligned: bytes
        ASSERT(nargs == 3);
        Vector* src = args[1].obj_vector();
        for (Value entry : src) {
            if (!entry.is_obj_foreign()) {
                throw condition_error("invalid-argument", "array must contain Foreign entries");
            }
        }
        ASSERT(args[2].fixnum() >= 1);
        // TODO: check against size_t
        void** ps =
            reinterpret_cast<void**>(aligned_alloc(args[2].fixnum(), src->length * sizeof(void*)));
        if (!ps) {
            throw condition_error("out-of-memory", "could not allocate requsted memory");
        }
        // Fill!
        Array* src_arr = src->v_array.obj_array();
        for (size_t i = 0; i < src->length; i++) {
            ps[i] = src_arr->components()[i].obj_foreign()->value;
        }
        return Value::object(make_foreign(vm.gc, ps));
    }

    Value ffi__ffi_prep_cif_abi_nargs_rtype_atypes(VM& vm, int64_t nargs, Value* args)
    {
        // _ ffi-prep-cif: cif abi: abi nargs: nargs rtype: rtype atypes: atypes
        ASSERT(nargs == 6);
        ffi_cif* cif = reinterpret_cast<ffi_cif*>(args[1].obj_foreign()->value);
        // TODO: range check
        ffi_abi abi = static_cast<ffi_abi>(args[2].fixnum());
        // TODO: range check
        unsigned int _nargs = static_cast<unsigned int>(args[3].fixnum());
        ffi_type* rtype = reinterpret_cast<ffi_type*>(args[4].obj_foreign()->value);
        ffi_type** atypes = reinterpret_cast<ffi_type**>(args[5].obj_foreign()->value);
        ffi_status result = ffi_prep_cif(cif, abi, _nargs, rtype, atypes);
        return Value::fixnum(static_cast<int64_t>(result));
    }

    Value ffi__ffi_call_fn_rvalue_avalues_(VM& vm, int64_t nargs, Value* args)
    {
        // _ ffi-call: cif fn: fn rvalue: rvalue avalues: avalues
        ASSERT(nargs == 5);
        ffi_cif* cif = reinterpret_cast<ffi_cif*>(args[1].obj_foreign()->value);
        void (*fn)() = reinterpret_cast<void (*)()>(args[2].obj_foreign()->value);
        void* rvalue = args[3].obj_foreign()->value;
        void** avalues = reinterpret_cast<void**>(args[4].obj_foreign()->value);
        ffi_call(cif, fn, rvalue, avalues);
        return Value::null();
    }

    template <typename T> T inline foreign_read_at_offset(ForeignValue* foreign, int64_t offset)
    {
        return *(T*)((uint8_t*)foreign->value + offset);
    }

    template <typename T>
    inline void foreign_write_at_offset(ForeignValue* foreign, int64_t offset, T value)
    {
        *(T*)((uint8_t*)foreign->value + offset) = value;
    }

    Value ffi__foreign_read_u8_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-u8-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<uint8_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_u8_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-u8-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<uint8_t>(args[0].obj_foreign(),
                                         args[1].fixnum(),
                                         (uint8_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_u16_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-u16-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<uint16_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_u16_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-u16-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<uint16_t>(args[0].obj_foreign(),
                                          args[1].fixnum(),
                                          (uint16_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_u32_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-u32-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<uint32_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_u32_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-u32-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<uint32_t>(args[0].obj_foreign(),
                                          args[1].fixnum(),
                                          (uint32_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_u64_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-u64-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        uint64_t read = foreign_read_at_offset<uint64_t>(args[0].obj_foreign(), args[1].fixnum());
        ASSERT(read <= INT64_MAX);
        return Value::fixnum((int64_t)read);
    }
    Value ffi__foreign_write_u64_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-u64-at-offset: offset value: u64
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        ASSERT(args[2].fixnum() >= 0);
        uint64_t write = (uint64_t)args[2].fixnum();
        foreign_write_at_offset(args[0].obj_foreign(), args[1].fixnum(), write);
        return Value::null();
    }


    Value ffi__foreign_read_s8_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-s8-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<int8_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_s8_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-s8-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<int8_t>(args[0].obj_foreign(),
                                        args[1].fixnum(),
                                        (int8_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_s16_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-s16-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<int16_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_s16_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-s16-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<int16_t>(args[0].obj_foreign(),
                                         args[1].fixnum(),
                                         (int16_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_s32_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-s32-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::fixnum(
            foreign_read_at_offset<int32_t>(args[0].obj_foreign(), args[1].fixnum()));
    }
    Value ffi__foreign_write_s32_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-s32-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        // TODO: check range
        foreign_write_at_offset<int32_t>(args[0].obj_foreign(),
                                         args[1].fixnum(),
                                         (int32_t)args[2].fixnum());
        return Value::null();
    }

    Value ffi__foreign_read_s64_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-s64-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        int64_t read = foreign_read_at_offset<int64_t>(args[0].obj_foreign(), args[1].fixnum());
        // TODO: check range
        return Value::fixnum(read);
    }
    Value ffi__foreign_write_s64_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-s64-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        ASSERT(args[2].fixnum() >= 0);
        int64_t write = (int64_t)args[2].fixnum();
        foreign_write_at_offset(args[0].obj_foreign(), args[1].fixnum(), write);
        return Value::null();
    }

    Value ffi__foreign_read_foreign_at_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-read-value-at-offset: offset
        ASSERT(nargs == 2);
        ASSERT(args[0].is_obj_foreign());
        return Value::object(
            make_foreign(vm.gc,
                         foreign_read_at_offset<void*>(args[0].obj_foreign(), args[1].fixnum())));
    }
    Value ffi__foreign_write_foreign_at_offset_value_(VM& vm, int64_t nargs, Value* args)
    {
        // obj foreign-write-value-at-offset: offset value: value
        ASSERT(nargs == 3);
        ASSERT(args[0].is_obj_foreign());
        ASSERT(args[2].is_obj_foreign());
        foreign_write_at_offset<void*>(args[0].obj_foreign(),
                                       args[1].fixnum(),
                                       args[2].obj_foreign()->value);
        return Value::null();
    }

    Value ffi__dlopen(VM& vm, int64_t nargs, Value* args)
    {
        // _ dlopen: filename flags: flags
        ASSERT(nargs == 3);
        return Value::object(
            make_foreign(vm.gc,
                         dlopen(reinterpret_cast<char*>(args[1].obj_foreign()->value),
                                // TODO: check range
                                static_cast<int>(args[2].fixnum()))));
    }
    Value ffi__dlclose(VM& vm, int64_t nargs, Value* args)
    {
        // handle dlclose
        ASSERT(nargs == 1);
        return Value::fixnum(dlclose(args[0].obj_foreign()->value));
    }
    Value ffi__dlerror(VM& vm, int64_t nargs, Value* args)
    {
        // _ dlerror
        ASSERT(nargs == 1);
        return Value::object(make_foreign(vm.gc, dlerror()));
    }
    Value ffi__dlsym(VM& vm, int64_t nargs, Value* args)
    {
        // handle dlsym: symbol
        ASSERT(nargs == 2);
        return Value::object(
            make_foreign(vm.gc,
                         dlsym(reinterpret_cast<char*>(args[0].obj_foreign()->value),
                               reinterpret_cast<char*>(args[1].obj_foreign()->value))));
    }

    Value ffi_errno(VM& vm, int64_t nargs, Value* args)
    {
        // _ errno
        ASSERT(nargs == 1);
        return Value::fixnum(errno);
    }

    Value ffi__c_string_to_string(VM& vm, int64_t nargs, Value* args)
    {
        // foreign c-string>string
        ASSERT(nargs == 1);
        const char* s = reinterpret_cast<const char*>(args[0].obj_foreign()->value);
        size_t len = strlen(s);
        String* str = make_string_nofill(vm.gc, len);
        memcpy(str->contents(), s, len);
        return Value::object(str);
    }

    // NOTE: caller must ensure that there is no GC activity while this is handed to foreign
    // functions.
    Value ffi__byte_array_to_foreign_offset_(VM& vm, int64_t nargs, Value* args)
    {
        // byte-array byte-array>foreign/offset: offset
        ASSERT(nargs == 2);
        Value v_foreign = Value::object(make_foreign(vm.gc, nullptr));
        v_foreign.obj_foreign()->value = args[0].obj_byte_array()->contents() + args[1].fixnum();
        return v_foreign;
    }

    void register_ffi_builtins(VM& vm, Root<Assoc>& r_ffi)
    {
        const std::function<Value()> matches_any = []() { return Value::null(); };
        const auto matches_type = [&vm](BuiltinId id) -> std::function<Value()> {
            return [&vm, id]() { return vm.builtin(id); };
        };
        const auto _register = [&vm, &r_ffi](const std::string& name,
                                             const std::vector<std::function<Value()>>& matchers,
                                             NativeHandler handler) -> void {
            Root<Array> r_matchers(vm.gc, make_array(vm.gc, matchers.size()));
            for (size_t i = 0; i < matchers.size(); i++) {
                r_matchers->components()[i] = matchers[i]();
            }
            add_native(vm.gc,
                       vm.v_multimethods,
                       true /* global */,
                       r_ffi,
                       name,
                       matchers.size(),
                       r_matchers,
                       handler);
        };
        const auto register_const = [&vm, &r_ffi](const std::string& name, Value value) -> void {
            ValueRoot r_name(vm.gc, Value::object(make_string(vm.gc, name)));
            ValueRoot r_value(vm.gc, std::move(value));
            append(vm.gc, r_ffi, r_name, r_value);
        };

        _register("malloc:", {matches_any, matches_type(_Fixnum)}, &ffi__malloc_);
        _register("malloc:aligned:",
                  {matches_any, matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__malloc_aligned_);
        _register("free", {matches_type(_Foreign)}, &ffi__free);

        _register("malloc-foreign-array:",
                  {matches_any, matches_type(_Vector)},
                  &ffi__malloc_foreign_array_);
        _register("malloc-foreign-array:aligned:",
                  {matches_any, matches_type(_Vector), matches_type(_Fixnum)},
                  &ffi__malloc_foreign_array_aligned_);

        register_const("sizeof-ffi_type", Value::fixnum(sizeof(ffi_type)));
        register_const("sizeof-ffi_cif", Value::fixnum(sizeof(ffi_cif)));
        register_const("sizeof-ffi_arg", Value::fixnum(sizeof(ffi_arg)));

        // TODO: how to make this more platform-independent? (not like anything about this language
        // is platform-independent right now...)
        // register_const("FFI_SYSV", Value::fixnum(FFI_SYSV));
        // register_const("FFI_STDCALL", Value::fixnum(FFI_STDCALL));
        // register_const("FFI_THISCALL", Value::fixnum(FFI_THISCALL));
        // register_const("FFI_FASTCALL", Value::fixnum(FFI_FASTCALL));
        // register_const("FFI_STDCALL", Value::fixnum(FFI_STDCALL));
        // register_const("FFI_PASCAL", Value::fixnum(FFI_PASCAL));
        // register_const("FFI_REGISTER", Value::fixnum(FFI_REGISTER));
        // register_const("FFI_MS_CDECL", Value::fixnum(FFI_MS_CDECL));
        register_const("ffi_abi.FFI_UNIX64", Value::fixnum(FFI_UNIX64));
        register_const("ffi_abi.FFI_WIN64", Value::fixnum(FFI_WIN64));
        register_const("ffi_abi.FFI_EFI64", Value::fixnum(FFI_EFI64));
        register_const("ffi_abi.FFI_GNUW64", Value::fixnum(FFI_GNUW64));
        register_const("ffi_abi.FFI_DEFAULT_ABI", Value::fixnum(FFI_DEFAULT_ABI));

        register_const("&ffi_type_void", Value::object(make_foreign(vm.gc, &ffi_type_void)));
        register_const("&ffi_type_uint8", Value::object(make_foreign(vm.gc, &ffi_type_uint8)));
        register_const("&ffi_type_sint8", Value::object(make_foreign(vm.gc, &ffi_type_sint8)));
        register_const("&ffi_type_uint16", Value::object(make_foreign(vm.gc, &ffi_type_uint16)));
        register_const("&ffi_type_sint16", Value::object(make_foreign(vm.gc, &ffi_type_sint16)));
        register_const("&ffi_type_uint32", Value::object(make_foreign(vm.gc, &ffi_type_uint32)));
        register_const("&ffi_type_sint32", Value::object(make_foreign(vm.gc, &ffi_type_sint32)));
        register_const("&ffi_type_uint64", Value::object(make_foreign(vm.gc, &ffi_type_uint64)));
        register_const("&ffi_type_sint64", Value::object(make_foreign(vm.gc, &ffi_type_sint64)));
        register_const("&ffi_type_float", Value::object(make_foreign(vm.gc, &ffi_type_float)));
        register_const("&ffi_type_double", Value::object(make_foreign(vm.gc, &ffi_type_double)));
        register_const("&ffi_type_uchar", Value::object(make_foreign(vm.gc, &ffi_type_uchar)));
        register_const("&ffi_type_schar", Value::object(make_foreign(vm.gc, &ffi_type_schar)));
        register_const("&ffi_type_ushort", Value::object(make_foreign(vm.gc, &ffi_type_ushort)));
        register_const("&ffi_type_sshort", Value::object(make_foreign(vm.gc, &ffi_type_sshort)));
        register_const("&ffi_type_uint", Value::object(make_foreign(vm.gc, &ffi_type_uint)));
        register_const("&ffi_type_sint", Value::object(make_foreign(vm.gc, &ffi_type_sint)));
        register_const("&ffi_type_ulong", Value::object(make_foreign(vm.gc, &ffi_type_ulong)));
        register_const("&ffi_type_slong", Value::object(make_foreign(vm.gc, &ffi_type_slong)));
        register_const("&ffi_type_longdouble",
                       Value::object(make_foreign(vm.gc, &ffi_type_longdouble)));
        register_const("&ffi_type_pointer", Value::object(make_foreign(vm.gc, &ffi_type_pointer)));
        register_const("&ffi_type_complex_float",
                       Value::object(make_foreign(vm.gc, &ffi_type_complex_float)));
        register_const("&ffi_type_complex_double",
                       Value::object(make_foreign(vm.gc, &ffi_type_complex_double)));
        register_const("&ffi_type_complex_longdouble",
                       Value::object(make_foreign(vm.gc, &ffi_type_complex_longdouble)));

        register_const("ffi_status.FFI_OK", Value::fixnum(FFI_OK));
        register_const("ffi_status.FFI_BAD_TYPEDEF", Value::fixnum(FFI_BAD_TYPEDEF));
        register_const("ffi_status.FFI_BAD_ABI", Value::fixnum(FFI_BAD_ABI));
        register_const("ffi_status.FFI_BAD_ARGTYPE", Value::fixnum(FFI_BAD_ARGTYPE));

        _register("ffi-prep-cif:abi:nargs:rtype:atypes:",
                  {
                      matches_any,
                      matches_type(_Foreign) /* cif */,
                      matches_type(_Fixnum) /* abi */,
                      matches_type(_Fixnum) /* nargs */,
                      matches_type(_Foreign) /* rtype */,
                      matches_type(_Foreign) /* atypes */,
                  },
                  ffi__ffi_prep_cif_abi_nargs_rtype_atypes);

        _register("ffi-call:fn:rvalue:avalues:",
                  {
                      {
                       matches_any, matches_type(_Foreign) /* cif */,
                       matches_type(_Foreign) /* fn */,
                       matches_type(_Foreign) /* rvalue */,
                       matches_type(_Foreign) /* avalues */,
                       }
        },
                  &ffi__ffi_call_fn_rvalue_avalues_);

        _register("foreign-read-u8-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_u8_at_offset_);
        _register("foreign-write-u8-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_u8_at_offset_value_);
        _register("foreign-read-u32-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_u32_at_offset_);
        _register("foreign-write-u32-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_u32_at_offset_value_);
        _register("foreign-read-u64-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_u64_at_offset_);
        _register("foreign-write-u64-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_u64_at_offset_value_);
        _register("foreign-read-s8-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_s8_at_offset_);
        _register("foreign-write-s8-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_s8_at_offset_value_);
        _register("foreign-read-s32-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_s32_at_offset_);
        _register("foreign-write-s32-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_s32_at_offset_value_);
        _register("foreign-read-s64-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_s64_at_offset_);
        _register("foreign-write-s64-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_type(_Fixnum)},
                  &ffi__foreign_write_s64_at_offset_value_);
        _register("foreign-read-foreign-at-offset:",
                  {matches_type(_Foreign), matches_type(_Fixnum)},
                  &ffi__foreign_read_foreign_at_offset_);
        _register("foreign-write-foreign-at-offset:value:",
                  {matches_type(_Foreign), matches_type(_Fixnum), matches_any},
                  &ffi__foreign_write_foreign_at_offset_value_);

        // TODO: look into the raw API for libffi (seems like it just lifts primitives to ffi_call
        // args, so args don't all need an extra void* allocation.
        // TODO: allow making a larger variety of `ffi_type`s.

        register_const("NULL", Value::object(make_foreign(vm.gc, nullptr)));
        _register(
            "dlopen:flags:",
            {matches_any, matches_type(_Foreign) /* filename */, matches_type(_Fixnum) /* flags */},
            &ffi__dlopen);
        _register("dlclose", {matches_type(_Foreign) /* handle */}, &ffi__dlclose);
        _register("dlerror", {matches_any}, &ffi__dlerror);
        register_const("RTLD_LAZY", Value::fixnum(RTLD_LAZY));
        register_const("RTLD_NOW", Value::fixnum(RTLD_NOW));
        register_const("RTLD_GLOBAL", Value::fixnum(RTLD_GLOBAL));
        register_const("RTLD_LOCAL", Value::fixnum(RTLD_LOCAL));
        register_const("RTLD_NODELETE", Value::fixnum(RTLD_NODELETE));
        register_const("RTLD_NOLOAD", Value::fixnum(RTLD_NOLOAD));
        register_const("RTLD_DEEPBIND", Value::fixnum(RTLD_DEEPBIND));

        _register("dlsym:",
                  {matches_type(_Foreign) /* handle */, matches_type(_Foreign) /* symbol */},
                  &ffi__dlsym);

        _register("errno", {matches_any}, &ffi_errno);
        // Define each error number: retrieved via `errno -l | cut -d' ' -f1`.
        // TODO: make this more system independent.
        register_const("EPERM", Value::fixnum(EPERM));
        register_const("ENOENT", Value::fixnum(ENOENT));
        register_const("ESRCH", Value::fixnum(ESRCH));
        register_const("EINTR", Value::fixnum(EINTR));
        register_const("EIO", Value::fixnum(EIO));
        register_const("ENXIO", Value::fixnum(ENXIO));
        register_const("E2BIG", Value::fixnum(E2BIG));
        register_const("ENOEXEC", Value::fixnum(ENOEXEC));
        register_const("EBADF", Value::fixnum(EBADF));
        register_const("ECHILD", Value::fixnum(ECHILD));
        register_const("EAGAIN", Value::fixnum(EAGAIN));
        register_const("ENOMEM", Value::fixnum(ENOMEM));
        register_const("EACCES", Value::fixnum(EACCES));
        register_const("EFAULT", Value::fixnum(EFAULT));
        register_const("ENOTBLK", Value::fixnum(ENOTBLK));
        register_const("EBUSY", Value::fixnum(EBUSY));
        register_const("EEXIST", Value::fixnum(EEXIST));
        register_const("EXDEV", Value::fixnum(EXDEV));
        register_const("ENODEV", Value::fixnum(ENODEV));
        register_const("ENOTDIR", Value::fixnum(ENOTDIR));
        register_const("EISDIR", Value::fixnum(EISDIR));
        register_const("EINVAL", Value::fixnum(EINVAL));
        register_const("ENFILE", Value::fixnum(ENFILE));
        register_const("EMFILE", Value::fixnum(EMFILE));
        register_const("ENOTTY", Value::fixnum(ENOTTY));
        register_const("ETXTBSY", Value::fixnum(ETXTBSY));
        register_const("EFBIG", Value::fixnum(EFBIG));
        register_const("ENOSPC", Value::fixnum(ENOSPC));
        register_const("ESPIPE", Value::fixnum(ESPIPE));
        register_const("EROFS", Value::fixnum(EROFS));
        register_const("EMLINK", Value::fixnum(EMLINK));
        register_const("EPIPE", Value::fixnum(EPIPE));
        register_const("EDOM", Value::fixnum(EDOM));
        register_const("ERANGE", Value::fixnum(ERANGE));
        register_const("EDEADLK", Value::fixnum(EDEADLK));
        register_const("ENAMETOOLONG", Value::fixnum(ENAMETOOLONG));
        register_const("ENOLCK", Value::fixnum(ENOLCK));
        register_const("ENOSYS", Value::fixnum(ENOSYS));
        register_const("ENOTEMPTY", Value::fixnum(ENOTEMPTY));
        register_const("ELOOP", Value::fixnum(ELOOP));
        register_const("EWOULDBLOCK", Value::fixnum(EWOULDBLOCK));
        register_const("ENOMSG", Value::fixnum(ENOMSG));
        register_const("EIDRM", Value::fixnum(EIDRM));
        register_const("ECHRNG", Value::fixnum(ECHRNG));
        register_const("EL2NSYNC", Value::fixnum(EL2NSYNC));
        register_const("EL3HLT", Value::fixnum(EL3HLT));
        register_const("EL3RST", Value::fixnum(EL3RST));
        register_const("ELNRNG", Value::fixnum(ELNRNG));
        register_const("EUNATCH", Value::fixnum(EUNATCH));
        register_const("ENOCSI", Value::fixnum(ENOCSI));
        register_const("EL2HLT", Value::fixnum(EL2HLT));
        register_const("EBADE", Value::fixnum(EBADE));
        register_const("EBADR", Value::fixnum(EBADR));
        register_const("EXFULL", Value::fixnum(EXFULL));
        register_const("ENOANO", Value::fixnum(ENOANO));
        register_const("EBADRQC", Value::fixnum(EBADRQC));
        register_const("EBADSLT", Value::fixnum(EBADSLT));
        register_const("EDEADLOCK", Value::fixnum(EDEADLOCK));
        register_const("EBFONT", Value::fixnum(EBFONT));
        register_const("ENOSTR", Value::fixnum(ENOSTR));
        register_const("ENODATA", Value::fixnum(ENODATA));
        register_const("ETIME", Value::fixnum(ETIME));
        register_const("ENOSR", Value::fixnum(ENOSR));
        register_const("ENONET", Value::fixnum(ENONET));
        register_const("ENOPKG", Value::fixnum(ENOPKG));
        register_const("EREMOTE", Value::fixnum(EREMOTE));
        register_const("ENOLINK", Value::fixnum(ENOLINK));
        register_const("EADV", Value::fixnum(EADV));
        register_const("ESRMNT", Value::fixnum(ESRMNT));
        register_const("ECOMM", Value::fixnum(ECOMM));
        register_const("EPROTO", Value::fixnum(EPROTO));
        register_const("EMULTIHOP", Value::fixnum(EMULTIHOP));
        register_const("EDOTDOT", Value::fixnum(EDOTDOT));
        register_const("EBADMSG", Value::fixnum(EBADMSG));
        register_const("EOVERFLOW", Value::fixnum(EOVERFLOW));
        register_const("ENOTUNIQ", Value::fixnum(ENOTUNIQ));
        register_const("EBADFD", Value::fixnum(EBADFD));
        register_const("EREMCHG", Value::fixnum(EREMCHG));
        register_const("ELIBACC", Value::fixnum(ELIBACC));
        register_const("ELIBBAD", Value::fixnum(ELIBBAD));
        register_const("ELIBSCN", Value::fixnum(ELIBSCN));
        register_const("ELIBMAX", Value::fixnum(ELIBMAX));
        register_const("ELIBEXEC", Value::fixnum(ELIBEXEC));
        register_const("EILSEQ", Value::fixnum(EILSEQ));
        register_const("ERESTART", Value::fixnum(ERESTART));
        register_const("ESTRPIPE", Value::fixnum(ESTRPIPE));
        register_const("EUSERS", Value::fixnum(EUSERS));
        register_const("ENOTSOCK", Value::fixnum(ENOTSOCK));
        register_const("EDESTADDRREQ", Value::fixnum(EDESTADDRREQ));
        register_const("EMSGSIZE", Value::fixnum(EMSGSIZE));
        register_const("EPROTOTYPE", Value::fixnum(EPROTOTYPE));
        register_const("ENOPROTOOPT", Value::fixnum(ENOPROTOOPT));
        register_const("EPROTONOSUPPORT", Value::fixnum(EPROTONOSUPPORT));
        register_const("ESOCKTNOSUPPORT", Value::fixnum(ESOCKTNOSUPPORT));
        register_const("EOPNOTSUPP", Value::fixnum(EOPNOTSUPP));
        register_const("EPFNOSUPPORT", Value::fixnum(EPFNOSUPPORT));
        register_const("EAFNOSUPPORT", Value::fixnum(EAFNOSUPPORT));
        register_const("EADDRINUSE", Value::fixnum(EADDRINUSE));
        register_const("EADDRNOTAVAIL", Value::fixnum(EADDRNOTAVAIL));
        register_const("ENETDOWN", Value::fixnum(ENETDOWN));
        register_const("ENETUNREACH", Value::fixnum(ENETUNREACH));
        register_const("ENETRESET", Value::fixnum(ENETRESET));
        register_const("ECONNABORTED", Value::fixnum(ECONNABORTED));
        register_const("ECONNRESET", Value::fixnum(ECONNRESET));
        register_const("ENOBUFS", Value::fixnum(ENOBUFS));
        register_const("EISCONN", Value::fixnum(EISCONN));
        register_const("ENOTCONN", Value::fixnum(ENOTCONN));
        register_const("ESHUTDOWN", Value::fixnum(ESHUTDOWN));
        register_const("ETOOMANYREFS", Value::fixnum(ETOOMANYREFS));
        register_const("ETIMEDOUT", Value::fixnum(ETIMEDOUT));
        register_const("ECONNREFUSED", Value::fixnum(ECONNREFUSED));
        register_const("EHOSTDOWN", Value::fixnum(EHOSTDOWN));
        register_const("EHOSTUNREACH", Value::fixnum(EHOSTUNREACH));
        register_const("EALREADY", Value::fixnum(EALREADY));
        register_const("EINPROGRESS", Value::fixnum(EINPROGRESS));
        register_const("ESTALE", Value::fixnum(ESTALE));
        register_const("EUCLEAN", Value::fixnum(EUCLEAN));
        register_const("ENOTNAM", Value::fixnum(ENOTNAM));
        register_const("ENAVAIL", Value::fixnum(ENAVAIL));
        register_const("EISNAM", Value::fixnum(EISNAM));
        register_const("EREMOTEIO", Value::fixnum(EREMOTEIO));
        register_const("EDQUOT", Value::fixnum(EDQUOT));
        register_const("ENOMEDIUM", Value::fixnum(ENOMEDIUM));
        register_const("EMEDIUMTYPE", Value::fixnum(EMEDIUMTYPE));
        register_const("ECANCELED", Value::fixnum(ECANCELED));
        register_const("ENOKEY", Value::fixnum(ENOKEY));
        register_const("EKEYEXPIRED", Value::fixnum(EKEYEXPIRED));
        register_const("EKEYREVOKED", Value::fixnum(EKEYREVOKED));
        register_const("EKEYREJECTED", Value::fixnum(EKEYREJECTED));
        register_const("EOWNERDEAD", Value::fixnum(EOWNERDEAD));
        register_const("ENOTRECOVERABLE", Value::fixnum(ENOTRECOVERABLE));
        register_const("ERFKILL", Value::fixnum(ERFKILL));
        register_const("EHWPOISON", Value::fixnum(EHWPOISON));
        register_const("ENOTSUP", Value::fixnum(ENOTSUP));

        // TODO: c-string to byte-array?
        _register("c-string>string", {matches_type(_Foreign)}, &ffi__c_string_to_string);
        _register("byte-array>foreign/offset:",
                  {matches_type(_ByteArray), matches_type(_Fixnum)},
                  &ffi__byte_array_to_foreign_offset_);
    }
};
