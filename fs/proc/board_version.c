#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/io.h>

#include <linux/cl2n_board_version.h>

static int board_version_proc_show(struct seq_file *m, void *v)
{
	int board_strap;

	board_strap = cl2n_get_board_strap();
	switch(board_strap)
	{
        /* A2110 Board Version */
		case CL2N_BOARD_VER_DVT: seq_printf(m, "DVT"); break;
		case CL2N_BOARD_VER_DVT2:seq_printf(m, "DVT2");break;
		case CL2N_BOARD_VER_PVT: seq_printf(m, "PVT"); break;
		case CL2N_BOARD_VER_MP:  seq_printf(m, "MP"); break;
		default:
			seq_printf(m, "UNKNOWN");
			break;
	}
	return 0;
}

static int board_version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, board_version_proc_show, NULL);
}

static const struct file_operations board_version_proc_fops = {
	.open		= board_version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_board_version_init(void)
{
	proc_create("board_version", 0, NULL, &board_version_proc_fops);
	return 0;
}
module_init(proc_board_version_init);
