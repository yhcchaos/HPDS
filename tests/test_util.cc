#include "yhchaos/yhchaos.h"
#include <assert.h>

yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_ROOT();

void test_assert() {
    YHCHAOS_LOG_INFO(g_logger) << yhchaos::BacktraceToString(10);
    //YHCHAOS_ASSERT2(0 == 1, "abcdef xx");
}

int main(int argc, char** argv) {
    test_assert();

    int arr[] = {1,3,5,7,9,11};

    YHCHAOS_LOG_INFO(g_logger) << yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 0);
    YHCHAOS_LOG_INFO(g_logger) << yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 1);
    YHCHAOS_LOG_INFO(g_logger) << yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 4);
    YHCHAOS_LOG_INFO(g_logger) << yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 13);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 0) == -1);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 1) == 0);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 2) == -2);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 3) == 1);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 4) == -3);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 5) == 2);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 6) == -4);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 7) == 3);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 8) == -5);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 9) == 4);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 10) == -6);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 11) == 5);
    YHCHAOS_ASSERT(yhchaos::BinarySearch(arr, sizeof(arr) / sizeof(arr[0]), 12) == -7);
    return 0;
}
