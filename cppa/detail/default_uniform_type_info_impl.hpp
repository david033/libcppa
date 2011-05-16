#ifndef DEFAULT_UNIFORM_TYPE_INFO_IMPL_HPP
#define DEFAULT_UNIFORM_TYPE_INFO_IMPL_HPP

#include "cppa/any_type.hpp"
#include "cppa/util/void_type.hpp"

#include "cppa/util/disjunction.hpp"
#include "cppa/util/is_iterable.hpp"
#include "cppa/util/is_primitive.hpp"
#include "cppa/util/is_forward_iterator.hpp"
#include "cppa/util/abstract_uniform_type_info.hpp"

#include "cppa/detail/map_member.hpp"
#include "cppa/detail/list_member.hpp"
#include "cppa/detail/primitive_member.hpp"

namespace cppa { namespace detail {

template<typename T>
class is_stl_compliant_list
{

    template<class C>
    static bool cmp_help_fun
    (
        // mutable pointer
        C* mc,
        // check if there's a 'void push_back()' that takes C::value_type
        decltype(mc->push_back(typename C::value_type()))*                  = 0
    )
    {
        return true;
    }

    // SFNINAE default
    static void cmp_help_fun(void*) { }

    typedef decltype(cmp_help_fun(static_cast<T*>(nullptr))) result_type;

 public:

    static const bool value =    util::is_iterable<T>::value
                              && std::is_same<bool, result_type>::value;

};

template<typename T>
class is_stl_compliant_map
{

    template<class C>
    static bool cmp_help_fun
    (
        // mutable pointer
        C* mc,
        // check if there's an 'insert()' that takes C::value_type
        decltype(mc->insert(typename C::value_type()))* = nullptr
    )
    {
        return true;
    }

    static void cmp_help_fun(...) { }

    typedef decltype(cmp_help_fun(static_cast<T*>(nullptr))) result_type;

 public:

    static const bool value =    util::is_iterable<T>::value
                              && std::is_same<bool, result_type>::value;

};

template<typename T>
struct has_default_uniform_type_info_impl
{
    static const bool value =    util::is_primitive<T>::value
                              || is_stl_compliant_map<T>::value
                              || is_stl_compliant_list<T>::value;
};

template<typename T>
class default_uniform_type_info_impl : public util::abstract_uniform_type_info<T>
{

    template<typename X>
    struct is_invalid
    {
        static const bool value =    !util::is_primitive<X>::value
                                  && !is_stl_compliant_map<X>::value
                                  && !is_stl_compliant_list<X>::value;
    };

    class member
    {

        uniform_type_info* m_meta;

        std::function<void (const uniform_type_info*,
                            const void*,
                            serializer*      )> m_serialize;

        std::function<void (const uniform_type_info*,
                            void*,
                            deserializer*    )> m_deserialize;

        member(const member&) = delete;
        member& operator=(const member&) = delete;

        void swap(member& other)
        {
            std::swap(m_meta, other.m_meta);
            std::swap(m_serialize, other.m_serialize);
            std::swap(m_deserialize, other.m_deserialize);
        }

        template<typename S, class D>
        member(uniform_type_info* mtptr, S&& s, D&& d)
            : m_meta(mtptr)
            , m_serialize(std::forward<S>(s))
            , m_deserialize(std::forward<D>(d))
        {
        }

     public:

        template<typename R, class C>
        member(uniform_type_info* mtptr, R C::*mem_ptr) : m_meta(mtptr)
        {
            m_serialize = [mem_ptr] (const uniform_type_info* mt,
                                     const void* obj,
                                     serializer* s)
            {
                mt->serialize(&(*reinterpret_cast<const C*>(obj).*mem_ptr), s);
            };
            m_deserialize = [mem_ptr] (const uniform_type_info* mt,
                                       void* obj,
                                       deserializer* d)
            {
                mt->deserialize(&(*reinterpret_cast<C*>(obj).*mem_ptr), d);
            };
        }

        member(member&& other) : m_meta(nullptr)
        {
            swap(other);
        }

        // a member that's not a member at all, but "forwards"
        // the 'self' pointer to make use *_member implementations
        static member fake_member(uniform_type_info* mtptr)
        {
            return {
                mtptr,
                [] (const uniform_type_info* mt, const void* obj, serializer* s)
                {
                    mt->serialize(obj, s);
                },
                [] (const uniform_type_info* mt, void* obj, deserializer* d)
                {
                    mt->deserialize(obj, d);
                }
            };
        }

        ~member()
        {
            delete m_meta;
        }

        member& operator=(member&& other)
        {
            swap(other);
            return *this;
        }

        inline void serialize(const void* parent, serializer* s) const
        {
            m_serialize(m_meta, parent, s);
        }

        inline void deserialize(void* parent, deserializer* d) const
        {
            m_deserialize(m_meta, parent, d);
        }

    };

    std::vector<member> m_members;

    // terminates recursion
    inline void push_back() { }

    // pr.first = member pointer
    // pr.second = meta object to handle pr.first
    template<typename R, class C, typename... Args>
    void push_back(std::pair<R C::*, util::abstract_uniform_type_info<R>*> pr,
                   const Args&... args)
    {
        m_members.push_back({ pr.second, pr.first });
        push_back(args...);
    }

    template<typename R, class C, typename... Args>
    typename util::enable_if<util::is_primitive<R> >::type
    push_back(R C::*mem_ptr, const Args&... args)
    {
        m_members.push_back({ new primitive_member<R>(), mem_ptr });
        push_back(args...);
    }

    template< typename R, class C,typename... Args>
    typename util::enable_if<is_stl_compliant_list<R> >::type
    push_back(R C::*mem_ptr, const Args&... args)
    {
        m_members.push_back({ new list_member<R>(), mem_ptr });
        push_back(args...);
    }

    template<typename R, class C, typename... Args>
    typename util::enable_if<is_stl_compliant_map<R> >::type
    push_back(R C::*mem_ptr, const Args&... args)
    {
        m_members.push_back({ new map_member<R>(), mem_ptr });
        push_back(args...);
    }

    template<typename R, class C, typename... Args>
    typename util::enable_if<is_invalid<R>>::type
    push_back(R C::*mem_ptr, const Args&... args)
    {
        static_assert(util::is_primitive<R>::value,
                      "T is neither a primitive type nor a "
                      "stl-compliant list/map");
    }

    template<class C>
    void init_(typename
               util::enable_if<
                   util::disjunction<std::is_same<C, util::void_type>,
                                     std::is_same<C, any_type>>>::type* = 0)
    {
        // any_type doesn't have any fields (no serialization required)
    }

    template<typename P>
    void init_(typename util::enable_if<util::is_primitive<P>>::type* = 0)
    {
        m_members.push_back(member::fake_member(new primitive_member<P>()));
    }

    template<typename X>
    void init_(typename
               util::disable_if<
                   util::disjunction<util::is_primitive<X>,
                                     std::is_same<X, util::void_type>,
                                     std::is_same<X, any_type>>>::type* = 0)
    {
        static_assert(util::is_primitive<X>::value,
                      "T is neither a primitive type nor a "
                      "stl-compliant list/map");
    }

 public:

    template<typename... Args>
    default_uniform_type_info_impl(const Args&... args)
    {
        push_back(args...);
    }

    default_uniform_type_info_impl()
    {
        init_<T>();
    }

    void serialize(const void* obj, serializer* s) const
    {
        s->begin_object(this->name());
        for (auto& m : m_members)
        {
            m.serialize(obj, s);
        }
        s->end_object();
    }

    void deserialize(void* obj, deserializer* d) const
    {
        std::string cname = d->seek_object();
        if (cname != this->name())
        {
            throw std::logic_error("wrong type name found");
        }
        d->begin_object(this->name());
        for (auto& m : m_members)
        {
            m.deserialize(obj, d);
        }
        d->end_object();
    }


};

} } // namespace detail

#endif // DEFAULT_UNIFORM_TYPE_INFO_IMPL_HPP