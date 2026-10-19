#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Textures of the 7-string model, registered on the loader thread through the game's own dds handler
// (assetload_tex.cpp).
namespace rs7asset {
// The whole file (1 byte .. 64 MiB); false (out empty) when it cannot be read.
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out);
// X_DDSH 0x4A81D0 over an in-memory IFile, SEH-guarded: the resource registered under `key`, nullptr on a fault.
void* CallDdsHandler(const char* key, const uint8_t* bytes, uint32_t size);
// Case-insensitive membership.
bool INameIn(const std::string& n, const std::vector<std::string>& list);
// The texture names the stock nif (buf/size as the nif handler got them; may be null) names.
std::vector<std::string> StockDdsNames(const void* stockBuf, uint32_t stockSize);
// Registers every texture of `model` (the 7-string model file) that the stock nif does not name and that lies next
// to `path`; returns how many the game accepted.
int RegisterTextures(const char* name, const std::wstring& path, const std::vector<uint8_t>& model,
                     const void* stockBuf, uint32_t stockSize);
// Diagnostic: the engine's NiTexture list entries of our textures, logged.
void LogTextureList(const char* when);
}  // namespace rs7asset
