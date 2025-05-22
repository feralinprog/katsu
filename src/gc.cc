#include "gc.h"

#include <cstring>
#include <map>

namespace Katsu
{
    GC::GC(uint64_t size)
        : root_providers{}
        , roots{}
        , mem(nullptr)
        , size(0)
        , mem_opp(nullptr)
        , spot(0)
    {
        if ((size & TAG_MASK) != 0) {
            throw std::invalid_argument("size must be TAG_BITS-aligned");
        }

        this->mem = reinterpret_cast<uint8_t*>(aligned_alloc(1 << TAG_BITS, size));
        if (!this->mem) {
            throw std::bad_alloc();
        }
        this->size = size;

        this->mem_opp = reinterpret_cast<uint8_t*>(aligned_alloc(1 << TAG_BITS, size));
        if (!this->mem_opp) {
            throw std::bad_alloc();
        }
    }

    GC::~GC()
    {
        if (this->mem) {
            free(this->mem);
        }
        if (this->mem_opp) {
            free(this->mem_opp);
        }
    }

    void GC::collect()
    {
        uint8_t* to = this->mem_opp;

#if DEBUG_GC_LOG
        std::cout << "GC: collecting...\n";
        std::cout << "GC: from=" << reinterpret_cast<void*>(this->mem) << "\n";
        std::cout << "GC:   to=" << reinterpret_cast<void*>(this->mem_opp) << "\n";
#endif

        // Input *node must be an object reference (Tag::OBJECT).
        const auto move_obj = [&to](Value* node) -> void {
            auto obj = node->object();
            // Follow the existing forwarding pointer if it exists; otherwise copy the object
            // and install a forwarding pointer.
#if DEBUG_GC_LOG
            std::cout << "GC: moving object @" << obj << " (from node @" << node << ")";
            if (obj->is_forwarding()) {
                std::cout << ", fwd to " << obj->forwarding() << "\n";
            } else {
                std::cout << ", tag=" << object_tag_str(obj->tag()) << "\n";
            }
#endif
            if (!obj->is_forwarding()) {
                uint64_t obj_size;
                switch (obj->tag()) {
                    case ObjectTag::REF: {
                        auto v = obj->object<Ref*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::TUPLE: {
                        auto v = obj->object<Tuple*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::VECTOR: {
                        auto v = obj->object<Vector*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::MODULE: {
                        auto v = obj->object<Module*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::STRING: {
                        auto v = obj->object<String*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::CODE: {
                        auto v = obj->object<Code*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::CLOSURE: {
                        auto v = obj->object<Closure*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::METHOD: {
                        auto v = obj->object<Method*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::MULTIMETHOD: {
                        auto v = obj->object<MultiMethod*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::TYPE: {
                        auto v = obj->object<Type*>();
                        obj_size = v->size();
                        break;
                    }
                    case ObjectTag::INSTANCE: {
                        auto v = obj->object<DataclassInstance*>();
                        obj_size = v->size();
                        break;
                    }
                    default: [[unlikely]] throw std::logic_error("missed an object tag?");
                }
#if DEBUG_GC_LOG
                std::cout << "GC: copying obj size=" << obj_size << "(0x" << std::hex << obj_size
                          << std::dec << ") from " << obj << " to " << reinterpret_cast<void*>(to)
                          << "\n";
#endif
                // TODO: don't necessarily need to memcpy the whole obj_size... for instance vectors
                // may have length far lower than capacity.
                memcpy(to, obj, obj_size);
                obj->set_forwarding(to);
                to += align_up(obj_size, TAG_BITS);
#if DEBUG_GC_LOG
                std::cout << "GC: new to=" << reinterpret_cast<void*>(to) << "\n";
#endif
            }
#if DEBUG_GC_LOG
            std::cout << "GC: setting node @" << node << " to forwarded obj @" << obj->forwarding()
                      << "\n";
#endif
            *node = Value::object(reinterpret_cast<Object*>(obj->forwarding()));
        };

        const auto move_value = [&move_obj](Value* node) {
#if DEBUG_GC_LOG
            std::cout << "GC: moving value @" << node << ", tag=" << tag_str(node->tag())
                      << ", raw=0x" << std::hex << node->raw_value() << std::dec << "\n";
#endif
            if (node->tag() == Tag::OBJECT) {
                move_obj(node);
            } else if (!node->is_inline()) [[unlikely]] {
                throw std::logic_error("can only move object reference or inline value");
            }
        };

        const auto add_root = [&move_obj](Value* root) {
#if DEBUG_GC_LOG
            std::cout << "GC: adding root @" << root << "\n";
#endif
            if (!root->is_inline()) {
                move_obj(root);
            }
        };

        std::function<void(Value*)> add_root_fn = add_root;
        for (RootProvider* provider : this->root_providers) {
            provider->visit_roots(add_root_fn);
        }

        for (Value* root : this->roots) {
            // There shouldn't be any inline roots, but we don't really guard against it.
            add_root(root);
        }

        uint8_t* queue = this->mem_opp;
        while (queue < to) {
            auto obj = reinterpret_cast<Object*>(queue);
#if DEBUG_GC_LOG
            std::cout << "GC: scanning object @" << obj << ", header=0x" << std::hex
                      << obj->raw_header() << std::dec;
            std::cout << ", tag=" << object_tag_str(obj->tag()) << "\n";
#endif
            uint64_t obj_size;
            switch (obj->tag()) {
                case ObjectTag::REF: {
                    auto v = obj->object<Ref*>();
                    move_value(&v->v_ref);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::TUPLE: {
                    auto v = obj->object<Tuple*>();
                    uint64_t length = v->length;
                    for (uint64_t i = 0; i < length; i++) {
                        move_value(&v->components()[i]);
                    }
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::VECTOR: {
                    auto v = obj->object<Vector*>();
                    uint64_t length = v->length;
                    for (uint64_t i = 0; i < length; i++) {
                        move_value(&v->components()[i]);
                    }
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::MODULE: {
                    auto v = obj->object<Module*>();
                    move_value(&v->v_base);
                    uint64_t length = v->length;
                    for (uint64_t i = 0; i < length; i++) {
                        Module::Entry& entry = v->entries()[i];
                        move_value(&entry.v_key);
                        move_value(&entry.v_value);
                    }
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::STRING: {
                    // No internal values to move.
                    auto v = obj->object<String*>();
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::CODE: {
                    auto v = obj->object<Code*>();
                    move_value(&v->v_module);
                    move_value(&v->v_upreg_map);
                    move_value(&v->v_insts);
                    move_value(&v->v_args);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::CLOSURE: {
                    auto v = obj->object<Closure*>();
                    move_value(&v->v_code);
                    move_value(&v->v_upregs);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::METHOD: {
                    auto v = obj->object<Method*>();
                    move_value(&v->v_param_matchers);
                    move_value(&v->v_return_type);
                    move_value(&v->v_code);
                    move_value(&v->v_attributes);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::MULTIMETHOD: {
                    auto v = obj->object<MultiMethod*>();
                    move_value(&v->v_name);
                    move_value(&v->v_methods);
                    move_value(&v->v_attributes);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::TYPE: {
                    auto v = obj->object<Type*>();
                    move_value(&v->v_name);
                    move_value(&v->v_bases);
                    move_value(&v->v_linearization);
                    move_value(&v->v_subtypes);
                    move_value(&v->v_slots);
                    obj_size = v->size();
                    break;
                }
                case ObjectTag::INSTANCE: {
                    auto v = obj->object<DataclassInstance*>();
                    // TODO: deduplicate slot-count lookup.
                    auto type = v->v_type.obj_type();
                    auto type_slots = type->v_slots.obj_vector();
                    uint64_t num_slots = type_slots->length;
                    for (uint64_t i = 0; i < num_slots; i++) {
                        move_value(&v->slots()[i]);
                    }
                    obj_size = v->size();
                    break;
                }
                default: throw std::logic_error("missed an object tag?");
            }
            queue += align_up(obj_size, TAG_BITS);
        }

        // We've copied all objects from `mem` to `mem_opp`. Now swap spaces so `mem` is the primary
        // again.
        std::swap(this->mem, this->mem_opp);
#if DEBUG_GC_NEW_SEMISPACE
        uint8_t* old_mem_opp = this->mem_opp;
        this->mem_opp = reinterpret_cast<uint8_t*>(aligned_alloc(1 << TAG_BITS, size));
        if (!this->mem_opp) {
            throw std::bad_alloc();
        }
        free(old_mem_opp);
#endif
        this->spot = queue - this->mem;
#if DEBUG_GC_LOG
        std::cout << "GC: finished collection - mem " << reinterpret_cast<void*>(this->mem)
                  << ", usage " << this->spot << "(0x" << std::hex << this->spot << std::dec
                  << ")\n";
#endif
    }
};
