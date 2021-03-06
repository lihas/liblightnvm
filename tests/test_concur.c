#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <liblightnvm.h>
#include <pthread.h>

#include <CUnit/Basic.h>

static char nvm_dev_name[DISK_NAME_LEN] = "nvme0n1";

struct context {
	NVM_VBLOCK blk;
	NVM_GEO geo;
	NVM_DEV dev;
	char *buf;
};

static void *write_thread(void *priv)
{
	struct context *ctx = priv;
	int i;

	for (i = 0; i < ctx->geo.npages; i++) {
		size_t count = nvm_vblock_pwrite(ctx->blk, ctx->buf, 1, i);
		CU_ASSERT(count);
	}
	pthread_exit(NULL);
}

static void *erase_thread(void *priv)
{
	struct context *ctx = priv;
	int i;

	for (i = 0; i < 4; i++) {
		int err;
		usleep(2000);
		err = nvm_vblock_erase(ctx->blk);/* ERASE */

		CU_ASSERT(!err);
	}

	pthread_exit(NULL);
}

#define NUM_BLOCKS (2)
void test_VBLOCK_CONCUR(void)
{
	NVM_VBLOCK vblock[2];
	NVM_DEV dev;
	NVM_GEO geo;
	int ret, i;
	struct context ctx[2];
	char *wbuf;
	pthread_t wr_th, er_th;

	dev = nvm_dev_open(nvm_dev_name);
	CU_ASSERT(dev > 0);

	geo = nvm_dev_get_geo(dev);

	for (i = 0; i < NUM_BLOCKS; i++) {
		vblock[i] = nvm_vblock_new();
		ret = nvm_vblock_gets(vblock[i], dev, 0, 0);
		CU_ASSERT(!ret);
		//nvm_vblock_pr(vblock[i]);
	}

	ret = posix_memalign((void**)&wbuf, geo.nbytes, geo.vpage_nbytes);
	CU_ASSERT(!ret);
	if (ret) {
		printf("Failed allocating write buffer(%p)\n", wbuf);
		return;
	}
	memset(wbuf, 0, geo.vpage_nbytes);

	ctx[0].blk = vblock[0];
	ctx[0].dev = dev;
	ctx[0].buf = wbuf;
	ctx[0].geo = geo;

	ctx[1].blk = vblock[1];
	ctx[1].dev = dev;
	ctx[1].buf = wbuf;
	ctx[1].geo = geo;

	if (pthread_create(&wr_th, NULL, write_thread, &ctx[0])) {
		fprintf(stderr, "fail...\n");
		return;
	}

	if (pthread_create(&er_th, NULL, erase_thread, &ctx[1])) {
		fprintf(stderr, "fail2...\n");
		return;
	}

	pthread_join(wr_th, NULL);
	pthread_join(er_th, NULL);

	for (i = 0; i < geo.npages; i++) {
		int count = nvm_vblock_pread(vblock[0], wbuf, 1, i);	/* READ */
		CU_ASSERT(count);
		if (!count)
			printf("FAILED count(%d) i(%d), wbuf(%s)\n",
				count, i, wbuf);
	}

	for (i = 0; i < NUM_BLOCKS; i++) {
		ret = nvm_vblock_put(vblock[i]);
		CU_ASSERT(!ret);

		nvm_vblock_free(&vblock[i]);
	}

	nvm_dev_close(dev);
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		if (strlen(argv[1]) > DISK_NAME_LEN) {
			printf("Argument nvm_dev can be maximum %d characters\n",
				DISK_NAME_LEN - 1);
		}
		strcpy(nvm_dev_name, argv[1]);
	}

	CU_pSuite pSuite = NULL;

	if (CUE_SUCCESS != CU_initialize_registry())
		return CU_get_error();

	pSuite = CU_add_suite("nvm_vblock*", NULL, NULL);
	if (NULL == pSuite) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (
	(NULL == CU_add_test(pSuite, "nvm_concur_write-erase", test_VBLOCK_CONCUR)) ||
	0)
	{
		CU_cleanup_registry();
		return CU_get_error();
	}

	/* Run all tests using the CUnit Basic interface */
	CU_basic_set_mode(CU_BRM_SILENT);
	CU_basic_run_tests();
	CU_cleanup_registry();

	return CU_get_error();
}
