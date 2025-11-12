#include "ngx_mem_pool.h"

#include <stdexcept>

class ngx_mem_pool::PImpl
{
public:
    PImpl(ngx_mem_pool *owenr);
    ~PImpl() = default;

public:
    /// \brief 创建指定size大小的内存池， 但是小块内存池不超过一个页面的大小
    /// \param size
    /// \return
    static void *ngx_create_pool(size_t size);

    /// \brief 内存池销毁函数
    /// \param pool
    static void ngx_destroy_pool(ngx_pool_t *pool);

public:
    ngx_mem_pool *owenr_ = nullptr;
    ngx_pool_s   *pool_  = nullptr; /// 指向ngx内存池的入口指针
};

ngx_mem_pool::PImpl::PImpl(ngx_mem_pool *owenr) : owenr_(owenr)
{
}

void *ngx_mem_pool::PImpl::ngx_create_pool(size_t size)
{
    ngx_pool_s *p = nullptr;
    p             = (ngx_pool_s *)malloc(size);
    if (p == NULL)
    {
        return NULL;
    }

    p->d.last   = (u_char *)p + sizeof(ngx_pool_t);
    p->d.end    = (u_char *)p + size;
    p->d.next   = NULL;
    p->d.failed = 0;

    size   = size - sizeof(ngx_pool_s);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->large   = NULL;
    p->cleanup = NULL;


    return p;
}

void ngx_mem_pool::PImpl::ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t         *p, *n;
    ngx_pool_large_t   *l;
    ngx_pool_cleanup_t *c;

    for (c = pool->cleanup; c; c = c->next)
    {
        if (c->handler)
        {
            c->handler(c->data);
        }
    }


    for (l = pool->large; l; l = l->next)
    {
        if (l->alloc)
        {
            free(l->alloc);
        }
    }

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next)
    {
        free(p);

        if (n == nullptr)
        {
            break;
        }
    }

    p = nullptr;
}

ngx_mem_pool::ngx_mem_pool()
{
    impl_ = std::make_unique<ngx_mem_pool::PImpl>(this);
}

ngx_mem_pool::ngx_mem_pool(size_t size)
{
    impl_ = std::make_unique<ngx_mem_pool::PImpl>(this);
    this->ngx_create_pool(size);
}

ngx_mem_pool::~ngx_mem_pool()
{
    this->ngx_destroy_pool();
}

void *ngx_mem_pool::ngx_create_pool(size_t size)
{
    if (!impl_->pool_)
    {
        impl_->pool_ = static_cast<ngx_pool_s *>(PImpl::ngx_create_pool(size));
        if (!impl_->pool_)
        {
            printf("ngx_create_pool failed\n");
        }
    }
}

void *ngx_mem_pool::ngx_palloc(size_t size)
{
    if (!impl_->pool_)
    {
        return nullptr;
    }

    if (size <= impl_->pool_->max)
    {                                     /// 大小内存的分界线 就是一个页面
        return ngx_palloc_small(size, 1); /// 考虑内存对齐
    }

    return ngx_palloc_large(size);
}

void *ngx_mem_pool::ngx_pnalloc(size_t size)
{
    if (!impl_->pool_)
    {
        return nullptr;
    }

    if (size <= impl_->pool_->max)
    {
        return ngx_palloc_small(size, 0); /// 不考虑内存对齐
    }

    return ngx_palloc_large(size);
}

void *ngx_mem_pool::ngx_pcalloc(size_t size)
{
    void *p = ngx_palloc(size); /// 内存开辟完之后 会清零
    if (p)
    {
        ngx_memzero(p, size);
    }

    return p;
}

ngx_int_t ngx_mem_pool::ngx_pfree(void *p)
{
    if (!impl_->pool_)
    {
        return NGX_ERROR;
    }

    ngx_pool_large_s *l;

    for (l = impl_->pool_->large; l; l = l->next)
    {
        if (p == l->alloc)
        {
            free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}

void ngx_mem_pool::ngx_reset_pool()
{
    ngx_pool_s       *p;
    ngx_pool_large_s *l;

    if (!impl_->pool_)
    {
        return;
    }

    /// 大块内存重置
    for (l = impl_->pool_->large; l; l = l->next)
    {
        if (l->alloc)
        {
            free(l->alloc);
        }
    }

    /// 小块内存重置
    /* for (p = pool_; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
        p->d.failed = 0;
    }*/

    {
        /// 处理第一个块内存池
        p           = impl_->pool_;
        p->d.last   = (u_char *)p + sizeof(ngx_pool_s);
        p->d.failed = 0;

        /// 第二个内存池开始循环到最后一个内存池
        for (p = p->d.next; p; p = p->d.next)
        {
            p->d.last   = (u_char *)p + sizeof(ngx_pool_data_t);
            p->d.failed = 0;
        }
    }


    impl_->pool_->current = impl_->pool_;
    impl_->pool_->large   = nullptr;
}

ngx_pool_cleanup_t *ngx_mem_pool::ngx_pool_cleanup_add(size_t size)
{
    ngx_pool_cleanup_t *c;

    c = (ngx_pool_cleanup_t *)ngx_palloc(sizeof(ngx_pool_cleanup_t));
    if (c == NULL)
    {
        return NULL;
    }

    if (size)
    {
        c->data = ngx_palloc(size);
        if (c->data == NULL)
        {
            return NULL;
        }
    }
    else
    {
        c->data = NULL;
    }

    c->handler = NULL;
    c->next    = impl_->pool_->cleanup;

    impl_->pool_->cleanup = c;

    return c;
}

void ngx_mem_pool::ngx_destroy_pool()
{
    if (impl_->pool_)
    {
        PImpl::ngx_destroy_pool(impl_->pool_);
    }
}

void *ngx_mem_pool::ngx_palloc_small(size_t size, ngx_uint_t align)
{
    u_char     *m;
    ngx_pool_t *p;

    if (!impl_->pool_)
    {
        return nullptr;
    }

    p = impl_->pool_->current;

    do
    {
        m = p->d.last;

        if (align)
        {
            m = ngx_align_ptr(m, NGX_ALIGNMENT); /// 调整内存对齐 64 位 8 字节  32 位 4字节
        }

        if ((size_t)(p->d.end - m) >= size)
        { /// 调整内存池 起始位置
            p->d.last = m + size;

            return m; /// 内存够 申请的大小 所以返回
        }

        p = p->d.next; /// 内存不够 指向下一个 第一个是null 的话 后面继续分配
    }
    while (p);

    return ngx_palloc_block(size); /// 继续分配
}

void *ngx_mem_pool::ngx_palloc_large(size_t size)
{
    void             *p;
    ngx_uint_t        n;
    ngx_pool_large_t *large;

    if (!impl_->pool_)
    {
        return nullptr;
    }

    p = malloc(size);
    if (p == NULL)
    {
        return NULL;
    }

    n = 0;

    for (large = impl_->pool_->large; large; large = large->next)
    {
        if (large->alloc == NULL)
        {
            large->alloc = p;
            return p;
        }

        if (n++ > 3)
        {
            break;
        }
    }

    large = (ngx_pool_large_t *)ngx_palloc_small(sizeof(ngx_pool_large_t), 1);
    if (large == NULL)
    {
        free(p);
        return NULL;
    }

    large->alloc        = p;
    large->next         = impl_->pool_->large;
    impl_->pool_->large = large;

    return p;
}

void *ngx_mem_pool::ngx_palloc_block(size_t size)
{
    u_char     *m;
    size_t      psize;
    ngx_pool_t *p, *newpool;

    if (!impl_->pool_)
    {
        return nullptr;
    }

    psize = (size_t)(impl_->pool_->d.end - (u_char *)impl_->pool_); /// 分配一个一样的

    m = (u_char *)malloc(ngx_align(NGX_POOL_ALIGNMENT, psize)); /// 开辟一个一样的
    if (m == NULL)
    {
        return NULL;
    }

    newpool = (ngx_pool_t *)m; /// 起始地址

    newpool->d.end    = m + psize;
    newpool->d.next   = NULL;
    newpool->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    m               = ngx_align_ptr(m, NGX_ALIGNMENT);
    newpool->d.last = m + size;

    for (p = impl_->pool_->current; p->d.next; p = p->d.next)
    {
        if (p->d.failed++ > 4)
        { /// 内存分配失败 超过4次 几乎没有多的内存 指向下一个
            impl_->pool_->current = p->d.next;
        }
    }

    p->d.next = newpool;

    return m;
}
