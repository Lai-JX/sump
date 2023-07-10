#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <spdk/bdev.h>
#include <spdk/bdev_module.h>
#include <spdk/string.h>
#include <stdlib.h>
int split_io_request(struct spdk_bdev_io *, struct spdk_bdev_io *, struct spdk_bdev_io *);