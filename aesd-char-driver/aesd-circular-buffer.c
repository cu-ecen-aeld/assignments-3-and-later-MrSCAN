/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#else
#include <string.h>
#include <pthread.h>
#endif
#include "aesd-circular-buffer.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
                                                                         size_t char_offset,
                                                                         size_t *entry_offset_byte_rtn)
{
    if (buffer == NULL || entry_offset_byte_rtn == NULL) {
        // Invalid char_offset, buffer, or entry_offset_byte_rtn
        return NULL;
    }

    pthread_mutex_lock(&mutex);  // Lock before accessing shared data

    size_t cumulative_offset = 0;
    size_t out_offs = buffer->out_offs;
    // Iterate through the circular buffer to find the corresponding entry
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        size_t value = (i + out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        // Check if the cumulative offset exceeds the target char_offset
        if (cumulative_offset + buffer->entry[value].size > char_offset) {
            *entry_offset_byte_rtn = char_offset - cumulative_offset;  // Use cumulative_offset instead of buffer->out_offs
            pthread_mutex_unlock(&mutex);  // Unlock after accessing shared data
            return &buffer->entry[value];
        }

        cumulative_offset += buffer->entry[value].size;
    }

    pthread_mutex_unlock(&mutex);  // Unlock after accessing shared data

    // If char_offset exceeds the total size of written data
    return NULL;
}



/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    if (buffer == NULL || add_entry == NULL) {
        // Invalid buffer, add_entry, or mutex
        return;
    }

    pthread_mutex_lock(&mutex);  // Lock before accessing shared data
    // Update buffer status
     if(buffer->in_offs == buffer->out_offs && buffer->entry[buffer->out_offs].size > 0){
        buffer->full = true;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; 
    }

     // Add the entry to the circular buffer at the current write position
    buffer->entry[buffer->in_offs] = *add_entry;

    // Update the write position
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; 

    pthread_mutex_unlock(&mutex);  // Unlock after accessing shared data
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
