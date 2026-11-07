/* Calls the shim the way the relay would, and keeps the port alive while doing
 * it: the virtual port belongs to this process, so it has to stay up for a
 * listener to find it.
 *
 * Build: gcc -O2 -o shim-host.exe shim-host.c
 */
#include <windows.h>
#include <stdio.h>

typedef void (*PostFn)(const char *);
typedef void (*SetFn)(double);

int main(void) {
  HMODULE module = LoadLibraryW(L"oak-shim.dll");
  if (module == NULL) {
    printf("load failed: %lu\n", GetLastError());
    return 1;
  }

  PostFn post = (PostFn)GetProcAddress(module, "oak_shim_post");
  SetFn set = (SetFn)GetProcAddress(module, "oak_shim_set");
  if (post == NULL || set == NULL) {
    printf("exports missing\n");
    return 1;
  }

  printf("shim loaded, sending for a few seconds\n");
  fflush(stdout);

  for (int i = 0; i < 8; ++i) {
    post("ice_drop");
    Sleep(120);
    set(0.82);
    Sleep(880);
  }

  printf("done\n");
  return 0;
}
