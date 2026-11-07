/* Minimal newlib syscall stubs. The bootloader never uses stdio, but the
 * default crt0 exit path links newlib's stdio cleanup, which otherwise pulls
 * the libnosys stubs and their "is not implemented" linker warnings. These
 * are never called at run time (main() never returns). */
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int ptr, int dir) { (void)fd; (void)ptr; (void)dir; return -1; }
int _read(int fd, char *ptr, int len) { (void)fd; (void)ptr; (void)len; return -1; }
int _write(int fd, char *ptr, int len) { (void)fd; (void)ptr; (void)len; return -1; }
