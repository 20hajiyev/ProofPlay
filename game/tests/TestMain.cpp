#include "TestHarness.h"

#include <cstdlib>
#include <string>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define RACER_HEAP_CHECK 1
#endif

int main()
{
    int leaking = 0;
    if (test::GlobalSetup())
        test::GlobalSetup()();
    // RACER_TEST_FILTER=substring runs only matching cases (for investigating one scenario).
    const char* filter = std::getenv("RACER_TEST_FILTER");
    for (auto& c : test::Registry())
    {
        if (filter && std::string(c.name).find(filter) == std::string::npos)
            continue;
        const int before = test::Failures();
#ifdef RACER_HEAP_CHECK
        _CrtMemState start, end, diff;
        _CrtMemCheckpoint(&start);
#endif
        c.body();
        bool leaked = false;
#ifdef RACER_HEAP_CHECK
        _CrtMemCheckpoint(&end);
        if (_CrtMemDifference(&diff, &start, &end) && diff.lCounts[_NORMAL_BLOCK] > 0)
        {
            leaked = true;
            ++leaking;
            std::printf("  LEAK %zu normal blocks, %zu bytes still allocated\n", diff.lCounts[_NORMAL_BLOCK], diff.lSizes[_NORMAL_BLOCK]);
        }
#endif
        std::printf("%s %s\n", (test::Failures() == before && !leaked) ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed checks, %d leaking cases\n", test::Registry().size(), test::Failures(), leaking);
    return (test::Failures() == 0 && leaking == 0) ? 0 : 1;
}
