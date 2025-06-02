#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define TEST_SIZE 100
#define DISABLE_UNMAP 0  // Set to 1 to test cleanup on exit

void
print_proc_size(char *label) 
{
  printf("Process size %s: %d bytes\n", label, (uint64)sbrk(0));
}

int
main(void)
{
  int pid;
  char *shared_data;
  uint64 shared_addr;
  char *malloc_ptr;
  uint64 original_size;
  
  printf("=== Shared Memory Test ===\n");
  
  // Allocate some data in parent
  shared_data = sbrk(TEST_SIZE);
  if(shared_data == (char*)-1) {
    printf("sbrk failed\n");
    exit(1);
  }
  
  // Initialize with test data
  for(int i = 0; i < TEST_SIZE; i++) {
    shared_data[i] = 'A' + (i % 26);
  }
  shared_data[TEST_SIZE-1] = '\0';
  
  printf("Parent allocated data at %p\n", shared_data);
  print_proc_size("in parent before fork");
  
  pid = fork();
  if(pid < 0) {
    printf("fork failed\n");
    exit(1);
  }
  
  if(pid == 0) {
    // Child process
    printf("Child process started (pid %d)\n", getpid());
    print_proc_size("in child before mapping");
    original_size = (uint64)sbrk(0);
    
    // Map shared memory from parent to child
    shared_addr = map_shared_pages(getppid(), getpid(), shared_data, TEST_SIZE);
    if(shared_addr == -1) {
      printf("map_shared_pages failed\n");
      exit(1);
    }
    
    printf("Child mapped shared memory at %p\n", (void*)shared_addr);
    print_proc_size("in child after mapping");
    
    // Read original data
    char temp[21];
    for(int i = 0; i < 20 && i < TEST_SIZE-1; i++) {
      temp[i] = ((char*)shared_addr)[i];
    }
    temp[20] = '\0';
    printf("Child reads: %s...\n", temp);
    
    // Write new data
    strcpy((char*)shared_addr, "Hello daddy");
    printf("Child wrote: %s\n", (char*)shared_addr);
    
    if(!DISABLE_UNMAP) {
      // Unmap shared memory
      if(unmap_shared_pages((void*)shared_addr, TEST_SIZE) != 0) {
        printf("unmap_shared_pages failed\n");
        exit(1);
      }
      
      printf("Child unmapped shared memory\n");
      print_proc_size("in child after unmapping");
      
      // Check if size returned to original
      uint64 current_size = (uint64)sbrk(0);
      if(current_size == original_size) {
        printf("SUCCESS: Process size returned to original (%d bytes)\n", original_size);
      } else {
        printf("WARNING: Process size not restored. Original: %d, Current: %d\n", 
               original_size, current_size);
      }
    }
    
    // Test malloc after unmapping
    malloc_ptr = malloc(50);
    if(malloc_ptr == 0) {
      printf("malloc failed after unmapping\n");
      exit(1);
    }
    
    printf("Child malloc succeeded at %p\n", malloc_ptr);
    strcpy(malloc_ptr, "malloc works");
    printf("Child malloc data: %s\n", malloc_ptr);
    print_proc_size("in child after malloc");
    
    free(malloc_ptr);
    exit(0);
  } else {
    // Parent process
    int status;
    wait(&status);
    
    printf("Parent reads after child: %s\n", shared_data);
    
    if(strcmp(shared_data, "Hello daddy") == 0) {
      printf("SUCCESS: Shared memory test passed!\n");
    } else {
      printf("FAILED: Expected 'Hello daddy', got '%s'\n", shared_data);
    }
    
    print_proc_size("in parent after child exit");
    
    // Test the DISABLE_UNMAP case
    printf("\n=== Testing DISABLE_UNMAP case ===\n");
    printf("To test cleanup on exit, change DISABLE_UNMAP to 1 and rebuild.\n");
    printf("The kernel should not free shared pages when child exits.\n");
  }
  
  exit(0);
}