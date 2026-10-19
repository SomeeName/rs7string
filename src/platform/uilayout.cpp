#include "platform/uilayout.h"

int Rs7Ui_TabItems(Rs7UiTab tab, const Rs7UiItem** out, int max) {
    int n = 0;

    for (int t = 0; t < kRs7UiTableCount; ++t) {
        const Rs7UiTable& table = *kRs7UiTables[t];

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

int Rs7Ui_PlayerTabs(Rs7UiPlayerTab* out, int max) {
    const Rs7UiItem* items[kRs7UiMaxTabItems];
    int n = Rs7Ui_TabItems(Rs7UiTab::Player, items, kRs7UiMaxTabItems);
    int tabs = 0;

    n = n < kRs7UiMaxTabItems ? n : kRs7UiMaxTabItems;

    for (int i = 0; i < n; ++i) {
        const Rs7UiItem& it = *items[i];

        if (it.kind == Rs7UiKind::Section) {
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
