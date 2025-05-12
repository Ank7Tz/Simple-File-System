/*
 * file:        testing.c
 * description: libcheck test skeleton for file system project
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <zlib.h>
#include <fuse.h>
#include <stdlib.h>
#include <errno.h>


/* change test name and make it do something useful */
START_TEST(a_test)
{
    ck_assert_int_eq(1, 1);
}
END_TEST

/* this is an example of a callback function for readdir
 */
int empty_filler(void *ptr, const char *name, const struct stat *stbuf,
                 off_t off)
{
    /* FUSE passes you the entry name and a pointer to a 'struct stat' 
     * with the attributes. Ignore the 'ptr' and 'off' arguments 
     * 
     */
    return 0;
}

/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */

extern struct fuse_operations fs_ops;
extern void block_init(char *file);

struct fs_getattr_test_t {
    const char *path;
    int uid;
    int gid;
    int mode;
    int size;
    int ctime;
    int mtime;
    int atime;
} fs_getattr_test_data[] = {
    {"/", 0, 0, 040777, 4096, 1565283152, 1565283167, 1565283167},
    {"/file.1k", 500, 500, 0100666, 1000, 1565283152, 1565283152, 1565283152},
    {"/file.10", 500, 500, 0100666, 10, 1565283152, 1565283167, 1565283167},
    {"/dir-with-long-name", 0, 0, 040777, 4096, 1565283152, 1565283167, 1565283167},
    {"/dir-with-long-name/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167, 1565283167},
    {"/dir2", 500, 500, 040777, 8192, 1565283152, 1565283167, 1565283167},
    {"/dir2/twenty-seven-byte-file-name", 500, 500, 0100666, 1000, 1565283152, 1565283167, 1565283167},
    {"/dir2/file.4k+", 500, 500, 0100777, 4098, 1565283152, 1565283167, 1565283167},
    {"/dir3", 0, 500, 040777, 4096, 1565283152, 1565283167, 1565283167},
    {"/dir3/subdir", 0, 500, 040777, 4096, 1565283152, 1565283167, 1565283167},
    {"/dir3/subdir/file.4k-", 500, 500, 0100666, 4095, 1565283152, 1565283167, 1565283167},
    {"/dir3/subdir/file.8k-", 500, 500, 0100666, 8190, 1565283152, 1565283167, 1565283167},
    {"/dir3/subdir/file.12k", 500, 500, 0100666, 12288, 1565283152, 1565283167, 1565283167},
    {"/dir3/file.12k-", 0, 500, 0100777, 12287, 1565283152, 1565283167, 1565283167},
    {"/file.8k+", 500, 500, 0100666, 8195, 1565283152, 1565283167, 1565283167}
};

START_TEST(fs_getattr_tests)
{
    struct stat *sb;
    int n = sizeof(fs_getattr_test_data) / sizeof(struct fs_getattr_test_t);
    for (int i = 0; i < n; i++) {
        sb = malloc(sizeof(struct stat));
        fs_ops.getattr(fs_getattr_test_data[i].path, sb);
        
        ck_assert_msg(fs_getattr_test_data[i].uid == sb->st_uid, 
                     "Test failed at index %d: uid mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].uid, sb->st_uid);
        
        ck_assert_msg(fs_getattr_test_data[i].gid == sb->st_gid,
                     "Test failed at index %d: gid mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].gid, sb->st_gid);
        
        ck_assert_msg(fs_getattr_test_data[i].mode == sb->st_mode,
                     "Test failed at index %d: mode mismatch - expected %o, got %o", 
                     i, fs_getattr_test_data[i].mode, sb->st_mode);
        
        ck_assert_msg(fs_getattr_test_data[i].size == sb->st_size,
                     "Test failed at index %d: size mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].size, (int) sb->st_size);
        
        ck_assert_msg(fs_getattr_test_data[i].ctime == (int) sb->st_ctim.tv_sec,
                     "Test failed at index %d: ctime mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].ctime, (int) sb->st_ctim.tv_sec);
        
        ck_assert_msg(fs_getattr_test_data[i].mtime == (int) sb->st_mtim.tv_sec,
                     "Test failed at index %d: mtime mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].mtime, (int) sb->st_mtim.tv_sec);
        
        ck_assert_msg(fs_getattr_test_data[i].atime == (int) sb->st_atim.tv_sec,
                     "Test failed at index %d: atime mismatch - expected %d, got %d", 
                     i, fs_getattr_test_data[i].atime, (int) sb->st_atim.tv_sec);
        
        free(sb);
    }
}
END_TEST

struct readdir_test_t {
    char *name;
    int seen;
};

struct readdir_test_t readdir_test_table[] = {
    {"file.1k", 0},
    {"file.10", 0},
    {"dir-with-long-name", 0},
    {"dir2", 0},
    {"dir3", 0},
    {"file.4k-", 0},
    {"file.8k-", 0},
    {"file.12k", 0},
    {"file.8k+", 0},
    {"subdir", 0},
    {"twenty-seven-byte-file-name", 0},
    {"file.4k+", 0},
    {"file.12k+", 0},
    {"dir-with-long-name", 0}
};

int readdir_test_count = 1;
struct readdir_test_data_t {
    char *path;
    char **ans;
    int count;
} readdir_test_data[] = {
    {
        "/", 
        (char*[]){"file.1k", "file.10", "dir-with-long-name", "dir2", "dir3", "file.8k+"}, 
        6
    },
    {
        "/dir2", 
        (char*[]){"twenty-seven-byte-file-name", "file.4k+"}, 
        2
    }
};

int test_filler(void *ptr, const char *name, const struct stat *st, off_t off)
{
    int n = sizeof(readdir_test_table) / sizeof(struct readdir_test_t);
    for (int i = 0; i < n; i++) {
        if (strcmp(name, readdir_test_table[i].name) == 0) {
            if (readdir_test_table[i].seen != 1) {
                readdir_test_table[i].seen = 1;
                return 0;
            } else {
                return -1; // Already seen.
            }
        }
    }
    return 0;
}

void reset_readdir_table()
{
    int n = sizeof(readdir_test_table) / sizeof(struct readdir_test_t);
    for (int i = 0; i < n; i++) {
        readdir_test_table[i].seen = 0;
    }
}

int verify_readdir_table(int index)
{
    int n = sizeof(readdir_test_table) / sizeof(struct readdir_test_t);
    for (int i = 0; i < readdir_test_data[index].count; i++) {
        char *ans = readdir_test_data[index].ans[i];
        for (int j = 0; j < n; j++) {
            if (strcmp(ans, readdir_test_table[j].name) == 0) {
                if (readdir_test_table[j].seen == 1) {
                    readdir_test_table[j].seen = 0;
                    break;
                } else {
                    return -1;
                }
            }
        }
    }
    return 0;
}

START_TEST(fs_readdir_tests)
{
    for (int i = 0; i < readdir_test_count; i++) {
        fs_ops.readdir(readdir_test_data[i].path, NULL, test_filler, 0, NULL);
        ck_assert_int_eq(verify_readdir_table(i), 0);
        reset_readdir_table();
    }
}
END_TEST

int main(int argc, char **argv)
{
    block_init("test.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("read_mostly");

    tcase_add_test(tc, a_test); /* see START_TEST above */
    /* add more tests here */

    tcase_add_test(tc, fs_getattr_tests);
    tcase_add_test(tc, fs_readdir_tests);

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
