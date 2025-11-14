/**
 * @file   SGIAllocator.h
 * @brief  SGI STL风格的内存池分配器实现
 * 
 * @details 移植自SGI STL的二级空间配置器，包含内存池管理和自由链表机制
 *          支持多线程环境，提供高效的小内存分配和回收
 * 
 * @author 31667
 * @date   2025-11-13
 */

#ifndef SGI_ALLOCATOR_H
#define SGI_ALLOCATOR_H

#include <cassert>
#include <mutex>
#include <cstddef>
#include <new>
#include <type_traits>
#include <memory>

#define SGI_VOLATILE      volatile               ///< volatile关键字宏，用于多线程环境
#define __THROW_BAD_ALLOC throw std::bad_alloc() ///< 内存分配失败时抛出异常

/**
 * @brief 一级空间配置器 - 直接使用malloc/free进行内存分配
 * @tparam Inst 实例标识，用于区分不同的分配器实例
 * 
 * @details 负责大内存块的分配，当内存不足时会调用用户设置的oom_handler
 */
template <int Inst>
class malloc_alloc_template
{
private:
    /// \brief 内存不足时的malloc处理函数
    static void* S_oom_malloc(size_t);

    /// \brief 内存不足时的realloc处理函数
    static void* S_oom_realloc(void*, size_t);

#ifndef __STL_STATIC_TEMPLATE_MEMBER_BUG
    /// \brief 内存不足处理函数指针
    static void (*malloc_alloc_oom_handler)();
#endif

public:
    /// \brief 分配指定大小的内存
    /// \param n 需要分配的字节数
    /// \return 分配的内存地址，失败时抛出bad_alloc异常
    static void* allocate(size_t n)
    {
        void* result = malloc(n);
        if (nullptr == result)
        {
            result = S_oom_malloc(n); /// 内存不足，尝试处理
        }
        return result;
    }

    /// \brief 释放之前分配的内存
    /// \param p 需要释放的内存地址
    /// \param n 内存块大小（未使用，为了接口统一）
    static void deallocate(void* p, size_t /* n */)
    {
        free(p);
    }

    /// \brief 重新分配内存（扩容或缩容）
    /// \param p 原内存地址
    /// \param old_sz 原内存大小（未使用）
    /// \param new_sz 新内存大小
    /// \return 重新分配后的内存地址
    static void* reallocate(void* p, size_t /* old_sz */, size_t new_sz)
    {
        void* result = realloc(p, new_sz);
        if (nullptr == result)
        {
            result = S_oom_realloc(p, new_sz); /// 内存不足，尝试处理
        }
        return result;
    }

    /// \brief 设置内存不足处理函数
    /// \param f 新的处理函数指针
    /// \return 旧的处理函数指针
    ///
    /// \details 用户可以通过此函数设置自定义的内存不足处理策略，
    ///          例如释放一些缓存或进行垃圾回收
    static void (*set_malloc_handler(void (*f)()))()
    {
        void (*old)()            = malloc_alloc_oom_handler;
        malloc_alloc_oom_handler = f;
        return old;
    }
};

/// 静态成员初始化
template <int Inst>
void (*malloc_alloc_template<Inst>::malloc_alloc_oom_handler)() = nullptr;

/// \brief 内存不足时的malloc处理实现
/// \details 循环调用用户设置的处理函数，直到分配成功或抛出异常
template <int Inst>
void* malloc_alloc_template<Inst>::S_oom_malloc(size_t _n)
{
    void (*my_malloc_handler)();
    void* result;

    for (;;) /// 无限循环，直到分配成功或抛出异常
    {
        my_malloc_handler = malloc_alloc_oom_handler;
        if (nullptr == my_malloc_handler)
        {
            __THROW_BAD_ALLOC; /// 没有设置处理函数，直接抛出异常
        }
        (*my_malloc_handler)(); /// 调用用户处理函数
        result = malloc(_n);    /// 再次尝试分配
        if (result)
        {
            return result; /// 分配成功，返回结果
        }
        /// 分配失败，继续循环
    }
}


/// \brief 内存不足时的realloc处理实现
template <int inst>
void* malloc_alloc_template<inst>::S_oom_realloc(void* p, size_t n)
{
    void (*my_malloc_handler)();
    void* result;

    for (;;)
    {
        my_malloc_handler = malloc_alloc_oom_handler;
        if (nullptr == my_malloc_handler)
        {
            __THROW_BAD_ALLOC;
        }
        (*my_malloc_handler)();
        result = realloc(p, n);
        if (result)
        {
            return result;
        }
    }
}

/// \brief 默认的一级空间配置器实例
typedef malloc_alloc_template<0> malloc_alloc;

///////////////////////////////////////////////////////////////////////////////////////////////////////

/// 内存池配置常量
enum
{
    _ALIGN = 8
}; ///< 内存对齐大小，自由链表以8字节为基准
enum
{
    _MAX_BYTES = 128
}; ///< 内存池管理的最大内存块大小
enum
{
    _NFREELISTS = 16
}; ///< 自由链表的数量（128/8=16）

/// \brief SGI STL二级空间配置器 - 带内存池的小内存分配器
/// \tparam T 分配的元素类型
///
/// \details 采用内存池和自由链表机制，高效管理小内存块的分配和回收。
///          小于等于128字节的内存请求使用内存池，大于128字节的转发给一级配置器。
///          线程安全，支持多线程环境。
template <class T>
class SGIAllocator
{
public:
    /// C++20 标准分配器类型定义
    using value_type      = T;         ///< 值类型
    using pointer         = T*;        ///< 指针类型
    using const_pointer   = const T*;  ///< 常量指针类型
    using reference       = T&;        ///< 引用类型
    using const_reference = const T&;  ///< 常量引用类型
    using size_type       = size_t;    ///< 大小类型
    using difference_type = ptrdiff_t; ///< 差值类型

    /// C++20 分配器特性
    template <class U>
    struct rebind
    {
        using other = SGIAllocator<U>; ///< 重新绑定到其他类型
    };

    /// 容器特性
    using propagate_on_container_copy_assignment = std::true_type;  ///< 支持拷贝赋值传播
    using propagate_on_container_move_assignment = std::true_type;  ///< 支持移动赋值传播
    using propagate_on_container_swap            = std::true_type;  ///< 支持交换传播
    using is_always_equal                        = std::false_type; ///< 分配器实例不总是相等

    /// 构造函数
    constexpr SGIAllocator() noexcept                    = default; ///< 默认构造函数
    constexpr SGIAllocator(const SGIAllocator&) noexcept = default; ///< 拷贝构造函数

    /// \brief 从其他类型分配器构造
    /// \tparam U 其他元素类型
    template <class U>
    constexpr SGIAllocator(const SGIAllocator<U>&) noexcept
    {
    }

    ~SGIAllocator() = default; ///< 析构函数

public:
    ///
    /// \brief 分配内存
    /// \param n 需要分配的元素数量
    /// \return 分配的内存地址
    /// \throw std::bad_alloc 当内存不足时抛出
    ///
    /// \details 如果请求的内存大于_MAX_BYTES，使用一级配置器；
    ///          否则从对应的自由链表中获取内存块
    ///
    [[nodiscard]] constexpr pointer allocate(size_type n)
    {
        if (n > max_size())
        {
            throw std::bad_alloc(); /// 请求大小超出最大限制
        }
        return static_cast<pointer>(S_allocate(n * sizeof(T)));
    }

    /// \brief 释放内存
    /// \param p 需要释放的内存地址
    /// \param n 释放的元素数量
    ///
    /// \details 将内存块回收至对应的自由链表
    constexpr void deallocate(pointer p, size_type n)
    {
        S_deallocate(p, n * sizeof(T));
    }

    /// \brief 在指定位置构造对象
    /// \tparam U 对象类型
    /// \tparam Args 参数类型
    /// \param p 对象构造地址
    /// \param args 构造参数
    template <class U, class... Args>
    constexpr void construct(U* p, Args&&... args)
    {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }

    /// \brief 销毁指定位置的对象
    /// \tparam U 对象类型
    /// \param p 对象地址
    template <class U>
    constexpr void destroy(U* p)
    {
        p->~U();
    }

    /// \brief 获取最大可分配的元素数量
    /// \return 最大元素数量
    constexpr size_type max_size() const noexcept
    {
        return (size_type(-1) / sizeof(value_type));
    }

    /// \brief 获取对象的地址
    /// \param x 对象引用
    /// \return 对象地址
    constexpr pointer address(reference x) const noexcept
    {
        return std::addressof(x);
    }

    /// \brief 获取常量对象的地址
    /// \param x 常量对象引用
    /// \return 常量对象地址
    constexpr const_pointer address(const_reference x) const noexcept
    {
        return std::addressof(x);
    }

    /// 比较操作
    template <class U>
    constexpr bool operator==(const SGIAllocator<U>&) const noexcept
    {
        return true; ///< 同类型分配器总是相等
    }

    template <class U>
    constexpr bool operator!=(const SGIAllocator<U>& other) const noexcept
    {
        return !(*this == other);
    }

public:
    /// \brief 自由链表节点联合体
    /// \
    /// \details 使用联合体实现，既可以作为链表节点，也可以作为用户数据存储区
    /// \         这种设计节省了内存，一个内存块同时满足管理和存储需求
    union Obj
    {
        /// 每一块chunk块的头信息 ， M_free_list_link存储下一个chunk块的地址
        union Obj* M_free_list_link; ///< 指向下一个空闲块的指针
        char       M_client_data[1]; ///< 用户数据存储区（最小1字节）
    };

private:
    /// 已分配的内存chunk 块的使用情况
    static char*             S_start_free;             ///< 内存池起始位置
    static char*             S_end_free;               ///< 内存池结束位置
    static size_t            S_heap_size;              ///< 内存池总大小
    static Obj* SGI_VOLATILE S_free_list[_NFREELISTS]; ///< 自由链表数组（volatile保证多线程可见性）
    static std::mutex        mtx;                      ///< 互斥锁，保证线程安全

    /// \brief 内部内存分配实现
    /// \param n 需要分配的字节数
    /// \return 分配的内存地址
    static void* S_allocate(size_t n)
    {
        void* ret = nullptr;

        if (n > (size_t)_MAX_BYTES)
        {
            /// 大内存块：使用一级配置器
            ret = malloc_alloc::allocate(n);
        }
        else
        {
            /// 小内存块：从自由链表获取
            Obj* SGI_VOLATILE*          my_free_list = S_free_list + S_freelist_index(n);
            std::lock_guard<std::mutex> lg(mtx); /// 加锁保证线程安全

            Obj* result = *my_free_list;
            if (result == nullptr)
            {
                /// 自由链表为空，需要重新填充
                ret = S_refill(S_round_up(n));
            }
            else
            {
                /// 从自由链表头部取一个块
                *my_free_list = result->M_free_list_link;
                ret           = result;
            }
        }
        return ret;
    }

    /// \brief 内部内存释放实现
    /// \param p 需要释放的内存地址
    /// \param n 释放的字节数
    static void S_deallocate(void* p, size_t n)
    {
        if (n > (size_t)_MAX_BYTES)
        {
            /// 大内存块：使用一级配置器
            malloc_alloc::deallocate(p, n);
        }
        else
        {
            /// 小内存块：回收到自由链表
            Obj* SGI_VOLATILE*          my_free_list = S_free_list + S_freelist_index(n);
            Obj*                        q            = static_cast<Obj*>(p);
            std::lock_guard<std::mutex> lg(mtx);

            /// 将块插入到自由链表头部
            q->M_free_list_link = *my_free_list;
            *my_free_list       = q;
        }
    }

    /// \brief 将字节数向上对齐到8的倍数
    /// \param bytes 原始字节数
    /// \return 对齐后的字节数
    ///
    /// \details 使用位运算实现高效对齐：
    ///          (bytes + 7) & ~7
    static size_t S_round_up(size_t bytes)
    {
        return (((bytes) + (size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
    }

    /// \brief 根据字节数计算对应的自由链表索引
    /// \param bytes 字节数
    /// \return 自由链表索引（0-15）
    ///
    /// \details 计算公式：((bytes + 7) / 8) - 1
    ///          例如：8字节 -> 索引0，16字节 -> 索引1，...，128字节 -> 索引15
    ///
    static size_t S_freelist_index(size_t bytes)
    {
        return (((bytes) + (size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
    }

    /// \brief 重新填充自由链表 主要是把分别配好的chunk块进行连接的
    /// \param n 每个内存块的大小（已对齐）
    /// \return 第一个内存块的地址
    ///
    /// \details 从内存池获取20个内存块，将第一个返回给用户，其余19个构建成链表
    static void* S_refill(size_t n)
    {
        int                nobjs = 20;                      /// 每次获取20个块
        char*              chunk = S_chunk_alloc(n, nobjs); /// 从内存池获取大块内存
        Obj* SGI_VOLATILE* my_free_list;
        Obj*               result;
        Obj*               current_obj;
        Obj*               next_obj;
        int                i;

        if (1 == nobjs)
        {
            /// 只获取到1个块，直接返回
            return chunk;
        }

        /// 定位到对应的自由链表
        my_free_list = S_free_list + S_freelist_index(n);

        /// 构建自由链表：第一个块返回给用户，其余19个构建成链表
        result        = reinterpret_cast<Obj*>(chunk);
        *my_free_list = next_obj = reinterpret_cast<Obj*>(chunk + n);

        /// 构建剩余的18个块的链表
        for (i = 1;; i++)
        {
            current_obj = next_obj;
            next_obj    = reinterpret_cast<Obj*>(reinterpret_cast<char*>(next_obj) + n);
            if (nobjs - 1 == i)
            {
                /// 最后一个块，next指针设为nullptr
                current_obj->M_free_list_link = nullptr;
                break;
            }
            else
            {
                /// 中间块，指向下一个块
                current_obj->M_free_list_link = next_obj;
            }
        }
        return result;
    }

    /// \brief 从内存池分配大块内存
    /// \param size 每个内存块的大小
    /// \param nobjs [in/out] 期望获取的块数/实际获取的块数
    /// \return 分配的内存块起始地址
    ///
    /// \details 这是内存池的核心函数，负责管理内存池的分配和扩容
    static char* S_chunk_alloc(size_t size, int& nobjs)
    {
        char*  result;
        size_t total_bytes = size * nobjs;
        size_t bytes_left  = S_end_free - S_start_free; /// 内存池剩余空间

        if (bytes_left >= total_bytes)
        {
            /// 情况1：内存池剩余空间足够分配所有请求的块
            result = S_start_free;
            S_start_free += total_bytes;
            return result;
        }
        else if (bytes_left >= size)
        {
            /// 情况2：内存池剩余空间不足以分配所有块，但至少能分配一个
            nobjs       = static_cast<int>(bytes_left / size); /// 调整实际分配数量
            total_bytes = size * nobjs;
            result      = S_start_free;
            S_start_free += total_bytes;
            return result;
        }
        else
        {
            /// 情况3：内存池剩余空间连一个块都不够分配

            /// 计算需要申请的新内存大小：2倍请求量 + 随堆大小增长的额外量
            size_t bytes_to_get = 2 * total_bytes + S_round_up(S_heap_size >> 4);

            /// 如果内存池还有剩余空间，将其回收到合适的自由链表
            if (bytes_left > 0)
            {
                Obj* SGI_VOLATILE* my_free_list                        = S_free_list + S_freelist_index(bytes_left);
                reinterpret_cast<Obj*>(S_start_free)->M_free_list_link = *my_free_list;
                *my_free_list                                          = reinterpret_cast<Obj*>(S_start_free);
            }

            /// 申请新的内存池空间
            S_start_free = static_cast<char*>(malloc(bytes_to_get));
            if (nullptr == S_start_free)
            {
                /// 内存申请失败：尝试从自由链表中寻找可用的大块内存
                for (size_t i = size; i <= static_cast<size_t>(_MAX_BYTES); i += static_cast<size_t>(_ALIGN))
                {
                    Obj* SGI_VOLATILE* my_free_list = S_free_list + S_freelist_index(i);
                    Obj*               p            = *my_free_list;
                    if (p != nullptr)
                    {
                        /// 找到可用的大块内存，将其作为新的内存池
                        *my_free_list = p->M_free_list_link;
                        S_start_free  = reinterpret_cast<char*>(p);
                        S_end_free    = S_start_free + i;
                        /// 递归调用，使用新获得的内存池重新分配
                        return S_chunk_alloc(size, nobjs);
                    }
                }

                /// 所有自由链表都为空，使用一级配置器作为最后手段
                S_end_free   = nullptr;
                S_start_free = static_cast<char*>(malloc_alloc::allocate(bytes_to_get));
            }

            /// 更新内存池状态并递归分配
            S_heap_size += bytes_to_get;
            S_end_free = S_start_free + bytes_to_get;
            return S_chunk_alloc(size, nobjs);
        }
    }
};

/// 静态成员定义和初始化
template <typename T>
char* SGIAllocator<T>::S_start_free = nullptr; ///< 内存池起始位置初始化

template <typename T>
char* SGIAllocator<T>::S_end_free = nullptr; ///< 内存池结束位置初始化

template <typename T>
size_t SGIAllocator<T>::S_heap_size = 0; ///< 内存池总大小初始化

template <typename T>
typename SGIAllocator<T>::Obj* volatile SGIAllocator<T>::S_free_list[_NFREELISTS] = { nullptr }; ///< 自由链表数组初始化

template <typename T>
std::mutex SGIAllocator<T>::mtx; ///< 互斥锁定义

#endif // SGI_ALLOCATOR_H
