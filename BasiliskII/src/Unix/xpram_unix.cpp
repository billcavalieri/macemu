/*
 *  xpram_unix.cpp - XPRAM handling, Unix specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include <string>
using std::string;

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "xpram.h"


// A library virtual machine's own folder (--vm-dir): its NVRAM goes there instead of the shared home-folder file
// (used by the non-Linux code below; set by SheepShaver's main_unix.cpp, unused by Basilisk II).
const char *xpram_dir_override = NULL;

#ifdef __linux__


// XPRAM file name, set by LoadPrefs() in prefs_unix.cpp
string xpram_name;

/*
 *  Load XPRAM from settings file
 */

void LoadXPRAM(const char* vmdir)
{
	assert(!xpram_name.empty());
	int fd;
	if ((fd = open(xpram_name.c_str(), O_RDONLY)) >= 0)
	{
		read(fd, XPRAM, XPRAM_SIZE);
		close(fd);
	}
}

/*
 *  Save XPRAM to settings file
 */

void SaveXPRAM(void)
{
	assert(!xpram_name.empty());
	int fd;
	if ((fd = open(xpram_name.c_str(), O_WRONLY | O_CREAT, 0666)) >= 0)
	{
		write(fd, XPRAM, XPRAM_SIZE);
		close(fd);
	}
	else
	{
		fprintf(stderr, "WARNING: Unable to save %s (%s)\n",
		        xpram_name.c_str(), strerror(errno));
	}
}

/*
 *  Delete PRAM file
 */

void ZapPRAM(void)
{
	// Delete file
	assert(!xpram_name.empty());
	unlink(xpram_name.c_str());
}


/*
 *  Path of the XPRAM file (valid after LoadPrefs())
 */

const char *XPRAMFilePath(void)
{
	return xpram_name.c_str();
}


#else	// __linux__


// XPRAM file name and path
#if POWERPC_ROM
const char XPRAM_FILE_NAME[] = ".sheepshaver_nvram";
#else
const char XPRAM_FILE_NAME[] = ".basilisk_ii_xpram";
#endif
static char xpram_path[1024];

// First start of a VM in its own folder: begin from the shared file, so the VM keeps what it had before each VM
// got its own (the NVRAM and, for the New World ROM, the boot flash next to it).
static void migrate_shared_nvram(const char *to)
{
	struct stat st;
	char legacy[1100], dest[1100];
	const char *home = getenv("HOME");
	if (!home || strlen(home) > 900 || stat(to, &st) == 0)
		return;
	for (int flash = 0; flash < 2; flash++) {
		snprintf(legacy, sizeof(legacy), "%s/%s%s", home, XPRAM_FILE_NAME, flash ? ".flash" : "");
		snprintf(dest, sizeof(dest), "%s%s", to, flash ? ".flash" : "");
		int in = open(legacy, O_RDONLY);
		if (in < 0)
			continue;
		int out = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (out >= 0) {
			printf("NVRAM: starting from the shared file %s\n", legacy);
			char buffer[4096];
			ssize_t n;
			while ((n = read(in, buffer, sizeof(buffer))) > 0)
				write(out, buffer, n);
			close(out);
		}
		close(in);
	}
}


/*
 *  Load XPRAM from settings file
 */

void LoadXPRAM(const char *vmdir)
{
	bool own_folder = false;
	if (!vmdir && xpram_dir_override) {
		vmdir = xpram_dir_override;
		own_folder = true;
	}
	if (vmdir) {
#if POWERPC_ROM
		snprintf(xpram_path, sizeof(xpram_path), "%s/nvram", vmdir);
#else
		snprintf(xpram_path, sizeof(xpram_path), "%s/xpram", vmdir);
#endif
	} else {
		// Construct XPRAM path
		xpram_path[0] = 0;
		char *home = getenv("HOME");
		if (home != NULL && strlen(home) < 1000) {
			strncpy(xpram_path, home, 1000);
			strcat(xpram_path, "/");
		}
		strcat(xpram_path, XPRAM_FILE_NAME);
	}

	if (own_folder)
		migrate_shared_nvram(xpram_path);

	// Load XPRAM from settings file
	int fd;
	if ((fd = open(xpram_path, O_RDONLY)) >= 0) {
		read(fd, XPRAM, XPRAM_SIZE);
		close(fd);
	}
}


/*
 *  Save XPRAM to settings file
 */

void SaveXPRAM(void)
{
	int fd;
	if ((fd = open(xpram_path, O_WRONLY | O_CREAT, 0666)) >= 0) {
		write(fd, XPRAM, XPRAM_SIZE);
		close(fd);
	}
}


/*
 *  Delete PRAM file
 */

void ZapPRAM(void)
{
	if (xpram_dir_override) {
		// A library VM's own file (xpram_path is already set by LoadXPRAM)
		unlink(xpram_path);
		return;
	}

	// Construct PRAM path
	xpram_path[0] = 0;
	char *home = getenv("HOME");
	if (home != NULL && strlen(home) < 1000) {
		strncpy(xpram_path, home, 1000);
		strcat(xpram_path, "/");
	}
	strcat(xpram_path, XPRAM_FILE_NAME);

	// Delete file
	unlink(xpram_path);
}


/*
 *  Path of the XPRAM file (valid after LoadXPRAM())
 */

const char *XPRAMFilePath(void)
{
	return xpram_path;
}


#endif	// __linux__

