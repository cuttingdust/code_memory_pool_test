#pragma once

#include <memory.h>
#include <stdlib.h>

#include <memory>

//////////////////////////////////////////////////////////////////
#define LF   (u_char)'\n'
#define CR   (u_char)'\r'
#define CRLF "\r\n"

#define NGX_OK       0
#define NGX_ERROR    -1
#define NGX_AGAIN    -2
#define NGX_BUSY     -3
#define NGX_DONE     -4
#define NGX_DECLINED -5
#define NGX_ABORT    -6

#ifndef NGX_ALIGNMENT
#define NGX_ALIGNMENT sizeof(unsigned long) /* platform word */
#endif

/// \brief 把数值d 调整成临近的a的倍数
/// \param d
/// \param a
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
#define ngx_align_ptr(p, a) (u_char *)(((uintptr_t)(p) + ((uintptr_t)a - 1)) & ~((uintptr_t)a - 1))

#define ngx_memzero(buf, n)   (void)memset(buf, 0, n)
#define ngx_memset(buf, c, n) (void)memset(buf, c, n)

//////////////////////////////////////////////////////////////////


struct ngx_pool_s;
using u_char     = unsigned char;
using ngx_uint_t = unsigned int;
using ngx_int_t  = intptr_t;

/// \brief 清理函数(回调函数)的类型
typedef void (*ngx_pool_cleanup_pt)(void *data);
struct ngx_pool_cleanup_s
{
    ngx_pool_cleanup_pt handler; ///< 定义了一个函数指针, 保存清理操作的回调函数
    void               *data;    ///< 传递给回调函数的参数
    ngx_pool_cleanup_s *next;    ///< 所有的cleanup清理操作 都被串在一条链表上
};
typedef struct ngx_pool_cleanup_s ngx_pool_cleanup_t;

/// \brief 大块内存的头部信息
struct ngx_pool_large_s
{
    ngx_pool_large_s *next  = nullptr; ///< 所有的大块内存分配也是被串在一条链表上
    void             *alloc = nullptr; ///< 保存分配出去的大块内存的起始地址
};
typedef struct ngx_pool_large_s ngx_pool_large_t;


/// \brief 分配小块内存的内存池的头部数据信息
struct ngx_pool_data_t
{
    u_char     *last = nullptr; ///< 小块内存池可用内存的起始地址
    u_char     *end  = nullptr; ///< 小块内存池可用内存的末尾地址
    ngx_pool_s *next = nullptr; ///< 所有小块内存池都被串在了一条链表上
    ngx_uint_t  failed;         ///< 记录了当前小块内存池分配内存失败的次数
};

/// \brief ngx内存池的头部信息和管理成员信息
struct ngx_pool_s
{
    ngx_pool_data_t     d;                 ///< 存储的是当前小块内存池的使用情况
    size_t              max;               ///< 存储的是小块内存和大块内存的分界线
    ngx_pool_s         *current = nullptr; ///< 指向第一个提供小块内存分配的小块内存池
    ngx_pool_large_s   *large   = nullptr; ///< 指向大块内存(链表)的入口地址
    ngx_pool_cleanup_s *cleanup = nullptr; ///< 指向所有预置的清理操作回调函数(链表)的入口
};

typedef struct ngx_pool_s ngx_pool_t;

//////////////////////////////////////////////////////////////////


/// \brief 默认一个物理页面的大小4K
constexpr int ngx_pagesize = 4096;

/// \brief ngx小块内存池可分配的最大空间
constexpr int NGX_MAX_ALLOC_FROM_POOL = (ngx_pagesize - 1);

/// \brief 表示一个默认的ngx内存池开辟的大小
constexpr int NGX_DEFAULT_POOL_SIZE = (16 * 1024); /// 16K

/// \brief 内存池大小按照16字节进行对齐
constexpr int NGX_POOL_ALIGNMENT = 16;

/// \brief ngx小块内存池最小的size 调整成 NGX_POOL_ALIGNMENT 的临近的倍数
constexpr int NGX_MIN_POOL_SIZE = ngx_align((sizeof(ngx_pool_s) + 2 * sizeof(ngx_pool_large_s)), NGX_POOL_ALIGNMENT);

//////////////////////////////////////////////////////////////////


/// \brief 移植nginx 内存池的代码， 用OOP来实现
class ngx_mem_pool final
{
public:
    explicit ngx_mem_pool();
    explicit ngx_mem_pool(size_t size);
    ~ngx_mem_pool();

public:
    /// \brief 创建指定size大小的内存池， 但是小块内存池不超过一个页面的大小
    /// \param size
    /// \return
    void ngx_create_pool(size_t size);

    /// \brief 考虑内存字节对齐, 从内存池申请size大小的内存
    /// \param size
    /// \return
    void *ngx_palloc(size_t size);

    /// \brief 不考虑内存对齐， 从内存池申请size大小的内存
    /// \param size
    /// \return
    void *ngx_pnalloc(size_t size);

    /// \brief 初始化0, 调用的是ngx_palloc
    /// \param size
    /// \return
    void *ngx_pcalloc(size_t size);

    /// \brief 释放大块内存
    /// \param p
    ngx_int_t ngx_pfree(void *p);

    /// \brief 内存重置函数
    void ngx_reset_pool();

    /// \brief 添加回调清理操作函数
    /// \param size
    /// \return
    ngx_pool_cleanup_t *ngx_pool_cleanup_add(size_t size);

    /// \brief 内存池销毁函数
    void ngx_destroy_pool();

private:
    /// \brief 小块内存的分配
    /// \param size
    /// \param align
    /// \return
    void *ngx_palloc_small(size_t size, ngx_uint_t align);

    /// \brief 大块内存的分配
    /// \param size
    /// \return
    void *ngx_palloc_large(size_t size);

    /// \brief 分配新的小块内存池
    /// \param size
    /// \return
    void *ngx_palloc_block(size_t size);

private:
    class PImpl;
    std::unique_ptr<PImpl> impl_;
};
