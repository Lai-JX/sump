struct nvme_ctrlr_opts {
	uint32_t prchk_flags;
	int32_t ctrlr_loss_timeout_sec;
	uint32_t reconnect_delay_sec;
	uint32_t fast_io_fail_timeout_sec;
	bool from_discovery_service;
};
struct nvme_ctrlr;
typedef void (*bdev_nvme_reset_cb)(void *cb_arg, bool success);
typedef void (*nvme_ctrlr_disconnected_cb)(struct nvme_ctrlr *nvme_ctrlr);
struct nvme_bdev_ctrlr {
	char				*name;
	TAILQ_HEAD(, nvme_ctrlr)	ctrlrs;
	TAILQ_HEAD(, nvme_bdev)		bdevs;
	TAILQ_ENTRY(nvme_bdev_ctrlr)	tailq;
};
struct nvme_ctrlr {
	/**
	 * points to pinned, physically contiguous memory region;
	 * contains 4KB IDENTIFY structure for controller which is
	 *  target for CONTROLLER IDENTIFY command during initialization
	 */
	struct spdk_nvme_ctrlr			*ctrlr;
	struct nvme_path_id			*active_path_id;
	int					ref;

	uint32_t				resetting : 1;
	uint32_t				reconnect_is_delayed : 1;
	uint32_t				fast_io_fail_timedout : 1;
	uint32_t				destruct : 1;
	uint32_t				ana_log_page_updating : 1;
	uint32_t				io_path_cache_clearing : 1;

	struct nvme_ctrlr_opts			opts;

	RB_HEAD(nvme_ns_tree, nvme_ns)		namespaces;

	struct spdk_opal_dev			*opal_dev;

	struct spdk_poller			*adminq_timer_poller;
	struct spdk_thread			*thread;

	bdev_nvme_reset_cb			reset_cb_fn;
	void					*reset_cb_arg;
	/* Poller used to check for reset/detach completion */
	struct spdk_poller			*reset_detach_poller;
	struct spdk_nvme_detach_ctx		*detach_ctx;

	uint64_t				reset_start_tsc;
	struct spdk_poller			*reconnect_delay_timer;

	nvme_ctrlr_disconnected_cb		disconnected_cb;

	/** linked list pointer for device list */
	TAILQ_ENTRY(nvme_ctrlr)			tailq;
	struct nvme_bdev_ctrlr			*nbdev_ctrlr;

	TAILQ_HEAD(nvme_paths, nvme_path_id)	trids;

	uint32_t				max_ana_log_page_size;
	struct spdk_nvme_ana_page		*ana_log_page;
	struct spdk_nvme_ana_group_descriptor	*copied_ana_desc;

	struct nvme_async_probe_ctx		*probe_ctx;

	pthread_mutex_t				mutex;
	uint32_t size;		// 修改ljx
};