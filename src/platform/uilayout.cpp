#include "platform/uilayout.h"

int RsxrUi_TabItems(RsxrUiTab tab, const RsxrUiItem** out, int max) {
    int n = 0;

    for (int t = 0; t < kRsxrUiTableCount; ++t) {
        const RsxrUiTable& table = *kRsxrUiTables[t];

        for (int i = 0; i < table.count; ++i) {
            if (table.items[i].tab != tab) {
                continue;
            }

            if (n < max) {
                out[n] = &table.items[i];
            }
            ++n;
        }
    }

    return n;
}

int RsxrUi_PlayerTabs(RsxrUiPlayerTab* out, int max) {
    const RsxrUiItem* items[kRsxrUiMaxTabItems];
    int n = RsxrUi_TabItems(RsxrUiTab::Player, items, kRsxrUiMaxTabItems);
    int tabs = 0;

    n = n < kRsxrUiMaxTabItems ? n : kRsxrUiMaxTabItems;

    for (int i = 0; i < n; ++i) {
        const RsxrUiItem& it = *items[i];

        if (it.kind == RsxrUiKind::Section) {
            if (tabs < max) {
                out[tabs] = {it.text, it.label ? it.label : it.text, i + 1};
            }
            ++tabs;
        } else if (tabs > 0 && tabs <= max) {
            ++out[tabs - 1].count;
            out[tabs - 1].locked += it.locked ? 1 : 0;
        }
    }

    return tabs;
}
