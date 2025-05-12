/*
 * file:        unittest-2.c
 * description: libcheck test skeleton, part 2
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

/* mockup for fuse_get_context. you can change ctx.uid, ctx.gid in 
 * tests if you want to test setting UIDs in mknod/mkdir
 */
struct fuse_context ctx = { .uid = 500, .gid = 500};
struct fuse_context *fuse_get_context(void)
{
    return &ctx;
}

#define FS_BLOCK_SIZE 4096

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

START_TEST(fs_create_file_basic_test)
{
    mode_t mode = S_IFREG | 0777;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *name = "/new_write.txt";
    
    int r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    // try to create again, error = -EEXIST
    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, -EEXIST);
}
END_TEST

START_TEST(fs_mkdir_basic_test)
{
    mode_t mode = 0777;
    const char *name = "/new_folder";
    
    int r = fs_ops.mkdir(name, mode);
    ck_assert_int_eq(r, 0);

    // try to create again, error = -EEXIST
    r = fs_ops.mkdir(name, mode);
    ck_assert_int_eq(r, -EEXIST);
}
END_TEST

START_TEST(fs_mkdir_and_create_tests)
{
    mode_t mode = 0777;
    const char *name = "/new_folder/another_newfolder";
    
    int r = fs_ops.mkdir(name, mode);
    ck_assert_int_eq(r, 0);

    // try to create again, error = -EEXIST
    r = fs_ops.mkdir(name, mode);
    ck_assert_int_eq(r, -EEXIST);

    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));

    // try to create a file inside new_folder
    name = "/new_folder/hello.c";
    mode = S_IFREG | 0777;
    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, -EEXIST);

    name = "/new_folder/testing.c";
    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    name = "/new_folder/unit.bin.xyz";
    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    name = "/new_folder/another_newfolder/world.c";
    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, -EEXIST);
}
END_TEST


START_TEST(fs_unlink_test)
{
    const char *name = "/new_write.txt";
    
    int r = fs_ops.unlink(name);
    ck_assert_int_eq(r, 0);

    // clear the directory content.
    r = fs_ops.unlink(name);
    ck_assert_int_eq(r, -ENOENT);

    // delete inside a directory.
    name = "/new_folder/testing.c";
    r = fs_ops.unlink(name);
    ck_assert_int_eq(r, 0);

    r = fs_ops.unlink(name);
    ck_assert_int_eq(r, -ENOENT);

    // try to delete a directory.
    name = "/new_folder";
    r = fs_ops.unlink(name);
    ck_assert_int_eq(r, -EISDIR);
}
END_TEST

START_TEST(fs_rmdir_test)
{
    const char *name = "/new_folder/another_newfolder";
 
    // dir not empty.
    int r = fs_ops.rmdir(name);
    ck_assert_int_eq(r, -ENOTEMPTY);

    name = "/new_folder/another_newfolder/world.c";
    r = fs_ops.unlink(name);
    ck_assert_int_eq(r, 0);

    name = "/new_folder/another_newfolder";
    r = fs_ops.rmdir(name);
    ck_assert_int_eq(r, 0);
}
END_TEST

START_TEST(fs_rename_test)
{
    mode_t mode = S_IFREG | 0777;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *name = "/file_for_rename_test.test";
    
    int r = fs_ops.create(name, mode, mock_file_info);
    ck_assert_int_eq(r, 0);

    const char *re_named = "/file_after_reanme.test";
    r = fs_ops.rename(name, re_named);

    ck_assert_int_eq(r, 0);

    struct stat *sb;
    sb = malloc(sizeof(struct stat));

    fs_ops.getattr(re_named, sb);

    ck_assert_ptr_nonnull(sb);
}
END_TEST

START_TEST(fs_chmod_test)
{
    mode_t create_mode = S_IFREG | 0644;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *filename = "/chmod_test.txt";
    
    int r = fs_ops.create(filename, create_mode, mock_file_info);
    ck_assert_int_eq(r, 0);
    
    // Verify initial mode
    struct stat sb;
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mode & 0777, 0644);
    
    // Change permissions to 0755
    r = fs_ops.chmod(filename, 0755);
    ck_assert_int_eq(r, 0);
    
    // Verify mode was changed
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mode & 0777, 0755);
    
    // Check still a regular file
    ck_assert(S_ISREG(sb.st_mode));
    
    // Try chmod on non-existent file
    r = fs_ops.chmod("/nonexistent.txt", 0777);
    ck_assert_int_eq(r, -ENOENT);
    
    // Test changing mode of a directory
    const char *dirname = "/chmod_test_dir";
    
    // Create test directory with mode 0700
    r = fs_ops.mkdir(dirname, 0700);
    ck_assert_int_eq(r, 0);
    
    // Verify initial mode
    r = fs_ops.getattr(dirname, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mode & 0777, 0700);
    
    // Change permissions to 0755
    r = fs_ops.chmod(dirname, 0755);
    ck_assert_int_eq(r, 0);
    
    // Verify mode was changed
    r = fs_ops.getattr(dirname, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mode & 0777, 0755);
    
    // Check that file is still a directory
    ck_assert(S_ISDIR(sb.st_mode));
    
    // Clean up
    r = fs_ops.unlink(filename);
    ck_assert_int_eq(r, 0);
    r = fs_ops.rmdir(dirname);
    ck_assert_int_eq(r, 0);
    
    free(mock_file_info);
}
END_TEST

START_TEST(fs_utime_test)
{
    mode_t create_mode = S_IFREG | 0644;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *filename = "/utime_test.txt";
    
    int r = fs_ops.create(filename, create_mode, mock_file_info);
    ck_assert_int_eq(r, 0);
    
    // Verify initial mtime
    struct stat sb;
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    time_t initial_mtime = sb.st_mtime;
    
    // Set new times
    struct utimbuf ut;
    ut.modtime = 67890;
    
    // Change times
    r = fs_ops.utime(filename, &ut);
    ck_assert_int_eq(r, 0);
    
    // Verify times were changed
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mtime, 67890);
    ck_assert_int_ne(sb.st_mtime, initial_mtime);
    
    // Try utime on non-existent file
    r = fs_ops.utime("/nonexistent.txt", &ut);
    ck_assert_int_eq(r, -ENOENT);
    
    // Test utime on a directory
    const char *dirname = "/utime_test_dir";
    
    // Create test directory
    r = fs_ops.mkdir(dirname, 0755);
    ck_assert_int_eq(r, 0);
    
    // Verify initial mtime
    r = fs_ops.getattr(dirname, &sb);
    ck_assert_int_eq(r, 0);
    initial_mtime = sb.st_mtime;
    
    // Set new times
    ut.modtime = 13579;
    
    // Change times
    r = fs_ops.utime(dirname, &ut);
    ck_assert_int_eq(r, 0);
    
    // Verify times were changed
    r = fs_ops.getattr(dirname, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_mtime, 13579);
    ck_assert_int_ne(sb.st_mtime, initial_mtime);
    
    // Clean up
    r = fs_ops.unlink(filename);
    ck_assert_int_eq(r, 0);
    r = fs_ops.rmdir(dirname);
    ck_assert_int_eq(r, 0);
    
    free(mock_file_info);
}
END_TEST

START_TEST(fs_truncate_test)
{
    mode_t create_mode = S_IFREG | 0644;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *filename = "/truncate_test.txt";
    
    int r = fs_ops.create(filename, create_mode, mock_file_info);
    ck_assert_int_eq(r, 0);
    
    // Verify file exists
    struct stat sb;
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    
    // Try to truncate with len > 0, should return -EINVAL
    r = fs_ops.truncate(filename, 10);
    ck_assert_int_eq(r, -EINVAL);
    
    // Truncate file to 0 bytes
    r = fs_ops.truncate(filename, 0);
    ck_assert_int_eq(r, 0);
    
    // Verify file size is now 0
    r = fs_ops.getattr(filename, &sb);
    ck_assert_int_eq(r, 0);
    ck_assert_int_eq(sb.st_size, 0);
    
    // Try truncate on non-existent file
    r = fs_ops.truncate("/nonexistent.txt", 0);
    ck_assert_int_eq(r, -ENOENT);
    
    // Try truncate on a directory
    const char *dirname = "/truncate_test_dir";
    
    // Create test directory
    r = fs_ops.mkdir(dirname, 0755);
    ck_assert_int_eq(r, 0);
    
    // Try to truncate directory, should return -EISDIR
    r = fs_ops.truncate(dirname, 0);
    ck_assert_int_eq(r, -EISDIR);
    
    // Clean up
    r = fs_ops.unlink(filename);
    ck_assert_int_eq(r, 0);
    r = fs_ops.rmdir(dirname);
    ck_assert_int_eq(r, 0);
    
    free(mock_file_info);
}
END_TEST

START_TEST(fs_read_test)
{
    mode_t create_mode = S_IFREG | 0644;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    const char *filename = "/read_test.txt";
    
    int r = fs_ops.create(filename, create_mode, mock_file_info);
    ck_assert_int_eq(r, 0);
    
    size_t buf_size = FS_BLOCK_SIZE * 3;
    char *write_buf = malloc(buf_size);
    char *read_buf = malloc(buf_size);
    
    // Fill write buffer with recognizable pattern
    for (size_t i = 0; i < buf_size; i++) {
        write_buf[i] = 'A' + (i % 26);
    }
    
    // Test case: Write data and read it back in various ways
    
    // First, write 100 bytes
    r = fs_ops.write(filename, write_buf, 100, 0, mock_file_info);
    ck_assert_int_eq(r, 100);
    
    // Read back entire content
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 0, mock_file_info);
    ck_assert_int_eq(r, 100);
    ck_assert_int_eq(memcmp(write_buf, read_buf, 100), 0);
    
    // Read with offset
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 50, mock_file_info);
    ck_assert_int_eq(r, 50);
    ck_assert_int_eq(memcmp(write_buf + 50, read_buf, 50), 0);
    
    // Read with offset beyond EOF
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 100, mock_file_info);
    ck_assert_int_eq(r, 0);
    
    // Now extend file to exactly one block
    r = fs_ops.write(filename, write_buf, FS_BLOCK_SIZE, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE);
    
    // Read back entire block
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE);
    ck_assert_int_eq(memcmp(write_buf, read_buf, FS_BLOCK_SIZE), 0);
    
    // Extend to 1.5 blocks
    r = fs_ops.write(filename, write_buf, FS_BLOCK_SIZE + (FS_BLOCK_SIZE/2), 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE + (FS_BLOCK_SIZE/2));
    
    // Read entire content
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE + (FS_BLOCK_SIZE/2));
    ck_assert_int_eq(memcmp(write_buf, read_buf, FS_BLOCK_SIZE + (FS_BLOCK_SIZE/2)), 0);
    
    // Read across block boundary
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, FS_BLOCK_SIZE, FS_BLOCK_SIZE/2, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE);
    ck_assert_int_eq(memcmp(write_buf + (FS_BLOCK_SIZE/2), read_buf, FS_BLOCK_SIZE), 0);
    
    // Extend to 2 full blocks
    r = fs_ops.write(filename, write_buf, FS_BLOCK_SIZE * 2, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE * 2);
    
    // Read both blocks
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE * 2);
    ck_assert_int_eq(memcmp(write_buf, read_buf, FS_BLOCK_SIZE * 2), 0);
    
    // Read just second block
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, FS_BLOCK_SIZE, FS_BLOCK_SIZE, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE);
    ck_assert_int_eq(memcmp(write_buf + FS_BLOCK_SIZE, read_buf, FS_BLOCK_SIZE), 0);
    
    // Extend to 2.5 blocks
    r = fs_ops.write(filename, write_buf, FS_BLOCK_SIZE * 2 + (FS_BLOCK_SIZE/2), 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE * 2 + (FS_BLOCK_SIZE/2));
    
    // Extend to 3 blocks
    r = fs_ops.write(filename, write_buf, FS_BLOCK_SIZE * 3, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE * 3);
    
    // Read all three blocks
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, buf_size, 0, mock_file_info);
    ck_assert_int_eq(r, FS_BLOCK_SIZE * 3);
    ck_assert_int_eq(memcmp(write_buf, read_buf, FS_BLOCK_SIZE * 3), 0);
    
    // Read with length limit
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, 100, 0, mock_file_info);
    ck_assert_int_eq(r, 100); // Should only read 100 bytes
    ck_assert_int_eq(memcmp(write_buf, read_buf, 100), 0);
    
    // Read from middle of file with length limit
    memset(read_buf, 0, buf_size);
    r = fs_ops.read(filename, read_buf, 100, FS_BLOCK_SIZE + 50, mock_file_info);
    ck_assert_int_eq(r, 100);
    ck_assert_int_eq(memcmp(write_buf + FS_BLOCK_SIZE + 50, read_buf, 100), 0);
    
    // Test error cases
    
    // Read from non-existent file
    r = fs_ops.read("/nonexistent.txt", read_buf, 100, 0, mock_file_info);
    ck_assert_int_eq(r, -ENOENT);
    
    // Read from directory
    const char *dirname = "/read_test_dir";
    r = fs_ops.mkdir(dirname, 0755);
    ck_assert_int_eq(r, 0);
    
    r = fs_ops.read(dirname, read_buf, 100, 0, mock_file_info);
    ck_assert_int_eq(r, -EISDIR);
    
    // Clean up
    r = fs_ops.unlink(filename);
    ck_assert_int_eq(r, 0);
    r = fs_ops.rmdir(dirname);
    ck_assert_int_eq(r, 0);
    
    free(write_buf);
    free(read_buf);
    free(mock_file_info);
}
END_TEST

START_TEST(fs_write_test)
{
    mode_t create_mode = S_IFREG | 0644;
    struct fuse_file_info *mock_file_info = malloc(sizeof(struct fuse_file_info));
    char *filenames[] = {
        "/write_small.txt",    // < 1 block
        "/write_1block.txt",   // 1 block
        "/write_1plus.txt",    // < 2 blocks
        "/write_2blocks.txt",  // 2 blocks
        "/write_2plus.txt",    // < 3 blocks
        "/write_3blocks.txt"   // 3 blocks
    };
    int num_files = 6;
    int r;
    
    size_t small_size = 100;
    size_t medium_size = FS_BLOCK_SIZE / 2;
    size_t block_size = FS_BLOCK_SIZE;
    size_t block_plus_size = FS_BLOCK_SIZE + (FS_BLOCK_SIZE / 2);
    size_t two_blocks_size = FS_BLOCK_SIZE * 2;
    size_t two_blocks_plus_size = FS_BLOCK_SIZE * 2 + (FS_BLOCK_SIZE / 2);
    size_t three_blocks_size = FS_BLOCK_SIZE * 3;
    
    char *small_buf = malloc(small_size + 10);
    char *medium_buf = malloc(medium_size + 10);
    char *block_buf = malloc(block_size + 10);
    char *block_plus_buf = malloc(block_plus_size + 10);
    char *two_blocks_buf = malloc(two_blocks_size + 10);
    char *two_blocks_plus_buf = malloc(two_blocks_plus_size + 10);
    char *three_blocks_buf = malloc(three_blocks_size + 10);
    
    // Fill buffers with numbered patterns
    char *ptr;
    int i;
    
    for (i = 0, ptr = small_buf; ptr < small_buf + small_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = medium_buf; ptr < medium_buf + medium_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = block_buf; ptr < block_buf + block_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = block_plus_buf; ptr < block_plus_buf + block_plus_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = two_blocks_buf; ptr < two_blocks_buf + two_blocks_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = two_blocks_plus_buf; ptr < two_blocks_plus_buf + two_blocks_plus_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    for (i = 0, ptr = three_blocks_buf; ptr < three_blocks_buf + three_blocks_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    char *read_small_buf = malloc(small_size + 10);
    char *read_medium_buf = malloc(medium_size + 10);
    char *read_block_buf = malloc(block_size + 10);
    char *read_block_plus_buf = malloc(block_plus_size + 10);
    char *read_two_blocks_buf = malloc(two_blocks_size + 10);
    char *read_two_blocks_plus_buf = malloc(two_blocks_plus_size + 10);
    char *read_three_blocks_buf = malloc(three_blocks_size + 10);
    
    for (int i = 0; i < num_files; i++) {
        r = fs_ops.create(filenames[i], create_mode, mock_file_info);
        ck_assert_int_eq(r, 0);
    }

    r = fs_ops.write(filenames[0], small_buf, small_size, 0, mock_file_info);
    ck_assert_int_eq(r, small_size);
    
    // Read back and verify
    memset(read_small_buf, 0, small_size + 10);
    r = fs_ops.read(filenames[0], read_small_buf, small_size, 0, mock_file_info);
    ck_assert_int_eq(r, small_size);
    ck_assert_int_eq(memcmp(small_buf, read_small_buf, small_size), 0);
    
    // Test 2: Write exactly one block
    r = fs_ops.write(filenames[1], block_buf, block_size, 0, mock_file_info);
    ck_assert_int_eq(r, block_size);
    
    // Read back and verify
    memset(read_block_buf, 0, block_size + 10);
    r = fs_ops.read(filenames[1], read_block_buf, block_size, 0, mock_file_info);
    ck_assert_int_eq(r, block_size);
    ck_assert_int_eq(memcmp(block_buf, read_block_buf, block_size), 0);
    
    // Test 3: Write more than one block but less than 2
    r = fs_ops.write(filenames[2], block_plus_buf, block_plus_size, 0, mock_file_info);
    ck_assert_int_eq(r, block_plus_size);
    
    // Read back and verify
    memset(read_block_plus_buf, 0, block_plus_size + 10);
    r = fs_ops.read(filenames[2], read_block_plus_buf, block_plus_size, 0, mock_file_info);
    ck_assert_int_eq(r, block_plus_size);
    ck_assert_int_eq(memcmp(block_plus_buf, read_block_plus_buf, block_plus_size), 0);
    
    // Test 4: Write exactly 2 blocks
    r = fs_ops.write(filenames[3], two_blocks_buf, two_blocks_size, 0, mock_file_info);
    ck_assert_int_eq(r, two_blocks_size);
    
    // Read back and verify
    memset(read_two_blocks_buf, 0, two_blocks_size + 10);
    r = fs_ops.read(filenames[3], read_two_blocks_buf, two_blocks_size, 0, mock_file_info);
    ck_assert_int_eq(r, two_blocks_size);
    ck_assert_int_eq(memcmp(two_blocks_buf, read_two_blocks_buf, two_blocks_size), 0);
    
    // Test 5: Write more than 2 blocks but less than 3
    r = fs_ops.write(filenames[4], two_blocks_plus_buf, two_blocks_plus_size, 0, mock_file_info);
    ck_assert_int_eq(r, two_blocks_plus_size);
    
    // Read back and verify
    memset(read_two_blocks_plus_buf, 0, two_blocks_plus_size + 10);
    r = fs_ops.read(filenames[4], read_two_blocks_plus_buf, two_blocks_plus_size, 0, mock_file_info);
    ck_assert_int_eq(r, two_blocks_plus_size);
    ck_assert_int_eq(memcmp(two_blocks_plus_buf, read_two_blocks_plus_buf, two_blocks_plus_size), 0);
    
    // Test 6: Write exactly 3 blocks
    r = fs_ops.write(filenames[5], three_blocks_buf, three_blocks_size, 0, mock_file_info);
    ck_assert_int_eq(r, three_blocks_size);
    
    // Read back and verify
    memset(read_three_blocks_buf, 0, three_blocks_size + 10);
    r = fs_ops.read(filenames[5], read_three_blocks_buf, three_blocks_size, 0, mock_file_info);
    ck_assert_int_eq(r, three_blocks_size);
    ck_assert_int_eq(memcmp(three_blocks_buf, read_three_blocks_buf, three_blocks_size), 0);
    
    // Test 7: Append to an existing file
    r = fs_ops.write(filenames[0], medium_buf, medium_size, small_size, mock_file_info);
    ck_assert_int_eq(r, medium_size);
    
    // Read back and verify
    char *combined_buf = malloc(small_size + medium_size + 10);
    memset(combined_buf, 0, small_size + medium_size + 10);
    r = fs_ops.read(filenames[0], combined_buf, small_size + medium_size, 0, mock_file_info);
    ck_assert_int_eq(r, small_size + medium_size);
    ck_assert_int_eq(memcmp(small_buf, combined_buf, small_size), 0);
    ck_assert_int_eq(memcmp(medium_buf, combined_buf + small_size, medium_size), 0);
    
    // Test 8: Overwrite test
    // Create a buffer with different content for overwriting
    char *overwrite_buf = malloc(small_size + 10);
    for (i = 1000, ptr = overwrite_buf; ptr < overwrite_buf + small_size; i++)
        ptr += sprintf(ptr, "%d ", i);
    
    // Write to the beginning of a file
    r = fs_ops.write(filenames[0], overwrite_buf, small_size, 0, mock_file_info);
    ck_assert_int_eq(r, small_size);
    
    // Read back and verify
    memset(read_small_buf, 0, small_size + 10);
    r = fs_ops.read(filenames[0], read_small_buf, small_size, 0, mock_file_info);
    ck_assert_int_eq(r, small_size);
    ck_assert_int_eq(memcmp(overwrite_buf, read_small_buf, small_size), 0);
    
    // Test 9: Verify that the rest of the file is unchanged
    memset(read_medium_buf, 0, medium_size + 10);
    r = fs_ops.read(filenames[0], read_medium_buf, medium_size, small_size, mock_file_info);
    ck_assert_int_eq(r, medium_size);
    ck_assert_int_eq(memcmp(medium_buf, read_medium_buf, medium_size), 0);
    
    // Test 10: Try to write with invalid offset
    struct stat sb;
    r = fs_ops.getattr(filenames[0], &sb);
    ck_assert_int_eq(r, 0);
    r = fs_ops.write(filenames[0], small_buf, small_size, sb.st_size + 1, mock_file_info);
    ck_assert_int_eq(r, -EINVAL);
    
    // Unlink all files
    for (int i = 0; i < num_files; i++) {
        r = fs_ops.unlink(filenames[i]);
        ck_assert_int_eq(r, 0);
    }
    
    // Free all allocated memory
    free(small_buf);
    free(medium_buf);
    free(block_buf);
    free(block_plus_buf);
    free(two_blocks_buf);
    free(two_blocks_plus_buf);
    free(three_blocks_buf);
    free(read_small_buf);
    free(read_medium_buf);
    free(read_block_buf);
    free(read_block_plus_buf);
    free(read_two_blocks_buf);
    free(read_two_blocks_plus_buf);
    free(read_three_blocks_buf);
    free(combined_buf);
    free(overwrite_buf);
    free(mock_file_info);
}
END_TEST

int main(int argc, char **argv)
{
    block_init("test2.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("write_mostly");

    tcase_add_test(tc, fs_create_file_basic_test);
    tcase_add_test(tc, fs_mkdir_basic_test);
    tcase_add_test(tc, fs_mkdir_and_create_tests);
    tcase_add_test(tc, fs_unlink_test);
    tcase_add_test(tc, fs_rmdir_test);
    tcase_add_test(tc, fs_rename_test);
    tcase_add_test(tc, fs_chmod_test);
    tcase_add_test(tc, fs_utime_test);
    tcase_add_test(tc, fs_truncate_test);
    tcase_add_test(tc, fs_read_test);
    tcase_add_test(tc, fs_write_test);

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

