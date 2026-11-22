#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <Drivers.h>
#include <StorageDefs.h>

static void dump_dev_size(int dev)
{
	size_t sz;
	if (ioctl(dev, B_GET_DEVICE_SIZE, &sz, sizeof(sz)) < 0) {
		perror("ioctl(B_GET_DEVICE_SIZE)");
		return;
	}
	printf("size: %" B_PRIuSIZE " bytes\n", sz);
}

static void dump_bios_id(int dev)
{
	uint8 id;
	if (ioctl(dev, B_GET_BIOS_DRIVE_ID, &id, sizeof(id)) < 0) {
		perror("ioctl(B_GET_BIOS_DRIVE_ID)");
		return;
	}
	printf("bios id: %d, 0x%x\n", id, id);
}

static void dump_media_status(int dev)
{
	uint32 st;
	if (ioctl(dev, B_GET_MEDIA_STATUS, &st, sizeof(st)) < 0) {
		perror("ioctl(B_GET_MEDIA_STATUS)");
		return;
	}
	printf("media status: %s\n", strerror(st));
}

static const char *device_type(uint32 type)
{
	if (type == B_DISK) return "disk";
	if (type == B_TAPE) return "tape";
	if (type == B_PRINTER) return "printer";
	if (type == B_CPU) return "cpu";
	if (type == B_WORM) return "worm";
	if (type == B_CD) return "cd";
	if (type == B_SCANNER) return "scanner";
	if (type == B_OPTICAL) return "optical";
	if (type == B_JUKEBOX) return "jukebox";
	if (type == B_NETWORK) return "network";
	return "<unknown>";
}

static void dump_geom(int dev, bool bios)
{
	device_geometry geom;
	
	if (ioctl(dev, bios?B_GET_BIOS_GEOMETRY:B_GET_GEOMETRY, &geom, sizeof(geom)) < 0) {
		perror(bios ? "ioctl(B_GET_BIOS_GEOMETRY)" : "ioctl(B_GET_GEOMETRY)");
		return;
	}
	printf("%sgeometry:\n", bios?"bios ":"");
	printf("bytes_per_sector:\t%" B_PRIu32 "\n", geom.bytes_per_sector);
	printf("sectors_per_track:\t%" B_PRIu32 "\n", geom.sectors_per_track);
	printf("cylinder_count:\t%" B_PRIu32 "\n", geom.cylinder_count);
	printf("head_count:\t%" B_PRIu32 "\n", geom.head_count);
	printf("device_type:\t%d, %s\n", geom.device_type, device_type(geom.device_type));
	printf("%sremovable.\n", geom.removable?"":"not ");
	printf("%sread_only.\n", geom.read_only?"":"not ");
	printf("%swrite_once.\n", geom.write_once?"":"not ");
}

static void dump_partition(int dev)
{
	partition_info partition;
	
	if (ioctl(dev, B_GET_PARTITION_INFO, &partition, sizeof(partition)) < 0) {
		perror("ioctl(B_GET_PARTITION_INFO)");
		return;
	}
	printf("partition:\n");
	printf("offset:\t%" B_PRIdOFF "\n", partition.offset);
	printf("size:\t%" B_PRIdOFF "\n", partition.size);
	printf("logical_block_size:\t%" B_PRId32 "\n", partition.logical_block_size);
	printf("session:\t%" B_PRId32 "\n", partition.session);
	printf("partition:\t%" B_PRId32 "\n", partition.partition);
	printf("device:\t%s\n", partition.device);
}

static void dump_misc(int dev)
{
	char path[B_PATH_NAME_LENGTH];
	if (ioctl(dev, B_GET_DRIVER_FOR_DEVICE, path, sizeof(path)) < 0) {
		perror("ioctl(B_GET_DRIVER_FOR_DEVICE)");
	} else {
		printf("driver:\t%s\n", path);
	}
#ifdef __VOS__
	if (ioctl(dev, B_GET_PATH_FOR_DEVICE, path, sizeof(path)) < 0) {
		perror("ioctl(B_GET_PATH_FOR_DEVICE)");
	} else {
		printf("device path:\t%s\n", path);
	}
#endif
}

#ifdef __linux__
/* Linux: the block device ioctls and sysfs replace the Haiku driver
 * ioctls (B_GET_DEVICE_SIZE, B_GET_GEOMETRY, B_GET_PARTITION_INFO, ...),
 * which Linux block drivers do not implement. */
#include <libgen.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static int read_sysfs(const char *dir, const char *name, char *buf, size_t size)
{
	char path[B_PATH_NAME_LENGTH];
	FILE *f;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "r");
	if (f == NULL)
		return -1;
	if (fgets(buf, size, f) == NULL) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = '\0';
	return 0;
}

static int linux_driveinfo(int dev)
{
	unsigned long long size;
	int sectorSize, readOnly;
	struct hd_geometry geo;
	struct stat st;
	char sys[B_PATH_NAME_LENGTH], value[256], link[B_PATH_NAME_LENGTH];
	ssize_t len;

	if (fstat(dev, &st) < 0 || !S_ISBLK(st.st_mode)) {
		fprintf(stderr, "not a block device\n");
		return 1;
	}
	if (ioctl(dev, BLKGETSIZE64, &size) < 0)
		perror("ioctl(BLKGETSIZE64)");
	else
		printf("size: %llu bytes\n", size);
	puts("");

	printf("geometry:\n");
	if (ioctl(dev, BLKSSZGET, &sectorSize) == 0)
		printf("bytes_per_sector:\t%d\n", sectorSize);
	if (ioctl(dev, HDIO_GETGEO, &geo) == 0) {
		printf("sectors_per_track:\t%u\n", geo.sectors);
		printf("cylinder_count:\t%u\n", geo.cylinders);
		printf("head_count:\t%u\n", geo.heads);
	}
	if (ioctl(dev, BLKROGET, &readOnly) == 0)
		printf("%sread_only.\n", readOnly ? "" : "not ");

	snprintf(sys, sizeof(sys), "/sys/dev/block/%u:%u", major(st.st_rdev),
		minor(st.st_rdev));
	if (read_sysfs(sys, "removable", value, sizeof(value)) == 0)
		printf("%sremovable.\n", strcmp(value, "1") == 0 ? "" : "not ");
	else {
		/* a partition: the flag is on the parent disk */
		char parent[B_PATH_NAME_LENGTH];
		snprintf(parent, sizeof(parent), "%s/..", sys);
		if (read_sysfs(parent, "removable", value, sizeof(value)) == 0)
			printf("%sremovable.\n", strcmp(value, "1") == 0 ? "" : "not ");
	}
	puts("");

	if (read_sysfs(sys, "partition", value, sizeof(value)) == 0) {
		printf("partition:\n");
		if (read_sysfs(sys, "start", value, sizeof(value)) == 0)
			printf("offset:\t%llu\n", strtoull(value, NULL, 10) * 512);
		printf("size:\t%llu\n", size);
		if (read_sysfs(sys, "partition", value, sizeof(value)) == 0)
			printf("partition:\t%s\n", value);
		puts("");
	}

	snprintf(value, sizeof(value), "%s/device/driver", sys);
	len = readlink(value, link, sizeof(link) - 1);
	if (len < 0) {
		snprintf(value, sizeof(value), "%s/../device/driver", sys);
		len = readlink(value, link, sizeof(link) - 1);
	}
	if (len >= 0) {
		link[len] = '\0';
		printf("driver:\t%s\n", basename(link));
	}
	len = readlink(sys, link, sizeof(link) - 1);
	if (len >= 0) {
		link[len] = '\0';
		printf("device path:\t%s\n", link);
	}
	puts("");
	return 0;
}
#endif

int main(int argc, char **argv)
{
	int dev;
	if (argc < 2) {
		printf("%s device\n", argv[0]);
		return 1;
	}
	dev = open(argv[1], O_RDONLY);
	if (dev < 0) {
		perror("open");
		return 1;
	}
#ifdef __linux__
	return linux_driveinfo(dev);
#endif
	dump_dev_size(dev);
	puts("");
	dump_bios_id(dev);
	puts("");
	dump_media_status(dev);
	puts("");
	dump_geom(dev, false);
	puts("");
	dump_geom(dev, true);
	puts("");
	dump_partition(dev);
	puts("");
	dump_misc(dev);
	puts("");
	return 0;
}
