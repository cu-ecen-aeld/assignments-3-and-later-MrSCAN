/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Chinonso Ngwu
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>   // file_operations
#include <linux/slab.h> // for kmalloc and kfree
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include <linux/mutex.h> // for mutex
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Chinonso Ngwu");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("Opening device\n");

    // Get the pointer to the device structure
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    // Check if the device is valid
    if (!dev) {
        PDEBUG("Failed to open device: invalid device structure\n");
        return -ENODEV;  // Return an error if the device is invalid
    }

    // Store the device pointer in the file's private data field
    filp->private_data = dev;

    PDEBUG("Device successfully opened\n");

    return 0;  // Return success
}


int aesd_release(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("Releasing device\n");

    // Check if the device pointer is valid
    if (!dev) {
        PDEBUG("Error: Device structure is invalid during release\n");
        return -ENODEV;  // Return an error if the device pointer is NULL
    }

    // Free the allocated memory for the buffer
    if (dev->buffer) {
        kfree(dev->buffer);
        dev->buffer = NULL;  // Avoid dangling pointers
    }

    PDEBUG("Device successfully released\n");

    return 0;  // Return success
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    size_t available_size;
    size_t bytes_to_read;
    size_t bytes_not_copied;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    // Acquire the lock before accessing the buffer
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;  // Return if interrupted

    // Check if the device buffer is valid
    if (!dev || !dev->buffer) {
        PDEBUG("Error: Invalid device or buffer\n");
        retval = -EFAULT;
        goto out;
    }

    // Determine how much data is available in the device buffer
    available_size = BUFFER_SIZE - *f_pos;  // Remaining data from the current offset

    if (available_size == 0) {
        PDEBUG("No more data to read\n");
        retval = 0;  // No more data to read (EOF)
        goto out;
    }

    // Limit the read size to the available data or the requested size, whichever is smaller
    bytes_to_read = min(count, available_size);

    // Copy data from the device buffer to the user space buffer
    bytes_not_copied = copy_to_user(buf, dev->buffer + *f_pos, bytes_to_read);

    if (bytes_not_copied) {
        retval = -EFAULT;  // Error occurred during copy_to_user
        PDEBUG("Failed to copy %zu bytes to user\n", bytes_not_copied);
    } else {
        // Successfully copied data, update the offset and return the number of bytes read
        retval = bytes_to_read;
        *f_pos += bytes_to_read;  // Advance the file pointer
        PDEBUG("Successfully read %zu bytes\n", bytes_to_read);
    }

out:
    mutex_unlock(&dev->lock);  // Release the lock
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    size_t available_space;
    size_t bytes_to_write;
    size_t bytes_not_copied;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    // Acquire the lock before modifying the buffer
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;  // Return if interrupted

    // Check if the device pointer is valid
    if (!dev || !dev->buffer) {
        PDEBUG("Error: Invalid device or buffer\n");
        retval = -EFAULT;
        goto out;
    }

    // Determine how much space is available in the device buffer from the current offset
    available_space = BUFFER_SIZE - *f_pos;

    if (available_space == 0) {
        PDEBUG("No more space in the device buffer\n");
        retval = -ENOSPC;  // No space left to write (buffer is full)
        goto out;
    }

    // Limit the write size to the available space or the requested size, whichever is smaller
    bytes_to_write = min(count, available_space);

    // Copy data from the user space buffer to the device buffer
    bytes_not_copied = copy_from_user(dev->buffer + *f_pos, buf, bytes_to_write);

    if (bytes_not_copied) {
        PDEBUG("Failed to copy %zu bytes from user\n", bytes_not_copied);
        retval = -EFAULT;  // Error occurred during copy_from_user
    } else {
        // Successfully copied data, update the offset and return the number of bytes written
        retval = bytes_to_write;
        *f_pos += bytes_to_write;  // Advance the file pointer
        PDEBUG("Successfully wrote %zu bytes\n", bytes_to_write);
    }

out:
    mutex_unlock(&dev->lock);  // Release the lock
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    // Allocate a range of char device numbers
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    // Zero out the device structure
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * Initialize AESD specific portions of the device
     */
    // Example: allocate memory for the device buffer
    aesd_device.buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!aesd_device.buffer) {
        printk(KERN_ERR "Failed to allocate memory for device buffer\n");
        unregister_chrdev_region(dev, 1);
        return -ENOMEM;
    }

    mutex_init(&aesd_device.lock);

    // Set up the character device (cdev)
    result = aesd_setup_cdev(&aesd_device);
    if (result) {
        // Cleanup in case of failure
        kfree(aesd_device.buffer);
        unregister_chrdev_region(dev, 1);
        return result;
    }

    printk(KERN_INFO "AESD module initialized, major number %d\n", aesd_major);
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    // Remove the character device from the system
    cdev_del(&aesd_device.cdev);

    /**
     * Cleanup AESD specific portions here
     */
    // Example: free the allocated memory buffer
    if (aesd_device.buffer) {
        kfree(aesd_device.buffer);
    }

    // Destroy the mutex (if needed)
    mutex_destroy(&aesd_device.lock);

    // Unregister the device numbers
    unregister_chrdev_region(devno, 1);

    printk(KERN_INFO "AESD module cleaned up\n");
}




module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
