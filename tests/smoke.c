#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct version {
    int32_t major;
    int32_t minor;
};

static bool is_expected_version(struct version candidate)
{
    return candidate.major == 0 && candidate.minor == 1;
}

int main(void)
{
    struct version placeholder = { .major = 0, .minor = 1 };
    bool toolchain_speaks_c99 = is_expected_version(placeholder);

    printf("qc smoke: c99 %s\n", toolchain_speaks_c99 ? "ok" : "broken");
    return toolchain_speaks_c99 ? 0 : 1;
}
