#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_map_shared_pages(void)
{
  int src_pid, dst_pid;
  uint64 src_va, size;
  struct proc *src_proc, *dst_proc;
  uint64 ret;
  
  argint(0, &src_pid);
  argint(1, &dst_pid);
  argaddr(2, &src_va);
  argaddr(3, &size);
  
  src_proc = findproc(src_pid);
  if(src_proc == 0)
    return -1;

  dst_proc = findproc(dst_pid);
  if(dst_proc == 0)
    return -1;

  // Acquire locks in a consistent order to prevent deadlock
  // For simplicity, we'll lock src_proc then dst_proc if different.
  // A more robust way is to lock based on PID or address.
  // This simple order works if src_proc and dst_proc are always different
  // or if map_shared_pages handles the case where they are the same
  // (which it doesn't currently, but it's not a typical use case for this API).

  // For a more robust locking order:
  struct proc *p1 = src_proc, *p2 = dst_proc;
  if ((uint64)p1 > (uint64)p2) { // Ensure p1 has a lower memory address than p2
    struct proc *tmp = p1;
    p1 = p2;
    p2 = tmp;
  }

  acquire(&p1->lock);
  if (p1 != p2) { // Only acquire second lock if processes are different
    acquire(&p2->lock);
  }

  ret = map_shared_pages(src_proc, dst_proc, src_va, size);

  if (p1 != p2) {
    release(&p2->lock);
  }
  release(&p1->lock);
    
  return ret;
}

uint64
sys_unmap_shared_pages(void)
{
  uint64 addr, size;
  struct proc *p = myproc();
  uint64 ret;
  
  argaddr(0, &addr);
  argaddr(1, &size);

  acquire(&p->lock);
  ret = unmap_shared_pages(p, addr, size);
  release(&p->lock);
  
  return ret;
}

uint64
sys_getppid(void)
{
  return myproc()->parent->pid;
}
