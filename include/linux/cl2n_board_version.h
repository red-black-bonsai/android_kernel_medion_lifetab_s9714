/*
 *	include/linux/cl2n_board_version.h
 */

#ifndef _LINUX_CL2N_BOARD_VERSION_H
#define _LINUX_CL2N_BOARD_VERSION_H

// arch/arm/mach-tegra/board.h
extern int cl2n_get_board_strap(void);

enum cl2n_board_version
{
  /* A2110 Board Version */
	CL2N_BOARD_VER_DVT    = 0x0,
	CL2N_BOARD_VER_PVT    = 0x1,
	CL2N_BOARD_VER_MP     = 0x2,
	CL2N_BOARD_VER_DVT2   = 0x4,
};

#endif
