#define FUSE_USE_VERSION 27
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "fs5600.h"

/* if you don't understand why you can't use these system calls here, 
 * you need to read the assignment description another time
 */
#define stat(a,b) error do not use stat()
#define open(a,b) error do not use open()
#define read(a,b,c) error do not use read()
#define write(a,b,c) error do not use write()

#define MAX_PATH_LEN 10
#define MAX_NAME_LEN 27

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int lba, int nblks);
extern int block_write(void *buf, int lba, int nblks);

/* bitmap functions
 */
void bit_set(unsigned char *map, int i)
{
    map[i/8] |= (1 << (i%8));
}
void bit_clear(unsigned char *map, int i)
{
    map[i/8] &= ~(1 << (i%8));
}
int bit_test(unsigned char *map, int i)
{
    return map[i/8] & (1 << (i%8));
}

// 32 * 1024 = 32k block
#define TOTAL_BLOCKS 32768

static struct fs_super super;
static unsigned char bitmap[TOTAL_BLOCKS];
static struct fs_inode root_inode;

/* init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, read bitmaps and inodes
 */
void* fs_init(struct fuse_conn_info *conn)
{
    /* your code here */
    (void) conn;
    block_read(&super, 0, 1);
    block_read(bitmap, 1, 1);
    block_read(&root_inode, 2, 1);
    return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path doesn't exist.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */

 int parse(char *path, char **argv)
 {
    int i;
    for (i = 0; i < MAX_PATH_LEN; i++) {
        if ((argv[i] = strtok(path, "/")) == NULL)
            break;
        if (strlen(argv[i]) > MAX_NAME_LEN)
            argv[i][MAX_NAME_LEN] = '\0';
        path = NULL;
    }
    return i;
 }

int translate(const char *c_path)
{
    int inum;
    int i;
    int j;
    int pathc;
    int32_t found;
    struct fs_inode *inode;
    struct fs_dirent dirents[128];
    struct fs_dirent *ent;

    char *path = strdup(c_path);
    char **pathv = malloc(MAX_NAME_LEN * sizeof(char *));

    pathc = parse(path, pathv);
    inum = 2;

    for (i = 0; i < pathc; i++) {
        inode = malloc(sizeof(struct fs_inode));
        block_read(inode, inum, 1);
        
        if (!S_ISDIR(inode->mode)) {
            free(path);
            free(pathv);
            free(inode);
            return -ENOTDIR;
        }

        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        found = 0;
        for (j = 0; j < 128; j++) {
            ent = &dirents[j];
            if (ent->valid && strcmp(ent->name, pathv[i]) == 0) {
                found = ent->inode;
                break;
            }
        }

        free(inode);

        if (found) {
            inum = found;
        } else {
            free(path);
            free(pathv);
            return -ENOENT;
        }
    }

    free(path);
    free(pathv);

    return inum;
}

void inode_to_stat(int inum, struct stat *sb)
{
    struct fs_inode *inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);

    sb->st_mtim.tv_sec = inode->mtime;
    sb->st_atim.tv_sec = inode->mtime;
    sb->st_ctim.tv_sec = inode->ctime;
    sb->st_nlink = 1;
    sb->st_mode = inode->mode;
    sb->st_uid = inode->uid;
    sb->st_gid = inode->gid;
    sb->st_size = inode->size;
    sb->st_blksize = FS_BLOCK_SIZE;
    free(inode);
}

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - for several fields in 'struct stat' there is no corresponding
 *  information in our file system:
 *    st_nlink - always set it to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * success - return 0
 * errors - path translation, ENOENT
 * hint - factor out inode-to-struct stat conversion - you'll use it
 *        again in readdir
 */
int fs_getattr(const char *path, struct stat *sb)
{
    /* your code here */
    int inum;
    inum = translate(path);
    
    if (inum < 0)
        return inum;
    
    inode_to_stat(inum, sb);

    return 0;
    // return -EOPNOTSUPP;
}

const char *merge_path(const char *base_path, char *sub)
{
    char *result = malloc(28);
    size_t base_len = strlen(base_path);
    
    if (base_len > 0 && base_path[base_len - 1] == '/') {
        snprintf(result, 28, "%s%s", base_path, sub);
    } else {
        snprintf(result, 28, "%s/%s", base_path, sub);
    }

    return result;
}

/* readdir - get directory contents.
 *
 * call the 'filler' function once for each valid entry in the 
 * directory, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a pointer to a struct stat
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 * 
 * hint - check the testing instructions if you don't understand how
 *        to call the filler function
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
    /* your code here */
    int inum;
    struct fs_inode *inode;
    int i;
    struct stat *sb;
    const char *sub_path;
    int getattr_r;

    inode = malloc(sizeof(struct fs_inode));

    inum = translate(path);

    if (inum < 0)
        return inum;

    block_read(inode, inum, 1);

    struct fs_dirent dirents[128];

    block_read(dirents, inode->ptrs[0], 1);

    sb = malloc(sizeof(struct stat));
    fs_getattr(path, sb);

    filler(ptr, ".", sb, 0);

    // TODO - get parent's path.
    // fs_getattr()
    filler(ptr, "..", NULL, 0);

    for (i = 0; i < 128; i++) {
        if (!dirents[i].valid)
            continue;
        memset(sb, 0, sizeof(struct stat));
        sub_path = merge_path(path, dirents[i].name);
        
        if ((getattr_r = fs_getattr(sub_path, sb)) < 0) {
            free(sb);
            return getattr_r;
        }
        filler(ptr, dirents[i].name, sb, 0);
    }

    free(inode);
    free(sb);

    return 0;
}

int find_freeblock()
{
    int i;
    int n = super.disk_size * 8;

    for (i = 0; i < n; i++) {
        if (bit_test(bitmap, i) == 0) {
            return i;
        }
    }

    return -1;
}

int create_inode(char *name, mode_t mode)
{
    struct fs_inode *inode;
    time_t raw_time;
    struct fuse_context *ctx;
    int inum;
    int inum_for_dirent;
    struct fs_dirent *entries;

    // Find free space.
    inum = find_freeblock();

    inode = malloc(sizeof(struct fs_inode));
    memset(inode, 0, sizeof(struct fs_inode));

    ctx = fuse_get_context();

    raw_time = time(NULL);
    inode->ctime = (uint32_t) raw_time;
    inode->mtime = (uint32_t) raw_time;
    inode->uid = ctx->uid;
    inode->gid = ctx->gid;
    inode->mode = mode;
    inode->size = 0;

    bit_set(bitmap, inum);

    if (S_ISDIR(mode)) {
        inum_for_dirent = find_freeblock();
        inode->ptrs[0] = inum_for_dirent;

        entries = malloc(sizeof(struct fs_dirent) * 128);
        memset(entries, 0, sizeof(struct fs_dirent) * 128);

        block_write(entries, inum_for_dirent, 1);
        bit_set(bitmap, inum_for_dirent);

        free(entries);
    }

    block_write(inode, inum, 1);

    block_write(bitmap, 1, 1);
    
    free(inode);

    return inum;
}

int find_base_dir(int pathc, char **pathv)
{
    int inum;
    struct fs_inode *inode;
    int found;
    int i;
    int j;
    struct fs_dirent dirents[128];
    struct fs_dirent *ent;

    inum = 2; //root
    for (i = 0; i < pathc - 1; i++) {
        inode = malloc(sizeof(struct fs_inode));
        block_read(inode, inum, 1);
        
        if (!S_ISDIR(inode->mode)) {
            free(pathv);
            free(inode);
            return -ENOTDIR;
        }

        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        found = 0;
        for (j = 0; j < 128; j++) {
            ent = &dirents[j];
            if (ent->valid && strcmp(ent->name, pathv[i]) == 0) {
                found = ent->inode;
                break;
            }
        }

        free(inode);

        if (found) {
            inum = found;
        } else {
            // path does not exist.
            inum = -1;
            break;
        }
    }

    return inum;
}

int find_entry_dirents(struct fs_dirent dirents[], char *name)
{
    int i;
    int count = 0;
    for (i = 0; i < 128; i++) {
        if (dirents[i].valid == 1) {
            count++;
            if (strcmp(dirents[i].name, name) == 0) {
                return i;
            }
        }
    }

    if (count == 128) {
        return count;
    } else {
        return -1;
    }
}

int find_freespot_dirents(struct fs_dirent dirents[])
{
    int i;

    for (i = 0; i < 128; i++) {
        if (dirents[i].valid == 0) {
            return i;
        }
    }

    return -1;
}

/* create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * Note that 'mode' will already have the S_IFREG bit set, so you can
 * just use it directly. Ignore the third parameter.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    /* your code here */
    uint32_t inum;
    char **pathv = malloc(MAX_NAME_LEN * sizeof(char *));
    int pathc;
    struct fs_inode *inode;
    int freespot;
    struct fs_dirent dirents[128];
    char *dup_path;
    int base_dir;

    dup_path = malloc(28);
    strcpy(dup_path, path);

    pathc = parse(dup_path, pathv);
    base_dir = find_base_dir(pathc, pathv);

    if (base_dir == -1) {
        free(pathv);
        // path doesn't exist
        return -EEXIST;
    } else {
        inode = malloc(sizeof(struct fs_inode));

        block_read(inode, base_dir, 1);

        // Check if file already exist
        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        int found = find_entry_dirents(dirents, pathv[pathc - 1]);

        if (found >= 0) {
            // already exist.
            free(inode);
            free(pathv);
            return -EEXIST;
        } else if (found == 128) {
            free(inode);
            free(pathv);
            return -ENOSPC;
        } else {
            // Find first free entry
            freespot = find_freespot_dirents(dirents);

            // create inode
            inum = create_inode(pathv[pathc - 1], mode);

            dirents[freespot].inode = inum;
            strcpy(dirents[freespot].name, pathv[pathc - 1]);
            dirents[freespot].valid = 1;

            block_write(dirents, inode->ptrs[0], 1);

            free(pathv);
            free(inode);
        }
    }

    return 0;
}

/* mkdir - create a directory with the given mode.
 *
 * WARNING: unlike fs_create, @mode only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 */ 
int fs_mkdir(const char *path, mode_t mode)
{
    /* your code here */
    uint32_t inum;
    char **pathv = malloc(MAX_NAME_LEN * sizeof(char *));
    int pathc;
    struct fs_inode *inode;
    int freespot;
    struct fs_dirent dirents[128];
    char *dup_path;
    int base_dir;

    mode = mode | __S_IFDIR;

    dup_path = malloc(28);
    strcpy(dup_path, path);

    pathc = parse(dup_path, pathv);
    base_dir = find_base_dir(pathc, pathv);

    if (base_dir == -1) {
        free(pathv);
        // path doesn't exist
        return -EEXIST;
    } else {
        inode = malloc(sizeof(struct fs_inode));

        block_read(inode, base_dir, 1);

        // Check if file already exist
        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        int found = find_entry_dirents(dirents, pathv[pathc - 1]);

        if (found >= 0) {
            // already exist.
            free(inode);
            free(pathv);
            return -EEXIST;
        } else if (found == 128) {
            free(inode);
            free(pathv);
            return -ENOSPC;
        } else {
            // Find first free entry
            freespot = find_freespot_dirents(dirents);

            // create inode
            inum = create_inode(pathv[pathc - 1], mode);

            dirents[freespot].inode = inum;
            strcpy(dirents[freespot].name, pathv[pathc - 1]);
            dirents[freespot].valid = 1;

            block_write(dirents, inode->ptrs[0], 1);

            free(pathv);
            free(inode);
        }
    }

    return 0;
}


/* unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 */
int fs_unlink(const char *path)
{
    /* your code here */
    char **pathv = malloc(MAX_NAME_LEN * sizeof(char *));
    int pathc;
    struct fs_inode *inode;
    struct fs_inode *file_inode;
    struct fs_dirent dirents[128];
    char *dup_path;
    int base_dir;

    dup_path = malloc(28);
    strcpy(dup_path, path);

    pathc = parse(dup_path, pathv);
    base_dir = find_base_dir(pathc, pathv);

    if (base_dir == -1) {
        free(pathv);
        // path doesn't exist
        return -ENOENT;
    } else {
        inode = malloc(sizeof(struct fs_inode));

        block_read(inode, base_dir, 1);

        // Check if file exist
        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        int found = find_entry_dirents(dirents, pathv[pathc - 1]);

        if (found < 0 || found == 128) {
            // File doesn't exist.
            free(inode);
            free(pathv);
            return -ENOENT;
        } else {
            file_inode = malloc(sizeof(struct fs_inode));
            block_read(file_inode, dirents[found].inode, 1);

            // Check if its a directory.
            if (S_ISDIR(file_inode->mode)) {
                free(file_inode);
                free(inode);
                free(pathv);
                return -EISDIR;
            }

            // clear data nodes
            for (int i = 0; i < FS_BLOCK_SIZE/4 - 5; i++) {
                if (file_inode->ptrs[i] != 0) {
                    bit_clear(bitmap, file_inode->ptrs[i]);
                }
            }

            // clear file inode
            bit_clear(bitmap, dirents[found].inode);

            dirents[found].valid = 0;

            block_write(dirents, inode->ptrs[0], 1);

            block_write(bitmap, 1, 1);

            free(pathv);
            free(inode);
            free(file_inode);
        }
    }

    return 0;
}

/* rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
int fs_rmdir(const char *path)
{
    /* your code here */
    char **pathv = malloc(MAX_NAME_LEN * sizeof(char *));
    int pathc;
    struct fs_inode *inode;
    struct fs_inode *dir_inode;
    struct fs_dirent dirents[128];
    struct fs_dirent target_dirents[128];
    char *dup_path;
    int base_dir;
    int not_empty = 0;

    dup_path = malloc(28);
    strcpy(dup_path, path);

    pathc = parse(dup_path, pathv);
    base_dir = find_base_dir(pathc, pathv);

    if (base_dir == -1) {
        free(pathv);
        // path doesn't exist
        return -ENOENT;
    } else {
        inode = malloc(sizeof(struct fs_inode));

        block_read(inode, base_dir, 1);

        // Check if directory exist
        memset(dirents, 0, sizeof(dirents));

        block_read(dirents, inode->ptrs[0], 1);

        int found = find_entry_dirents(dirents, pathv[pathc - 1]);

        if (found < 0 || found == 128) {
            // File doesn't exist.
            free(inode);
            free(pathv);
            return -ENOENT;
        } else {
            dir_inode = malloc(sizeof(struct fs_inode));
            block_read(dir_inode, dirents[found].inode, 1);

            // Check if its not a directory.
            if (!S_ISDIR(dir_inode->mode)) {
                free(dir_inode);
                free(inode);
                free(pathv);
                return -ENOTDIR;
            }

            // check if the directory is empty.
            block_read(target_dirents, dir_inode->ptrs[0], 1);
            for (int i = 0; i < 128; i++) {
                if (target_dirents[i].valid) {
                    not_empty = i + 1;
                    break;
                }
            }

            if (not_empty) {
                free(dir_inode);
                free(inode);
                free(pathv);
                return -ENOTEMPTY;
            }

            // clear data nodes
            for (int i = 0; i < FS_BLOCK_SIZE/4 - 5; i++) {
                if (dir_inode->ptrs[i] != 0) {
                    bit_clear(bitmap, dir_inode->ptrs[i]);
                }
            }

            // clear file inode
            bit_clear(bitmap, dirents[found].inode);

            dirents[found].valid = 0;

            block_write(dirents, inode->ptrs[0], 1);

            block_write(bitmap, 1, 1);

            free(pathv);
            free(inode);
            free(dir_inode);
        }
    }

    return 0;
}

/* rename - rename a file or directory
 * success - return 0
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
int fs_rename(const char *src_path, const char *dst_path)
{
    char **src_pathv = malloc(MAX_PATH_LEN * sizeof(char *));
    char **dst_pathv = malloc(MAX_PATH_LEN * sizeof(char *));
    int src_pathc, dst_pathc;
    char *src_dup_path, *dst_dup_path;
    int src_parent_inum, dst_parent_inum;
    struct fs_inode *parent_inode;
    struct fs_dirent dirents[128];
    int src_found, dst_found;

    src_dup_path = strdup(src_path);
    dst_dup_path = strdup(dst_path);

    src_pathc = parse(src_dup_path, src_pathv);
    dst_pathc = parse(dst_dup_path, dst_pathv);
    
    if (src_pathc != dst_pathc) {
        free(src_pathv);
        free(dst_pathv);
        free(src_dup_path);
        free(dst_dup_path);
        return -EINVAL;
    }

    if (src_pathc == 1 && dst_pathc == 1) {
        src_parent_inum = 2;
        dst_parent_inum = 2;
    } else {
        src_parent_inum = find_base_dir(src_pathc, src_pathv);
        dst_parent_inum = find_base_dir(dst_pathc, dst_pathv);
        
        if (src_parent_inum < 0 || dst_parent_inum < 0) {
            free(src_pathv);
            free(dst_pathv);
            free(src_dup_path);
            free(dst_dup_path);
            return -ENOENT;
        }
        
        if (src_parent_inum != dst_parent_inum) {
            free(src_pathv);
            free(dst_pathv);
            free(src_dup_path);
            free(dst_dup_path);
            return -EINVAL;
        }
    }
    
    parent_inode = malloc(sizeof(struct fs_inode));
    block_read(parent_inode, src_parent_inum, 1);
    
    // Read directory entries
    memset(dirents, 0, sizeof(dirents));
    block_read(dirents, parent_inode->ptrs[0], 1);
    
    // Find source entry
    src_found = find_entry_dirents(dirents, src_pathv[src_pathc - 1]);
    if (src_found < 0 || src_found == 128) {
        free(src_pathv);
        free(dst_pathv);
        free(src_dup_path);
        free(dst_dup_path);
        free(parent_inode);
        return -ENOENT;
    }
    
    // Check if destination entry already exists
    dst_found = find_entry_dirents(dirents, dst_pathv[dst_pathc - 1]);
    if (dst_found >= 0 && dst_found != 128) {
        free(src_pathv);
        free(dst_pathv);
        free(src_dup_path);
        free(dst_dup_path);
        free(parent_inode);
        return -EEXIST;
    }
    
    strncpy(dirents[src_found].name, dst_pathv[dst_pathc - 1], MAX_NAME_LEN);
    dirents[src_found].name[MAX_NAME_LEN] = '\0';
    
    block_write(dirents, parent_inode->ptrs[0], 1);
    
    time_t raw_time = time(NULL);
    parent_inode->mtime = (uint32_t) raw_time;
    block_write(parent_inode, src_parent_inum, 1);
    
    free(src_pathv);
    free(dst_pathv);
    free(src_dup_path);
    free(dst_dup_path);
    free(parent_inode);
    
    return 0;
}

/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_chmod(const char *path, mode_t mode)
{
    int inum;
    struct fs_inode *inode;
    
    inum = translate(path);
    
    if (inum < 0)
        return inum;

    inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);
    
    mode_t type_bits = inode->mode & S_IFMT;
    mode_t new_mode = (mode & ~S_IFMT) | type_bits;
    
    inode->mode = new_mode;
    
    time_t current_time = time(NULL);
    inode->mtime = (uint32_t)current_time;
    
    block_write(inode, inum, 1);
    
    free(inode);
    
    return 0;
}

int fs_utime(const char *path, struct utimbuf *ut)
{
    int inum;
    struct fs_inode *inode;

    inum = translate(path);
    
    if (inum < 0)
        return inum;
    
    inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);
    
    inode->mtime = (uint32_t)ut->modtime;
    
    block_write(inode, inum, 1);
    
    free(inode);
    
    return 0;
}

/* truncate - truncate file to exactly 'len' bytes
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0)
        return -EINVAL;

    int inum;
    struct fs_inode *inode;
    
    inum = translate(path);

    if (inum < 0)
        return inum;
    
    inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);
    
    if (S_ISDIR(inode->mode)) {
        free(inode);
        return -EISDIR;
    }
    
    for (int i = 0; i < FS_BLOCK_SIZE/4 - 5; i++) {
        if (inode->ptrs[i] != 0) {
            bit_clear(bitmap, inode->ptrs[i]);
            inode->ptrs[i] = 0;
        }
    }
    
    inode->size = 0;
    
    time_t current_time = time(NULL);
    inode->mtime = (uint32_t)current_time;
    
    block_write(inode, inum, 1);
    
    block_write(bitmap, 1, 1);
    
    free(inode);
    
    return 0;
}

/* read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
int fs_read(const char *path, char *buf, size_t len, off_t offset,
            struct fuse_file_info *fi)
{
    int inum;
    struct fs_inode *inode;
    size_t file_size;
    int block_index;
    int block_offset;
    size_t bytes_to_read;
    size_t bytes_read = 0;
    char block_buf[FS_BLOCK_SIZE];
    
    inum = translate(path);
    
    if (inum < 0)
        return inum;
    
    inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);
    
    if (S_ISDIR(inode->mode)) {
        free(inode);
        return -EISDIR;
    }
    
    file_size = inode->size;
    
    if (offset >= file_size) {
        free(inode);
        return 0;
    }
    
    if (offset + len > file_size)
        bytes_to_read = file_size - offset;
    else
        bytes_to_read = len;
    
    // read the data block by block
    while (bytes_read < bytes_to_read) {
        // Calculate which block contains the current offset
        block_index = offset / FS_BLOCK_SIZE;
        block_offset = offset % FS_BLOCK_SIZE;
        
        // Make sure the block index is within range
        if (block_index >= FS_BLOCK_SIZE/4 - 5) {
            break;
        }
        
        // Check if this block exists (it should, given the file_size)
        if (inode->ptrs[block_index] == 0) {
            break;
        }
        
        memset(block_buf, 0, FS_BLOCK_SIZE);
        block_read(block_buf, inode->ptrs[block_index], 1);
        
        size_t remaining_in_block = FS_BLOCK_SIZE - block_offset;
        size_t remaining_to_read = bytes_to_read - bytes_read;
        size_t copy_size = (remaining_in_block < remaining_to_read) ? 
                            remaining_in_block : remaining_to_read;
        
        memcpy(buf + bytes_read, block_buf + block_offset, copy_size);
        
        bytes_read += copy_size;
        offset += copy_size;
    }
    
    time_t current_time = time(NULL);
    inode->mtime = (uint32_t)current_time; // Ideally we'd update atime, but we only have mtime
    block_write(inode, inum, 1);
    
    free(inode);
    
    return bytes_read;
}

/* write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */
int fs_write(const char *path, const char *buf, size_t len,
             off_t offset, struct fuse_file_info *fi)
{
    int inum;
    struct fs_inode *inode;
    size_t file_size;
    int block_index;
    int block_offset;
    size_t bytes_to_write;
    size_t bytes_written = 0;
    char block_buf[FS_BLOCK_SIZE];
    
    inum = translate(path);
    
    // Check if file exists
    if (inum < 0)
        return inum;
    
    inode = malloc(sizeof(struct fs_inode));
    block_read(inode, inum, 1);
    
    // Check if it's a directory
    if (S_ISDIR(inode->mode)) {
        free(inode);
        return -EISDIR;
    }
    
    file_size = inode->size;
    
    // Check if offset is valid
    if (offset > file_size) {
        free(inode);
        return -EINVAL;
    }
    
    bytes_to_write = len;
    
    while (bytes_written < bytes_to_write) {
        block_index = offset / FS_BLOCK_SIZE;
        block_offset = offset % FS_BLOCK_SIZE;
        
        if (block_index >= FS_BLOCK_SIZE/4 - 5) {
            break;
        }
        
        // Check if this block exists, if not, allocate it
        if (inode->ptrs[block_index] == 0) {
            int new_block = find_freeblock();
            if (new_block < 0) {
                // No free blocks available
                break;
            }
            
            memset(block_buf, 0, FS_BLOCK_SIZE);
            block_write(block_buf, new_block, 1);
            
            bit_set(bitmap, new_block);
            inode->ptrs[block_index] = new_block;
        }
        
        memset(block_buf, 0, FS_BLOCK_SIZE);
        block_read(block_buf, inode->ptrs[block_index], 1);
        
        size_t remaining_in_block = FS_BLOCK_SIZE - block_offset;
        size_t remaining_to_write = bytes_to_write - bytes_written;
        size_t write_size = (remaining_in_block < remaining_to_write) ? 
                           remaining_in_block : remaining_to_write;
        
        memcpy(block_buf + block_offset, buf + bytes_written, write_size);
        
        block_write(block_buf, inode->ptrs[block_index], 1);
        
        bytes_written += write_size;
        offset += write_size;
    }
    
    if (offset > file_size) {
        inode->size = offset;
    }
    
    time_t current_time = time(NULL);
    inode->mtime = (uint32_t)current_time;
    
    block_write(inode, inum, 1);
    block_write(bitmap, 1, 1);
    
    free(inode);
    
    return bytes_written;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - (superblock + block map)
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namemax = <whatever your max namelength is>
     *
     * it's OK to calculate this dynamically on the rare occasions
     * when this function is called.
     */
    
    memset(st, 0, sizeof(struct statvfs));
    
    st->f_bsize = FS_BLOCK_SIZE;
    st->f_frsize = FS_BLOCK_SIZE;
    
    st->f_blocks = super.disk_size - 2; // 2 blocks for superblock and bitmap
    
    unsigned long free_blocks = 0;
    for (unsigned long i = 2; i < super.disk_size; i++) {
        if (bit_test(bitmap, i) == 0) {
            free_blocks++;
        }
    }
    
    st->f_bfree = free_blocks;
    st->f_bavail = free_blocks;
    
    st->f_namemax = MAX_NAME_LEN;
    
    return 0;
}

/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
    .init = fs_init,            /* read-mostly operations */
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .read = fs_read,
    .statfs = fs_statfs,

    .create = fs_create,        /* write operations */
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .write = fs_write,
};

