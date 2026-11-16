#pragma once
#include <string>

// This DLL's folder with a trailing separator, computed once; empty when the path cannot be read in full.
const std::wstring& Rs7_ModuleDir();

// Runs on the worker thread started by DllMain.
void Rs7_Startup();
