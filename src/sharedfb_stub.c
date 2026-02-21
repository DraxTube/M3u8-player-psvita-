/**
 * Stub vuoti per le funzioni SceSharedFb referenziate da vita2d.
 * Queste funzioni sono usate solo per la modalità shared framebuffer
 * (LiveArea overlay), che non usiamo. Forniamo implementazioni vuote
 * per soddisfare il linker.
 */

int sceSharedFbClose(int handle) { (void)handle; return 0; }
int _sceSharedFbOpen(int id, int index, void *info, int size) {
    (void)id; (void)index; (void)info; (void)size; return 0;
}
int sceSharedFbGetInfo(int handle, void *info) { (void)handle; (void)info; return 0; }
int sceSharedFbEnd(int handle) { (void)handle; return 0; }
int sceSharedFbBegin(int handle, void *info) { (void)handle; (void)info; return 0; }
