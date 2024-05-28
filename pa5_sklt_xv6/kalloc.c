// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

// written by Seung Jae Oh PA4
// LRU list for managing physical pages for swapping
void init_lru()
{
  page_lru_head = 0;
  num_lru_pages = 0;
}

void insert_lru(struct page *p)
{
  struct page *head = page_lru_head;
  if (head == 0)
  {
    page_lru_head = p;
    p->next = p;
    p->prev = p;
  }
  else
  {
    struct page *tail = head->prev;
    tail->next = p;
    p->prev = tail;
    p->next = head;
    head->prev = p;
  }
  ++num_lru_pages;
}

void delete_lru(struct page *p)
{
  struct page *prev = p->prev;
  struct page *next = p->next;
  prev->next = next;
  next->prev = prev;
  --num_lru_pages;
}

void update_lru(struct page *p)
{
  delete_page(p);
  insert_page(p);
}

struct page *evict_page()
{
  struct page *p = page_lru_head->next;
  struct page *updated_head;
  // search evictable page
  // if PTE_A==0, evitable page
  while (1)
  {
    pte_t *pte = walkpgdir(p->pgdir, p->vaddr, 0);
    if ((*pte & PTE_A) == 0)
    {
      break;
    }
    updated_head = p;
    p = p->next;
    update_lru(updated_head);
  }
  return p;
}

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct page pages[PHYSTOP / PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void freerange(void *vstart, void *vend)
{
  char *p;
  p = (char *)PGROUNDUP((uint)vstart);
  for (; p + PGSIZE <= (char *)vend; p += PGSIZE)
    kfree(p);
}
// PAGEBREAK: 21
//  Free the page of physical memory pointed at by v,
//  which normally should have been returned by a
//  call to kalloc().  (The exception is when
//  initializing the allocator; see kinit above.)
void kfree(char *v)
{
  struct run *r;

  if ((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if (kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run *)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if (kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char *
kalloc(void)
{
  struct run *r;

  // try_again:
  if (kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  //  if(!r && reclaim())
  //	  goto try_again;
  if (r)
    kmem.freelist = r->next;
  // there is not enough physical memory, swap out user memory
  else
  {
    
    struct page *page = evict_page();
    swapwrite(page->vaddr,num_free_pages);
    pte_t *pte = walkpgdir(page->pgdir, page->vaddr, 0);
    *pte &= ~PTE_P;

  }
  if (kmem.use_lock)
    release(&kmem.lock);
  return (char *)r;
}
