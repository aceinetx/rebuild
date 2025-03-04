# rebuild
Incremental build system designed to be a program that compiles program(s)

# Example usage
```cpp
#include "rebuild.h"

int main(int argc, char **argv) {
  rebuild_targets.push_back(new Target("a", {"b.o", "a.o"}, "gcc b.o a.o -o a"));
  rebuild_targets.push_back(new Target("a.o", {"a.c"}, "gcc -c a.c -o a.o"));
  rebuild_targets.push_back(new Target("b.o", {"b.c"}, "gcc -c b.c -o b.o"));
  return 0;
}
```

# Get started
- Download the header: ```wget https://raw.githubusercontent.com/aceinetx/rebuild/refs/heads/main/rebuild.h```
- Write rebuild.cpp with your targets (ref. above)
- Compile rebuild: ```g++ -o rebuild rebuild.cpp```
