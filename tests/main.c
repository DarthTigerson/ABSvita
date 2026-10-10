#include "test.h"

int t_failures, t_checks;
const char *t_current;

#define SUITES(X) X(suite_timeline) X(suite_mp4) X(suite_strutil) X(suite_abs_json) X(suite_policy) X(suite_utils) X(suite_pipeline)
#define DECL(s) void s(void);
SUITES(DECL)

int main(void) {
#define CALL(s) printf("%s\n", #s); s();
    SUITES(CALL)
    printf("\n%d checks, %d failures\n", t_checks, t_failures);
    return t_failures ? 1 : 0;
}
