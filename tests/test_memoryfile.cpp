#include "model/memoryfile.h"
#include "test.h"
#include <cstring>

namespace {
void __fastcall Destroy(rs7::MemoryFile*, void*, uint32_t) {}

long __fastcall Release(rs7::MemoryFile* f, void*) {
    return rs7::MemoryFileOps::DropRef(f);
}
}  // namespace

TEST(memory_file_vtable_read_seek_and_reference_contract) {
    const auto vt = rs7::MemoryFileVtable<true, true>(Destroy, Release);
    const uint8_t bytes[] = {1, 2, 3, 4};
    rs7::MemoryFile f{vt.data(), 1, bytes, sizeof bytes, 0};
    using Get = uint32_t(__thiscall*)(void*);
    using Read = uint32_t(__thiscall*)(void*, void*, uint32_t);
    using Seek = bool(__thiscall*)(void*, uint32_t);
    using Ref = long(__thiscall*)(void*);
    using Size64 = uint64_t(__thiscall*)(void*);
    using Good = bool(__thiscall*)(void*);
    uint8_t out[8] = {};

    CHECK_EQ(reinterpret_cast<Ref>(vt[1])(&f), 2L);
    CHECK_EQ(reinterpret_cast<Ref>(vt[2])(&f), 1L);
    CHECK_EQ(reinterpret_cast<Get>(vt[4])(&f), 4u);
    CHECK_EQ(reinterpret_cast<Size64>(vt[7])(&f), uint64_t(4));
    CHECK_EQ(reinterpret_cast<Read>(vt[3])(&f, out, 8), 4u);
    CHECK(std::memcmp(out, bytes, 4) == 0);
    CHECK_EQ(reinterpret_cast<Get>(vt[5])(&f), 4u);
    CHECK(!reinterpret_cast<Seek>(vt[6])(&f, 5));
    CHECK(reinterpret_cast<Seek>(vt[6])(&f, 0));
    CHECK_EQ(reinterpret_cast<Read>(vt[3])(&f, nullptr, 2), 2u);
    CHECK_EQ(f.pos, 2u);
    CHECK(reinterpret_cast<Good>(vt[9])(&f));
    f.data = nullptr;
    CHECK(!reinterpret_cast<Good>(vt[9])(&f));
    CHECK(reinterpret_cast<Good>(vt[8])(&f));
    CHECK(reinterpret_cast<Good>(vt[10])(&f));
    CHECK(reinterpret_cast<Good>(vt[11])(&f));
}

TEST(memory_file_owned_policy_rejects_null_destination_without_advancing) {
    rs7::MemoryFile f{nullptr, 1, nullptr, 4, 0};

    CHECK_EQ(rs7::MemoryFileOps::Read<false>(&f, nullptr, nullptr, 2), 0u);
    CHECK_EQ(f.pos, 0u);
    CHECK(rs7::MemoryFileOps::Good<false>(&f, nullptr));
}
