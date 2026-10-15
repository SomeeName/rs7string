#pragma once
#include <cstdint>
#include <vector>

// Fixed headstock inputs only, read into memory after the game's VFS is ready.
// Unknown build, unready VFS, read fault or oversized entry: false, empty outputs.
bool Rs7HeadstockRead_HeadstockTemplates(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool Rs7HeadstockRead_HeadstockTemplatesLh(std::vector<uint8_t>& guitar, std::vector<uint8_t>& peg);
bool Rs7HeadstockRead_InlinePeg(std::vector<uint8_t>& peg, std::vector<uint8_t>& hardware);
