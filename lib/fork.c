// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern volatile pte_t uvpt[];     // VA of "virtual page table"
extern volatile pde_t uvpd[];     // VA of current page directory
extern void _pgfault_upcall(void);
//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	//注意！addr有可能不是页对齐的
	void *addr = (void *) ROUNDDOWN(utf->utf_fault_va, PGSIZE);
	uint32_t err = utf->utf_err;
	int r;
	pte_t pte = uvpt[(uintptr_t)addr/PGSIZE];
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	//不管是parent还是child都不会复用实际的这个物理页，当最终引用计数降为０时，这个页自然会被销毁
	// LAB 4: Your code here.
	if((FEC_WR & err) && (pte & PTE_COW)){
		sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W);
		memmove(PFTEMP, addr, PGSIZE);
		sys_page_map(0, PFTEMP, 0, addr, PTE_P|PTE_U|PTE_W);
		//unmap操作其实可以省略，下次用到的时候自然会unmap
		sys_page_unmap(0, PFTEMP);
	}else{
		panic("page fault at %x !", (uintptr_t)addr);
	}
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)//因为如果已经是ｃｏｗ的话，说明父进程的父进程也共享同一副本，我们需要写一下这个副本，使得父进程
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{	
	// LAB 4: Your code here.
	int r = 0;
	void* addr = (void*)(pn * PGSIZE);
	if(pn == (UXSTACKTOP/PGSIZE - 1)){
		if((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_P|PTE_W|PTE_U)) < 0)
			panic("sys_page_alloc: %e", r);
	}else{
		pte_t pte = uvpt[pn];
		//TODO:此处是否需要检测PTE_U,目前问题在于stack上面那个页到底是个啥？？？
		if((pte & PTE_P) == 0 || (pte & PTE_U) == 0){
			return 0;
		}
		else if((pte & PTE_W) || (pte & PTE_COW)){
			/*
			注意，此刻的顺序不可颠倒：因为现在parent正在修改栈，如果先把栈的地方设置成COW，
			那么会立刻因为write而触发page fault,从而将栈设置为write。之后child照常设置成COW，
			但此时parent的改变已经反映在了child的栈上
			*/
			if((r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P|PTE_COW)) < 0){
				panic("sys_page_map: %e", r);
			}
			if((r = sys_page_map(0, addr, 0, addr, PTE_U|PTE_P|PTE_COW)) < 0){
				panic("sys_page_map: %e", r);
			}
		}else{
			if((r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P)) < 0)
				panic("sys_page_map: %e, pn:%d", r, pn);
		}
	}
	return r;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	for(int i = 0; i<UTOP/PGSIZE; i++){
		pde_t pde = uvpd[i/NPTENTRIES];
		if(pde & PTE_P){
			duppage(envid, i);
		}
	}
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	sys_env_set_status(envid, ENV_RUNNABLE);
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
