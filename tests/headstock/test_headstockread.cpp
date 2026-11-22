#include "features/headstock/headstockread.h"
#include "test.h"

TEST(headstock_reader_rejects_non_game_process_without_retaining_output) {
    using Reader = bool (*)(std::vector<uint8_t>&, std::vector<uint8_t>&);

    for (Reader read :
         {RsxrHeadstockRead_HeadstockTemplates, RsxrHeadstockRead_HeadstockTemplatesLh, RsxrHeadstockRead_InlinePeg}) {
        std::vector<uint8_t> first(8, 0xA5), second(8, 0xA5);

        CHECK(!read(first, second));
        CHECK(first.empty());
        CHECK(second.empty());
    }
}
