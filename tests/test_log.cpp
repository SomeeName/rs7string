#include "log.h"
#include "test.h"
#include <string>

TEST(log_line_has_seconds_millis_and_newline) {
    CHECK_EQ(Rs7Log_Format(12345, "hi"), std::string("[     12.345] hi\n"));
    CHECK_EQ(Rs7Log_Format(0, "x"), std::string("[      0.000] x\n"));
}

TEST(log_before_open_is_a_noop) {
    Rs7Log("nothing happens %d", 1);  // must not crash without Rs7Log_Open
    CHECK(true);
}
