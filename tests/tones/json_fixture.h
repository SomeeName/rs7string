#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace tonetest {
inline uint32_t Addr(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// Minimal owned JSON objects with the same vtable/string contracts used by the production adapter.
struct Node {
    uintptr_t* table = methods;
    uint32_t padding = 0;
    double number = 26;
    uintptr_t methods[26]{};
    uint32_t stringValue[3]{}, gameString[6]{};
    std::string text;
    std::map<std::string, Node*> members;
    uint32_t refs = 0;
    bool canonical = true;

    explicit Node(const char* value = "") : text(value) {
        methods[0] = reinterpret_cast<uintptr_t>(&Retain);
        methods[1] = reinterpret_cast<uintptr_t>(&Release);
        methods[0x14 / 4] = reinterpret_cast<uintptr_t>(&Find);
        methods[0x18 / 4] = reinterpret_cast<uintptr_t>(&Find);
        methods[0x34 / 4] = reinterpret_cast<uintptr_t>(&Unwrap);
        methods[0x50 / 4] = reinterpret_cast<uintptr_t>(&Number);
        methods[0x5C / 4] = reinterpret_cast<uintptr_t>(&Canonical);
        methods[0x60 / 4] = reinterpret_cast<uintptr_t>(&String);
        stringValue[2] = Addr(gameString);
        gameString[0] = Addr(text.c_str());
        gameString[4] = gameString[0] + static_cast<uint32_t>(text.size());
    }

    static uint32_t __fastcall Retain(Node* self, void*) { return ++self->refs; }

    static uint32_t __fastcall Release(Node* self, void*) { return --self->refs; }

    static uint32_t __fastcall Find(Node* self, void*, const char* key) {
        const auto found = self->members.find(key);

        return found == self->members.end() ? 0 : Addr(found->second);
    }

    static uint32_t __fastcall Unwrap(Node*, void*) { return 0; }

    static uint32_t __fastcall Number(Node* self, void*) { return Addr(self); }

    static uint32_t __fastcall Canonical(Node* self, void*) { return self->canonical ? Addr(self) : 0; }

    static uint32_t __fastcall String(Node* self, void*) { return Addr(self->stringValue); }
};

struct Context {
    Node root, custom, players[2];
    std::array<uint32_t, 10> manager{}, context{};
    uint32_t managerAddress = Addr(manager.data());

    Context() {
        manager[0x14 / 4] = Addr(context.data());
        context[0x20 / 4] = Addr(&root);
        root.members["CustomTone"] = &custom;
        custom.members["0"] = &players[0];
        custom.members["1"] = &players[1];
    }

    uint32_t Global() const { return Addr(&managerAddress); }
};
}  // namespace tonetest
