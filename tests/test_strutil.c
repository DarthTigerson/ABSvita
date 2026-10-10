#include "strutil.h"
#include "test.h"

TEST(copy_truncates_on_utf8_boundary) {
    char b[6];
    str_copy(b, sizeof b, "abcd\xC3\xA9xyz"); /* "abcdé..." : é is 2 bytes, would be split */
    CHECK_STR(b, "abcd");
    str_copy(b, sizeof b, NULL);
    CHECK_STR(b, "");
    str_copy(b, sizeof b, "hi");
    CHECK_STR(b, "hi");
    char c[8];
    str_copy(c, sizeof c, "\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"); /* three em dashes (3 bytes each) */
    CHECK_STR(c, "\xE2\x80\x94\xE2\x80\x94");
}
TEST(time_formats) {
    char b[16];
    fmt_time(0, b, sizeof b); CHECK_STR(b, "0:00");
    fmt_time(125.9, b, sizeof b); CHECK_STR(b, "2:05");
    fmt_time(3723, b, sizeof b); CHECK_STR(b, "1:02:03");
    fmt_time(-4, b, sizeof b); CHECK_STR(b, "0:00");
    fmt_duration(3600 * 12 + 300, b, sizeof b); CHECK_STR(b, "12h 5m");
    fmt_duration(2700, b, sizeof b); CHECK_STR(b, "45m");
    fmt_duration(30, b, sizeof b); CHECK_STR(b, "30s");
}
TEST(icontains) {
    CHECK(str_icontains("The Hobbit", "hob"));
    CHECK(!str_icontains("The Hobbit", "dune"));
    CHECK(str_icontains("anything", ""));
}
TEST(strip_html) {
    char s[] = "<p>Hello&nbsp;<b>world</b> &amp; more</p><p>Second &quot;para&quot;</p>";
    str_strip_html(s);
    CHECK_STR(s, "Hello world & more\nSecond \"para\"");
    char t[] = "a<br/>b &#39;c&#39; &lt;d&gt;";
    str_strip_html(t);
    CHECK_STR(t, "a\nb 'c' <d>");
}

void suite_strutil(void) {
    RUN(copy_truncates_on_utf8_boundary);
    RUN(time_formats);
    RUN(icontains);
    RUN(strip_html);
}
