#include <subprocess/subprocess.hpp>

int main() {
  using namespace process::named_arguments;
  using namespace process;

#if defined(_WIN32)
  run({"cmd.exe", "/c", "dir"}, $stdout > 2);
#else
  run({"ls", "-lh", "./"}, $stdout > 2);
#endif
}
