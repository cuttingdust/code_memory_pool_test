#include "ngx_mem_pool.h"

#include <stdio.h>
#include <string.h>

typedef struct Data stData;
struct Data
{
    char *ptr;
    FILE *pfile;
};

void func1(void *p)
{
    printf("free ptr mem!\n");
    free(p);
}
void func2(void *pf)
{
    printf("close file!\n");
    fclose(static_cast<FILE *>(pf));
}

int main(int argc, char *argv[])
{
    /// 512 - sizeof(ngx_pool_t) - 4095   =>   max
    ngx_mem_pool mem_pool(512);
    void        *p1 = mem_pool.ngx_palloc(128); /// 从小块内存池分配的
    if (p1 == NULL)
    {
        printf("ngx_palloc 128 bytes fail...");
        return -1;
    }

    stData *p2 = (stData *)mem_pool.ngx_palloc(512); /// 从大块内存池分配的
    if (p2 == NULL)
    {
        printf("ngx_palloc 512 bytes fail...");
        return -1;
    }
    p2->ptr = (char *)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");

    ngx_pool_cleanup_t *c1 = mem_pool.ngx_pool_cleanup_add(sizeof(char *));
    c1->handler            = func1;
    c1->data               = p2->ptr;

    ngx_pool_cleanup_t *c2 = mem_pool.ngx_pool_cleanup_add(sizeof(FILE *));
    c2->handler            = func2;
    c2->data               = p2->pfile;

    mem_pool.ngx_destroy_pool();

    getchar();
    return 0;
}
