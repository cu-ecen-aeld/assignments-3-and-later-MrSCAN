/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>      // file_operations
#include <linux/slab.h>    // for kmalloc and kfree
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include <linux/mutex.h>   // for mutex
#include "aesdchar.h"
#include "aesd_ioctl.h"
#include "aesd-circular-buffer.h"
#include <linux/device.h> // for device_create and device_destroy

static struct class *aesdchar_class = NULL; // Device class

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Chinonso Ngwu");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/* Function Prototypes */
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t off_set, int whence);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
ssize_t get_buffer_size(struct aesd_dev *dev);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("Opening device\n");

    // Get the pointer to the device structure
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    // Check if the device is valid
    if (!dev)
    {
        PDEBUG("Failed to open device: invalid device structure\n");
        return -ENODEV; // Return an error if the device is invalid
    }

    // Store the device pointer in the file's private data field
    filp->private_data = dev;

    PDEBUG("Device successfully opened\n");

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("Releasing device\n");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    size_t entry_offset = 0;
    struct aesd_buffer_entry *entry;
    size_t total_bytes_read = 0;
    size_t bytes_to_read;
    size_t bytes_not_copied;

    PDEBUG("Read %zu bytes with offset %lld\n", count, *f_pos);

    PDEBUG("Acquiring read lock...");
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Read lock interupted, exiting...");
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (!entry)
    {
        retval = total_bytes_read;
        goto out;
    }

    bytes_to_read = min(count, entry->size - entry_offset);
    bytes_not_copied = copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read);
    if (bytes_not_copied)
    {
        retval = -EFAULT;
        PDEBUG("Failed to copy %zu bytes to user\n", bytes_not_copied);
        retval = total_bytes_read > 0 ? total_bytes_read : -EFAULT;
        goto out;
    }
    total_bytes_read += bytes_to_read;
    *f_pos += bytes_to_read;

    PDEBUG("Successfully read %zu bytes\n", bytes_to_read);
    retval = total_bytes_read;

out:
    mutex_unlock(&dev->lock);
    PDEBUG("Release read lock...");
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    size_t bytes_not_copied;
    char *new_buff;
    char *combined_buff;
    bool newline_found;

    PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);

    PDEBUG("Acquiring write lock...");
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Write interupted, exiting...");
        retval = -ERESTARTSYS;
        goto out;
    }
    new_buff = kmalloc(count, GFP_KERNEL);
    if (!new_buff)
    {
        PDEBUG("Failed to allocate memory for write buffer\n");
        retval = -ENOMEM;
        goto out;
    }

    bytes_not_copied = copy_from_user(new_buff, buf, count);
    if (bytes_not_copied)
    {
        PDEBUG("Failed to copy %zu bytes from user\n", bytes_not_copied);
        kfree(new_buff);
        retval = -EFAULT;
        goto out;
    }

    if (dev->entry.buffptr == NULL)
    {
        PDEBUG("Buffer is empty, writing new entry to buffer");
        dev->entry.buffptr = new_buff;
    }
    else
    {
        PDEBUG("Buffer is not empty, appending to buffer");
        combined_buff = krealloc(dev->entry.buffptr, dev->entry.size + count, GFP_KERNEL);
        if (!combined_buff)
        {
            PDEBUG("Failed to allocate memory for combined buffer\n");
            kfree(new_buff);
            retval = -ENOMEM;
            goto out;
        }
        PDEBUG("Copying exisiting write buffer to combined buffer");
        memcpy(combined_buff, dev->entry.buffptr, dev->entry.size);
        PDEBUG("Copying new write enrty to combined buffer");
        memcpy(combined_buff + dev->entry.size, new_buff, count);
        PDEBUG("Free new buffer");
        kfree(new_buff);
        dev->entry.buffptr = combined_buff;
    }
    dev->entry.size += count;
    newline_found = dev->entry.buffptr[dev->entry.size - 1] == '\n';

    if (newline_found)
    {
        PDEBUG("New line found: Add entry to circular buffer");
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry);
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }

    retval = count;
    *f_pos += count;

out:
    mutex_unlock(&dev->lock);
    PDEBUG("Release write lock...");
    PDEBUG("Write complete, wrote %zu bytes\n", retval);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off_set, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    ssize_t dev_size = get_buffer_size(dev);

    switch (whence)
    {
    case SEEK_SET:
        newpos = off_set;
        break;
    case SEEK_CUR:
        newpos = filp->f_pos + off_set;
        break;
    case SEEK_END:
        newpos = dev_size + off_set;
        break;
    default:
        return -EINVAL;
    }

    if (newpos < 0 || newpos > dev_size)
        return -EINVAL;

    filp->f_pos = newpos;
    return newpos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t newpos = 0;
    int i;

    switch (cmd)
    {
    case AESDCHAR_IOCSEEKTO:
        if (copy_from_user(&seekto, (void __user *)arg, sizeof(seekto)))
            return -EFAULT;

        if (seekto.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            return -EINVAL;

        // Calculate new file position based on command index and offset
        for (i = 0; i < seekto.write_cmd; i++)
        {
            newpos += dev->buffer.entry[i].size;
        }

        if (seekto.write_cmd_offset >= dev->buffer.entry[seekto.write_cmd].size)
            return -EINVAL;

        newpos += seekto.write_cmd_offset;
        filp->f_pos = newpos;
        break;

    default:
        return -ENOTTY;
    }

    return 0;
}

ssize_t get_buffer_size(struct aesd_dev *dev)
{
    short i;
    ssize_t buffer_size = 0;
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Buffer lock interupted, exiting...");
        buffer_size = -ENOMEM;
        goto out;
    }
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        buffer_size += dev->buffer.entry[i].size;
    }
out:
    mutex_unlock(&dev->lock);
    PDEBUG("Release buffer lock...");
    return buffer_size;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        PDEBUG("Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    PDEBUG("Initializing AESD module\n");

    // Allocate a range of char device numbers
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        PDEBUG("Can't get major %d\n", aesd_major);
        return result;
    }

    // Zero out the device structure
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    // Initialize the mutex
    mutex_init(&aesd_device.lock);

    // Initialize the circular buffer
    aesd_circular_buffer_init(&aesd_device.buffer);

    // Set up the character device (cdev)
    result = aesd_setup_cdev(&aesd_device);
    if (result)
    {
        unregister_chrdev_region(dev, 1);
        return result;
    }

    // Create a device class
    aesdchar_class = class_create("aesdchar");
    if (IS_ERR(aesdchar_class))
    {
        cdev_del(&aesd_device.cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(aesdchar_class);
    }

    // Create the device node
    if (device_create(aesdchar_class, NULL, dev, NULL, "aesdchar") == NULL)
    {
        class_destroy(aesdchar_class);
        cdev_del(&aesd_device.cdev);
        unregister_chrdev_region(dev, 1);
        return -1;
    }

    PDEBUG("AESD module initialized, major number %d\n", aesd_major);
    return 0;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    size_t i = 0;

    PDEBUG("Cleaning up AESD module\n");

    // Remove the device node
    device_destroy(aesdchar_class, devno);

    // Destroy the device class
    class_destroy(aesdchar_class);

    // Remove the character device from the system
    cdev_del(&aesd_device.cdev);

    // Free the allocated memory buffers
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        if (aesd_device.buffer.entry[i].buffptr)
        {
            kfree(aesd_device.buffer.entry[i].buffptr);
        }
    }
    // Destroy the mutex
    mutex_destroy(&aesd_device.lock);

    // Unregister the device numbers
    unregister_chrdev_region(devno, 1);

    PDEBUG("AESD module cleaned up\n");
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
