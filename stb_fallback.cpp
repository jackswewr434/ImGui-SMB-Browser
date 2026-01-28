// Fallback weak definitions for stb_image functions to avoid linker errors
// If the real stb_image implementation is linked, these weak symbols will be overridden.

#include <cstdlib>

extern "C" unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp) __attribute__((weak));
extern "C" void stbi_image_free(void *retval_from_stbi_load) __attribute__((weak));

extern "C" unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp) {
    (void)buffer; (void)len; (void)x; (void)y; (void)comp; (void)req_comp;
    return nullptr;
}

extern "C" void stbi_image_free(void *p) {
    free(p);
}
