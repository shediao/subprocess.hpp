#include <subprocess/subprocess.hpp>

int main() {
#if defined(_WIN32)
  $({"cmd.exe", "/c", "dir"}, $stdout > 2);
#else
  $({"ls", "-lh", "./"}, $stdout > 2);
#endif
}
