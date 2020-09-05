#include <cstdio>
#include "ThreadPool.h"

auto f(int a) -> int {
    if (a <= 2)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    printf("%d\n", a);
    return a;
}

int main () {
    ThreadPool pool(4);
    auto ret = pool.enqueue(f, 1);
    pool.enqueue(f, 2);
    pool.enqueue(f, 3);
    pool.enqueue(f, 4);
    printf("result: %d\b", ret.get());
}