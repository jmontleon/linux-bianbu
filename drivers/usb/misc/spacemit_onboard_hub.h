#include <linux/property.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>


struct spacemit_hub_priv {
	struct device *dev;
	bool is_hub_on;
	bool is_vbus_on;

	struct gpio_descs *hub_gpios;
	struct gpio_descs *vbus_gpios;
	bool hub_gpio_active_low;
	bool vbus_gpio_active_low;

	u32 hub_inter_delay_ms;
	u32 vbus_delay_ms;
	u32 vbus_inter_delay_ms;

	bool suspend_power_on;

	struct mutex hub_mutex;
};

static void spacemit_hub_enable(struct spacemit_hub_priv *spacemit, bool on);

static void spacemit_hub_vbus_enable(struct spacemit_hub_priv *spacemit,
					bool on);

static int spacemit_hub_enable_show(struct seq_file *s, void *unused)
{
	struct spacemit_hub_priv *spacemit = s->private;
	mutex_lock(&spacemit->hub_mutex);
	seq_puts(s, spacemit->is_hub_on ? "true\n" : "false\n");
	mutex_unlock(&spacemit->hub_mutex);
	return 0;
}

static ssize_t spacemit_hub_enable_write(struct file *file,
					   const char __user *ubuf, size_t count,
					   loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct spacemit_hub_priv *spacemit = s->private;
	bool on = false;
	char buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if ((!strncmp(buf, "true", 4)) || (!strncmp(buf, "1", 1)))
		on = true;
	if ((!strncmp(buf, "false", 5)) || !strncmp(buf, "0", 1))
		on = false;

	mutex_lock(&spacemit->hub_mutex);
	if (on != spacemit->is_hub_on) {
		spacemit_hub_enable(spacemit, on);
	}
	mutex_unlock(&spacemit->hub_mutex);

	return count;
}

static int spacemit_hub_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, spacemit_hub_enable_show, inode->i_private);
}

struct file_operations spacemit_hub_enable_fops = {
	.open = spacemit_hub_enable_open,
	.write = spacemit_hub_enable_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int spacemit_hub_vbus_show(struct seq_file *s, void *unused)
{
	struct spacemit_hub_priv *spacemit = s->private;
	mutex_lock(&spacemit->hub_mutex);
	seq_puts(s, spacemit->is_vbus_on ? "true\n" : "false\n");
	mutex_unlock(&spacemit->hub_mutex);
	return 0;
}

static ssize_t spacemit_hub_vbus_write(struct file *file,
					   const char __user *ubuf, size_t count,
					   loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct spacemit_hub_priv *spacemit = s->private;
	bool on = false;
	char buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if ((!strncmp(buf, "true", 4)) || (!strncmp(buf, "1", 1)))
		on = true;
	if ((!strncmp(buf, "false", 5)) || !strncmp(buf, "0", 1))
		on = false;

	mutex_lock(&spacemit->hub_mutex);
	if (on != spacemit->is_vbus_on) {
		spacemit_hub_vbus_enable(spacemit, on);
	}
	mutex_unlock(&spacemit->hub_mutex);

	return count;
}

static int spacemit_hub_vbus_open(struct inode *inode, struct file *file)
{
	return single_open(file, spacemit_hub_vbus_show, inode->i_private);
}

struct file_operations spacemit_hub_vbus_fops = {
	.open = spacemit_hub_vbus_open,
	.write = spacemit_hub_vbus_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int spacemit_hub_suspend_show(struct seq_file *s, void *unused)
{
	struct spacemit_hub_priv *spacemit = s->private;
	mutex_lock(&spacemit->hub_mutex);
	seq_puts(s, spacemit->suspend_power_on ? "true\n" : "false\n");
	mutex_unlock(&spacemit->hub_mutex);
	return 0;
}

static ssize_t spacemit_hub_suspend_write(struct file *file,
					   const char __user *ubuf, size_t count,
					   loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct spacemit_hub_priv *spacemit = s->private;
	bool on = false;
	char buf[32];

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if ((!strncmp(buf, "true", 4)) || (!strncmp(buf, "1", 1)))
		on = true;
	if ((!strncmp(buf, "false", 5)) || !strncmp(buf, "0", 1))
		on = false;

	mutex_lock(&spacemit->hub_mutex);
	spacemit->suspend_power_on = on;
	mutex_unlock(&spacemit->hub_mutex);

	return count;
}

static int spacemit_hub_suspend_open(struct inode *inode, struct file *file)
{
	return single_open(file, spacemit_hub_suspend_show, inode->i_private);
}

struct file_operations spacemit_hub_suspend_fops = {
	.open = spacemit_hub_suspend_open,
	.write = spacemit_hub_suspend_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void spacemit_hub_debugfs_init(struct spacemit_hub_priv *spacemit)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(spacemit->dev), usb_debug_root);
	debugfs_create_file("vbus_on", 0644, root, spacemit,
				&spacemit_hub_vbus_fops);
	debugfs_create_file("hub_on", 0644, root, spacemit,
				&spacemit_hub_enable_fops);
	debugfs_create_file("suspend_power_on", 0644, root, spacemit,
				&spacemit_hub_suspend_fops);
}
