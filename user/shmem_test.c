#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h" // For PGSIZE

// PGSIZE is 4096 in xv6
#define PGSIZE 4096
#define TEST_SIZE (PGSIZE + 200) // Data that spans more than one page
#define DISABLE_UNMAP 1 // Set to 1 to test cleanup on exit

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
  
  // Initialize with test data (enough to span pages)
  printf("Parent initializing %d bytes of shared data...\n", TEST_SIZE);
  for(int i = 0; i < TEST_SIZE; i++) {
    shared_data[i] = 'A' + (i % 26);
  }
  // Ensure null termination if used as a C-string, though for raw data it's not strictly needed
  // For very large data, avoid full string operations like strcpy if not necessary.
  // shared_data[TEST_SIZE-1] = '\0'; // Only if TEST_SIZE is small enough for a single string

  printf("Parent allocated data at %p (size %d)\n", shared_data, TEST_SIZE);
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
    
    // Read original data (first 20 bytes and last 20 bytes to check boundaries)
    char temp_start[21];
    char temp_end[21];

    for(int i = 0; i < 20; i++) { // First 20 bytes
      temp_start[i] = ((char*)shared_addr)[i];
    }
    temp_start[20] = '\0';

    for(int i = 0; i < 20; i++) { // Last 20 bytes
        if (TEST_SIZE > 20) { // Ensure we don't read out of bounds if TEST_SIZE < 20
            temp_end[i] = ((char*)shared_addr)[TEST_SIZE - 20 + i];
        } else {
            temp_end[i] = ((char*)shared_addr)[i]; // if TEST_SIZE is small, just read start
        }
    }
    temp_end[20] = '\0';
    printf("Child reads (start): %s...\n", temp_start);
    if (TEST_SIZE > 20) {
        printf("Child reads (end): ...%s\n", temp_end);
    }
    
    // Write new data - e.g., modify first few bytes and last few bytes
    printf("Child writing to shared memory...\n");
    strcpy((char*)shared_addr, "CHILD_WROTE_START"); // Modifies the beginning
    if (TEST_SIZE > 50) { // Ensure space for end marker
        char end_marker[] = "CHILD_WROTE_END";
        int marker_len = strlen(end_marker);
        if (TEST_SIZE >= marker_len) {
            strcpy((char*)shared_addr + TEST_SIZE - marker_len -1 , end_marker); // Modifies the end
        }
    }
    printf("Child finished writing.\n");
    
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
    
    printf("Parent checking shared memory after child exit...\n");
    // Parent reads and verifies the changes made by the child
    char expected_start[] = "CHILD_WROTE_START";
    char actual_start[sizeof(expected_start)];
    memcpy(actual_start, shared_data, sizeof(expected_start)-1);
    actual_start[sizeof(expected_start)-1] = '\0';

    printf("Parent reads (start): %s\n", actual_start);

    int success = 1;
    if(strcmp(actual_start, expected_start) != 0) {
      printf("FAILED: Parent did not see child's start marker. Expected: '%s', Got: '%s'\n", expected_start, actual_start);
      success = 0;
    }

    if (TEST_SIZE > 50) {
        char expected_end[] = "CHILD_WROTE_END";
        char actual_end[sizeof(expected_end)];
        int marker_len = strlen(expected_end);
        if (TEST_SIZE >= marker_len) {
            memcpy(actual_end, shared_data + TEST_SIZE - marker_len -1, sizeof(expected_end)-1);
            actual_end[sizeof(expected_end)-1] = '\0';
            printf("Parent reads (end): %s\n", actual_end);
            if(strcmp(actual_end, expected_end) != 0) {
              printf("FAILED: Parent did not see child's end marker. Expected: '%s', Got: '%s'\n", expected_end, actual_end);
              success = 0;
            }
        }
    }
    
    if(success){
      printf("SUCCESS: Shared memory test passed (multi-page data verified)!\n");
    } else {
      printf("FAILED: Shared memory test failed (multi-page data verification)!\n");
    }
    
    print_proc_size("in parent after child exit");
    
    // Test the DISABLE_UNMAP case
    printf("\n=== Testing DISABLE_UNMAP case ===\n");
    printf("To test cleanup on exit, change DISABLE_UNMAP to 1 in shmem_test.c and rebuild.\n");
    printf("The kernel should not free shared pages when child exits.\n");
  }
  
  exit(0);
}