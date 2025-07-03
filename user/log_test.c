#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NCHILD 10
#define BUFFER_SIZE 4096
#define MAX_MESSAGE_LEN 100

// Message header structure (must be 4-byte aligned)
struct message_header {
    uint16 length;     // Message length (without null terminator)
    uint16 child_id;   // Child process index
};

// Atomic compare and swap operation
static inline uint32
atomic_compare_and_swap(volatile uint32 *ptr, uint32 expected, uint32 new_val)
{
    return __sync_val_compare_and_swap(ptr, expected, new_val);
}

// Align address to 4-byte boundary
static inline uint64
align_address(uint64 addr)
{
    return (addr + 3) & ~3;
}

// Simple integer to string conversion
void
int_to_str(int num, char *str)
{
    if(num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    int i = 0;
    int temp = num;
    
    // Count digits
    while(temp > 0) {
        i++;
        temp /= 10;
    }
    
    str[i] = '\0';
    i--;
    
    // Convert digits
    while(num > 0) {
        str[i] = '0' + (num % 10);
        num /= 10;
        i--;
    }
}

void
child_process(uint64 shared_buffer, int child_id, uint64 buffer_size)
{
    // Each child writes multiple messages - make at least one child exceed page size
    int num_messages = (child_id == 0) ? 50 : 10; // Child 0 writes many messages
    
    for(int msg_num = 0; msg_num < num_messages; msg_num++) {
        char message[MAX_MESSAGE_LEN];
        char msg_num_str[10];
        char child_id_str[10];
        
        // Format message manually: "Message X from child Y"
        strcpy(message, "Message ");
        int_to_str(msg_num, msg_num_str);
        strcpy(message + strlen(message), msg_num_str);
        strcpy(message + strlen(message), " from child ");
        int_to_str(child_id, child_id_str);
        strcpy(message + strlen(message), child_id_str);
        
        int msg_len = strlen(message);
        
        // Find a free slot in the buffer
        uint64 current_addr = shared_buffer;
        int attempts = 0;
        
        while(current_addr + sizeof(struct message_header) + msg_len < shared_buffer + buffer_size && attempts < 1000) {
            // Check if we're properly aligned
            current_addr = align_address(current_addr);
            
            // Check bounds again after alignment
            if(current_addr + sizeof(struct message_header) + msg_len >= shared_buffer + buffer_size) {
                break;
            }
            
            volatile uint32 *header_ptr = (volatile uint32*)current_addr;
            uint32 expected = 0; // Free slot
            
            // Create new header
            struct message_header new_header;
            new_header.length = msg_len;
            new_header.child_id = child_id;
            uint32 new_header_val = *(uint32*)&new_header;
            
            // Try to claim this slot atomically
            uint32 old_val = atomic_compare_and_swap(header_ptr, expected, new_header_val);
            
            if(old_val == 0) {
                // Successfully claimed the slot, write the message
                char *msg_ptr = (char*)(current_addr + sizeof(struct message_header));
                memcpy(msg_ptr, message, msg_len);
                break;
            } else {
                // Slot was taken, skip to next potential location
                struct message_header *existing_header = (struct message_header*)&old_val;
                current_addr += sizeof(struct message_header) + existing_header->length;
                current_addr = align_address(current_addr);
                attempts++;
            }
        }
        
        if(attempts >= 1000) {
            break; // Buffer full or too contentious
        }
        
        // Brief yield to allow other processes to work
        // sleep(1); // removed for final submission
    }
    
    exit(0);
}

void
parent_process(uint64 shared_buffer, uint64 buffer_size)
{
    int messages_read = 0;
    int consecutive_empty_scans = 0;
    
    // Continuously scan for new messages while children are running
    while(consecutive_empty_scans < 30) { // Increased threshold
        uint64 read_addr = shared_buffer;
        int found_new_message = 0;
        
        // Scan entire buffer each time to catch all messages
        while(read_addr + sizeof(struct message_header) < shared_buffer + buffer_size) {
            read_addr = align_address(read_addr);
            
            // Check bounds after alignment
            if(read_addr + sizeof(struct message_header) >= shared_buffer + buffer_size) {
                break;
            }
            
            volatile uint32 *header_ptr = (volatile uint32*)read_addr;
            uint32 header_val = *header_ptr;
            
            if(header_val == 0) {
                // Free slot, move to next potential location
                read_addr += sizeof(struct message_header);
                continue;
            }
            
            struct message_header *header = (struct message_header*)&header_val;
            
            // Validate header values
            if(header->length == 0 || header->length > MAX_MESSAGE_LEN || 
               header->child_id >= NCHILD) {
                // Invalid header, skip
                read_addr += sizeof(struct message_header);
                continue;
            }
            
            // Check if we have enough space for the message
            if(read_addr + sizeof(struct message_header) + header->length >= shared_buffer + buffer_size) {
                break;
            }
            
            // Read the message
            char *msg_ptr = (char*)(read_addr + sizeof(struct message_header));
            char message[MAX_MESSAGE_LEN + 1];
            memcpy(message, msg_ptr, header->length);
            message[header->length] = '\0';
            
            printf("Parent: Read from child %d (len=%d): %s\n", header->child_id, header->length, message);
            messages_read++;
            found_new_message = 1;
            
            // Clear the header to mark as read - THIS PREVENTS RE-READING
            *header_ptr = 0;
            
            // Move to next message
            read_addr += sizeof(struct message_header) + header->length;
        }
        
        // Update scan counter
        if(found_new_message) {
            consecutive_empty_scans = 0;
        } else {
            consecutive_empty_scans++;
        }
        
        // Brief sleep to avoid busy waiting
        // sleep(1); // removed for final submission
    }
    
    printf("Parent: Read %d messages total\n", messages_read);
    
    // Wait for all children to finish
    for(int i = 0; i < NCHILD; i++) {
        wait(0);
    }
    
    printf("Parent: All children finished\n");
}

int
main(void)
{
    printf("=== Multi-Process Logging Test ===\n");
    
    // Allocate shared buffer in parent
    char *shared_buffer = sbrk(BUFFER_SIZE);
    if(shared_buffer == (char*)-1) {
        printf("Failed to allocate shared buffer\n");
        exit(1);
    }
    
    // Initialize buffer to zero
    memset(shared_buffer, 0, BUFFER_SIZE);
    
    printf("Parent: Allocated shared buffer at %p (size %d)\n", shared_buffer, BUFFER_SIZE);
    
    // Fork child processes
    for(int i = 0; i < NCHILD; i++) {
        int pid = fork();
        if(pid < 0) {
            printf("Fork failed for child %d\n", i);
            exit(1);
        }
        
        if(pid == 0) {
            // Child process
            // Map shared buffer from parent
            uint64 mapped_addr = map_shared_pages(getppid(), getpid(), shared_buffer, BUFFER_SIZE);
            if(mapped_addr == -1) {
                printf("Child %d: Failed to map shared buffer\n", i);
                exit(1);
            }
            
            // Start logging immediately
            child_process(mapped_addr, i, BUFFER_SIZE);
            // child_process calls exit(0)
        }
    }
    
    // Parent starts reading immediately - NO WAIT/SYNC
    // This achieves true concurrency as required
    parent_process((uint64)shared_buffer, BUFFER_SIZE);
    
    exit(0);
}