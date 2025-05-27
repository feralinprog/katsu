#pragma once

#include "value.h"
#include <functional>
#include <vector>

// Enable logging from the GC.
// Default off.
#ifndef DEBUG_GC_LOG
#define DEBUG_GC_LOG (0)
#endif
// Have the GC fill all new allocations with a fixed byte pattern.
// Default on.
#ifndef DEBUG_GC_FILL
#define DEBUG_GC_FILL (1)
#endif
// Have the GC perform a collection on every allocation. This is incredibly slow but quickly finds
// bugs where consumers forgot to add a GC root.
// Default off.
#ifndef DEBUG_GC_COLLECT_EVERY_ALLOC
#define DEBUG_GC_COLLECT_EVERY_ALLOC (0)
#endif
// Have the GC allocate a new semispace to migrate all live objects to, on each collection. Usual
// behavior is to cycle between two semispaces. This can also help finding bugs where consumers
// forgot to add a GC root, particularly with ASAN enabled, as any pointers to dead GC objects will
// eventually (or nearly immediately, if DEBUG_GC_COLLECT_EVERY_ALLOC is also enabled!) point to
// `free`d memory.
// Default off.
#ifndef DEBUG_GC_NEW_SEMISPACE
#define DEBUG_GC_NEW_SEMISPACE (0)
#endif

#if DEBUG_GC_LOG
#include <iostream>
#endif

namespace Katsu
{
    inline uint64_t align_up(uint64_t x, size_t alignment_bits)
    {
        const uint64_t mask_n = (1ll << alignment_bits) - 1;
        return (x + mask_n) & ~mask_n;
    }

    class RootProvider
    {
    public:
        virtual void visit_roots(std::function<void(Value*)>& visitor) = 0;
    };

    class GC
    {
    public:
        // Create a GC managing a region of `size` bytes. The size must be TAG_BITS-aligned.
        GC(uint64_t size);

        ~GC();

        // Note: prefer the make_*() functions instead.
        // T must be a subtype of Object.
        // The (variadic) arguments are directly passed to T::size(...).
        // For instance, alloc<Vector>(n) would allocate a vector of capacity n (and size being
        // sizeof(Vector) + n * sizeof(Value)).
        template <typename T, typename... S> T* alloc(S... size_args)
        {
            static_assert(!std::is_same_v<Object, T> && std::is_base_of_v<Object, T>);
            uint64_t size = T::size(size_args...);
            auto obj = reinterpret_cast<Object*>(this->_alloc_raw(size));
            obj->set_object(T::CLASS_TAG);
            return reinterpret_cast<T*>(obj);
        }

        // Allocate a region of `size` bytes and return a pointer to the first byte.
        // This may garbage-collect in order to free up space.
        // If DEBUG_GC_FILL is enabled, this furthermore fills the newly allocated region with a
        // repeating 0xFEEDBEEF pattern. Throws on allocation failure. Implemented here to allow
        // inlining the happy path.
        void* _alloc_raw(uint64_t size)
        {
#if DEBUG_GC_LOG
            std::cout << "GC: allocating size=" << size;
#endif
            size = align_up(size, TAG_BITS);
#if DEBUG_GC_LOG
            std::cout << " aligned=" << size << "\n";
#endif

            if (size > this->size) [[unlikely]] {
                throw std::bad_alloc();
            }

            uint64_t remaining = this->size - this->spot;

#if DEBUG_GC_COLLECT_EVERY_ALLOC
            {
#else
            if (size > remaining) [[unlikely]] {
#endif
                this->collect();

                remaining = this->size - this->spot;
                if (size > remaining) {
                    // No room even after collection -- we're really out of memory!
                    throw std::bad_alloc();
                }
            }

            uint64_t spot = this->spot;
            this->spot += size;
            auto allocation = &this->mem[spot];
#if DEBUG_GC_LOG
            std::cout << "GC: allocated @" << reinterpret_cast<void*>(allocation) << "\n";
#endif
#if DEBUG_GC_FILL
            for (uint32_t* w = reinterpret_cast<uint32_t*>(&this->mem[spot]);
                 w < reinterpret_cast<uint32_t*>(&this->mem[spot + size]);
                 w++) {
                *w = 0xFEEDBEEF;
            }
#endif
            return allocation;
        }

        std::vector<RootProvider*> root_providers;
        // (Pointers to) object values indicating any GC roots, i.e. entry points to the graph
        // of live objects. This is intended more for ephemeral extra roots that are not covered
        // already by the root_providers.
        std::vector<Value*> roots;

    private:
        void collect();

        // Core array of values.
        uint8_t* mem;
        uint64_t size;

        // Backup array of values to be used during semispace copying collection.
        uint8_t* mem_opp;

        // Next allocation location.
        uint64_t spot;

        friend void TESTONLY_collect(GC& gc);
        friend uint8_t* TESTONLY_get_mem(GC& gc);
    };

    class ValueRoot
    {
    public:
        ValueRoot(GC& _gc, Value&& value)
            : gc(&_gc)
            , root(value)
        {
            gc->roots.push_back(&this->root);
            value = Value::null();
        }

        ValueRoot(ValueRoot&) = delete;

        ValueRoot(ValueRoot&& from)
            : gc(from.gc)
            , root(from.root)
        {
            from.gc = nullptr;
            from.root = Value::null();
        }

        ~ValueRoot()
        {
            if (this->gc) {
                this->gc->roots.pop_back();
            }
        }

        inline Value& operator*()
        {
            return this->root;
        }

        inline Value* operator->()
        {
            return &this->root;
        }

    private:
        GC* gc;
        Value root;
    };

    template <typename T> class Root
    {
        static_assert(!std::is_same_v<Object, T> && std::is_base_of_v<Object, T>);

    public:
        Root(GC& _gc, T*&& value)
            : gc(&_gc)
            , root(Value::object(value))
        {
            if (!value) {
                throw std::logic_error("attempted to create Root from nullptr");
            }
            gc->roots.push_back(&this->root);
            value = nullptr;
        }

        Root(Root<T>&) = delete;

        Root(Root<T>&& from)
            : gc(from.gc)
            , root(from.root)
        {
            from.gc = nullptr;
            from.root = Value::null();
        }

        ~Root()
        {
            if (this->gc) {
                this->gc->roots.pop_back();
            }
        }

        inline Value value()
        {
            return this->root;
        }

        inline T* operator*()
        {
            return this->root.template value<Object*>()->template object<T*>();
        }

        inline T* operator->()
        {
            return this->root.template value<Object*>()->template object<T*>();
        }

    private:
        GC* gc;
        Value root;
    };

    template <typename T> class OptionalRoot
    {
        static_assert(!std::is_same_v<Object, T> && std::is_base_of_v<Object, T>);

    public:
        OptionalRoot(GC& _gc, T*&& value)
            : gc(&_gc)
            , root(value ? Value::object(value) : Value::null())
        {
            gc->roots.push_back(&this->root);
            value = nullptr;
        }

        OptionalRoot(OptionalRoot<T>&) = delete;

        OptionalRoot(OptionalRoot<T>&& from)
            : gc(from.gc)
            , root(from.root)
        {
            from.gc = nullptr;
            from.root = Value::null();
        }

        ~OptionalRoot()
        {
            if (this->gc) {
                this->gc->roots.pop_back();
            }
        }

        inline Value value()
        {
            return this->root;
        }

        inline operator bool()
        {
            return !this->root.is_null();
        }

        inline T* operator*()
        {
            return (bool)this ? this->root.template value<Object*>()->template object<T*>()
                              : nullptr;
        }

        inline T* operator->()
        {
            if (!(bool)this) {
                throw std::logic_error("dereferencing null OptionalRoot!");
            }
            return this->root.template value<Object*>()->template object<T*>();
        }

    private:
        GC* gc;
        Value root;
    };
};
