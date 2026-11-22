#pragma once
#include <cstdint>
#include <vector>

// Fixed headstock inputs only, read into memory after the game's VFS is ready.
// Unknown build, unready VFS, read fault or oversized entry: false, empty outputs.
bool RsxrHeadstockRead_HeadstockTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool RsxrHeadstockRead_HeadstockTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
// The in-line bass frame (bass_thunder[_lh].nif) and its peg (peg_2clove.nif) from the same archive.
bool RsxrHeadstockRead_BassTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool RsxrHeadstockRead_BassTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool RsxrHeadstockRead_InlinePeg(std::vector<uint8_t>& peg, std::vector<uint8_t>& hardware);
