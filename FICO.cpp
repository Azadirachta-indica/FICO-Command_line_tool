#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <cstdint>
#include <limits>
#include <cwctype>

using namespace std;
namespace fs = std::filesystem;

// ============================================================
// FICO - File Compression CLI
// Windows-only | C++17
//
// External tools:
//   7-Zip: 7z.exe
//   WinRAR: rar.exe (RAR creation only)
//
// Commands:
//   fico help
//   fico tools
//   fico version
//   fico compress 7z <input> [output] [options]
//   fico compress zip <input> [output] [options]
//   fico compress rar <input> [output] [options]
//   fico extract <archive> [destination]
//   fico list <archive>
//   fico test <archive>
//   fico hash <file>
//   fico verify-hash <file> <sha256>
//   fico benchmark <input>
// ============================================================

namespace FICO {

const string VERSION = "1.0.0";

struct Tools {
    fs::path seven;
    fs::path rar;
};

wstring quoteArg(const wstring& s) {
    wstring out = L"\"";
    size_t slashes = 0;

    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
        } else if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out += L'"';
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out += c;
        }
    }

    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}

int runTool(const fs::path& exe, const vector<wstring>& args) {
    wstring command = quoteArg(exe.wstring());

    for (const auto& arg : args)
        command += L" " + quoteArg(arg);

    vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(
            exe.c_str(), buffer.data(),
            nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &si, &pi)) {
        wcerr << L"FICO: Could not launch " << exe.wstring() << L"\n";
        return 127;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &code))
        code = 1;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return static_cast<int>(code);
}

wstring widen(const string& s) {
    if (s.empty()) return L"";

    int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);

    if (size <= 0) {
        size = MultiByteToWideChar(
            CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (size <= 0) return L"";
        wstring out(size, L'\0');
        MultiByteToWideChar(
            CP_ACP, 0, s.c_str(), -1, out.data(), size);
        out.resize(size - 1);
        return out;
    }

    wstring out(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
        out.data(), size);
    out.resize(size - 1);
    return out;
}

string narrow(const wstring& s) {
    if (s.empty()) return "";

    int size = WideCharToMultiByte(
        CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);

    if (size <= 0) return "";

    string out(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, s.c_str(), -1, out.data(), size,
        nullptr, nullptr);
    out.resize(size - 1);
    return out;
}

fs::path argPath(const char* arg) {
    return fs::path(widen(arg));
}

string lower(string s) {
    transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
    return s;
}

bool regularFile(const fs::path& p) {
    error_code ec;
    return fs::is_regular_file(p, ec) && !ec;
}

bool existsPath(const fs::path& p) {
    error_code ec;
    return fs::exists(p, ec) && !ec;
}

fs::path findExecutable(const vector<wstring>& names) {
    vector<fs::path> dirs;

    wchar_t* env = _wgetenv(L"PATH");
    wstring path = env ? env : L"";

    dirs.push_back(L"C:\\Program Files\\7-Zip");
    dirs.push_back(L"C:\\Program Files (x86)\\7-Zip");
    dirs.push_back(L"C:\\Program Files\\WinRAR");
    dirs.push_back(L"C:\\Program Files (x86)\\WinRAR");

    size_t start = 0;

    while (start <= path.size()) {
        size_t end = path.find(L';', start);

        wstring part = path.substr(
            start,
            end == wstring::npos ? path.size() - start : end - start);

        if (!part.empty())
            dirs.emplace_back(part);

        if (end == wstring::npos) break;
        start = end + 1;
    }

    for (const auto& dir : dirs) {
        for (const auto& name : names) {
            fs::path candidate = dir / name;

            if (regularFile(candidate))
                return candidate;
        }
    }

    return {};
}

Tools detectTools() {
    Tools t;

    t.seven = findExecutable({
        L"7z.exe", L"7zz.exe", L"7za.exe"
    });

    t.rar = findExecutable({
        L"rar.exe"
    });

    return t;
}

uintmax_t fileSize(const fs::path& p) {
    error_code ec;
    uintmax_t n = fs::file_size(p, ec);
    return ec ? 0 : n;
}

uintmax_t directorySize(const fs::path& p) {
    uintmax_t total = 0;
    error_code ec;

    if (regularFile(p))
        return fileSize(p);

    if (!fs::is_directory(p, ec) || ec)
        return 0;

    fs::recursive_directory_iterator it(
        p, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        if (it->is_regular_file(ec) && !ec) {
            uintmax_t size = it->file_size(ec);
            if (!ec && size <= UINTMAX_MAX - total)
                total += size;
        }
        ec.clear();
    }

    return total;
}

string humanSize(uintmax_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int unit = 0;

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        ++unit;
    }

    ostringstream out;
    out << fixed << setprecision(unit == 0 ? 0 : 2)
        << size << " " << units[unit];

    return out.str();
}

void help() {
    cout <<
R"(FICO - File Compression CLI v1.0.0
Windows edition

USAGE
  fico help
  fico version
  fico tools

  fico compress 7z <input> [output.7z] [options]
  fico compress zip <input> [output.zip] [options]
  fico compress rar <input> [output.rar] [options]

  fico extract <archive> [destination]
  fico list <archive>
  fico test <archive>
  fico hash <file>
  fico verify-hash <file> <sha256>
  fico benchmark <input>

COMPRESSION OPTIONS
  --level 1..9        Compression level
  --exclude PATTERN   Exclude matching filenames
  --split SIZE        Split archive, e.g. 100M or 1G
  --overwrite         Allow replacing an existing output

EXAMPLES
  fico compress 7z "C:\work\project"
  fico compress zip "C:\work\project" "D:\backup.zip"
  fico compress 7z "C:\work" --level 9
  fico compress 7z "C:\work" --exclude "*.tmp"
  fico compress 7z "C:\work" --split 100M
  fico extract "D:\backup.7z" "D:\restored"
  fico list "D:\backup.7z"
  fico test "D:\backup.7z"
  fico hash "D:\backup.7z"

REQUIREMENTS
  7-Zip for ZIP/7z operations, listing, testing and extraction.
  WinRAR for RAR creation.

NOTE
  Do not extract untrusted archives into sensitive directories.
  FICO's external-tool integration does not replace a security audit.
)";
}

bool parseSize(const wstring& text, wstring& result) {
    if (text.empty()) return false;

    wstring s = text;
    for (auto& c : s) c = towupper(c);

    size_t i = 0;
    while (i < s.size() && iswdigit(s[i])) ++i;

    if (i == 0) return false;

    wstring number = s.substr(0, i);
    wstring suffix = s.substr(i);

    if (suffix.empty()) suffix = L"B";

    if (suffix != L"B" && suffix != L"K" && suffix != L"KB" &&
        suffix != L"M" && suffix != L"MB" &&
        suffix != L"G" && suffix != L"GB")
        return false;

    result = number + suffix;
    return true;
}

bool isWithin(const fs::path& child, const fs::path& parent) {
    fs::path c = child.lexically_normal();
    fs::path p = parent.lexically_normal();

    auto ci = c.begin();
    auto pi = p.begin();

    for (; pi != p.end(); ++pi, ++ci) {
        if (ci == c.end()) return false;

        if (_wcsicmp(ci->c_str(), pi->c_str()) != 0)
            return false;
    }

    return true;
}

// ------------------------------------------------------------
// Archive commands
// ------------------------------------------------------------

int compress(const Tools& tools, int argc, char* argv[]) {
    if (argc < 4) {
        help();
        return 2;
    }

    string format = lower(argv[2]);

    if (format != "7z" && format != "zip" && format != "rar") {
        cerr << "FICO: Format must be 7z, zip or rar.\n";
        return 2;
    }

    fs::path input = argPath(argv[3]);

    if (!existsPath(input)) {
        cerr << "FICO: Input does not exist.\n";
        return 2;
    }

    if (input == input.root_path()) {
        cerr << "FICO: Refusing to archive a drive root directly.\n";
        return 2;
    }

    fs::path output;
    int index = 4;

    if (index < argc && string(argv[index]).rfind("--", 0) != 0) {
        output = argPath(argv[index++]);
    } else {
        output = fs::path(input.wstring() +
            (format == "7z" ? L".7z" :
             format == "zip" ? L".zip" : L".rar"));
    }

    int level = 5;
    bool overwrite = false;
    wstring split;
    vector<wstring> excludes;

    while (index < argc) {
        string option = argv[index++];

        if (option == "--level") {
            if (index >= argc) {
                cerr << "FICO: --level requires a number from 1 to 9.\n";
                return 2;
            }

            try {
                size_t consumed = 0;
                string value = argv[index++];
                level = stoi(value, &consumed);
                if (consumed != value.size() || level < 1 || level > 9)
                    throw invalid_argument("level");
            } catch (...) {
                cerr << "FICO: Invalid compression level.\n";
                return 2;
            }
        } else if (option == "--exclude") {
            if (index >= argc) {
                cerr << "FICO: --exclude requires a pattern.\n";
                return 2;
            }
            excludes.push_back(widen(argv[index++]));
        } else if (option == "--split") {
            if (index >= argc ||
                !parseSize(widen(argv[index++]), split)) {
                cerr << "FICO: Invalid split size. Example: 100M\n";
                return 2;
            }
        } else if (option == "--overwrite") {
            overwrite = true;
        } else {
            cerr << "FICO: Unknown option: " << option << "\n";
            return 2;
        }
    }

    error_code ec;
    fs::path absoluteInput = fs::absolute(input, ec);
    if (ec) absoluteInput = input;

    fs::path absoluteOutput = fs::absolute(output, ec);
    if (ec) absoluteOutput = output;

    if (absoluteInput == absoluteOutput ||
        isWithin(absoluteOutput, absoluteInput)) {
        cerr << "FICO: Output cannot be inside the input directory.\n";
        return 2;
    }

    if (existsPath(output) && !overwrite) {
        cerr << "FICO: Output exists. Choose another name or use --overwrite.\n";
        return 2;
    }

    fs::path tool = format == "rar" ? tools.rar : tools.seven;

    if (tool.empty()) {
        cerr << "FICO: Required tool not found. Run: fico tools\n";
        return 3;
    }

    vector<wstring> args = {L"a"};

    if (format == "7z") {
        args.push_back(L"-t7z");
    } else if (format == "zip") {
        args.push_back(L"-tzip");
    }

    args.push_back(L"-mx=" + to_wstring(level));

    if (format == "rar" && level == 1)
        args.back() = L"-m1";
    else if (format == "rar" && level <= 3)
        args.back() = L"-m3";
    else if (format == "rar" && level <= 5)
        args.back() = L"-m5";
    else if (format == "rar")
        args.back() = L"-m5";

    if (overwrite)
        args.push_back(L"-y");

    if (!split.empty()) {
        if (format == "rar")
            args.push_back(L"-v" + split);
        else
            args.push_back(L"-v" + split);
    }

    for (const auto& pattern : excludes)
        args.push_back(L"-xr!" + pattern);

    args.push_back(fs::absolute(output).wstring());
    args.push_back(absoluteInput.wstring());

    cout << "FICO: Creating " << format << " archive...\n";
    cout << "Input size: " << humanSize(directorySize(input)) << "\n";

    auto start = chrono::steady_clock::now();
    int result = runTool(tool, args);
    auto elapsed = chrono::duration<double>(
        chrono::steady_clock::now() - start).count();

    if (result == 0) {
        cout << "FICO: Compression completed.\n";
        cout << "Output: " << narrow(fs::absolute(output).wstring()) << "\n";
        cout << "Elapsed: " << fixed << setprecision(2)
             << elapsed << " seconds\n";

        if (regularFile(output)) {
            uintmax_t original = directorySize(input);
            uintmax_t packed = fileSize(output);

            cout << "Archive size: " << humanSize(packed) << "\n";

            if (original > 0) {
                double ratio = 100.0 * packed / original;
                cout << "Size ratio: " << fixed << setprecision(2)
                     << ratio << "% of input size\n";
            }
        }
    } else {
        cerr << "FICO: Archiver exited with code " << result << "\n";
    }

    return result;
}

int extract(const Tools& tools, int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        help();
        return 2;
    }

    fs::path archive = argPath(argv[2]);

    if (!regularFile(archive)) {
        cerr << "FICO: Archive not found.\n";
        return 2;
    }

    fs::path destination;

    if (argc == 4) {
        destination = argPath(argv[3]);
    } else {
        destination = archive;
        destination.replace_extension();
    }

    error_code ec;

    fs::path absoluteArchive = fs::weakly_canonical(archive, ec);
    if (ec) absoluteArchive = fs::absolute(archive);

    fs::path absoluteDestination = fs::absolute(destination, ec);
    if (ec) absoluteDestination = destination;

    if (absoluteArchive == absoluteDestination ||
        isWithin(absoluteDestination, absoluteArchive)) {
        cerr << "FICO: Invalid destination path.\n";
        return 2;
    }

    if (existsPath(destination)) {
        if (!fs::is_directory(destination)) {
            cerr << "FICO: Destination exists and is not a directory.\n";
            return 2;
        }

        if (!fs::is_empty(destination, ec) || ec) {
            cerr << "FICO: Destination must be empty to avoid overwriting files.\n";
            return 2;
        }
    } else {
        if (!fs::create_directories(destination, ec) || ec) {
            cerr << "FICO: Could not create destination directory.\n";
            return 2;
        }
    }

    if (tools.seven.empty()) {
        cerr << "FICO: 7-Zip is required for extraction.\n";
        return 3;
    }

    // The external extractor is used for extraction. Archive entries can
    // still contain malicious paths or links, so only use trusted archives.
    vector<wstring> args = {
        L"x",
        L"-y",
        L"-o" + fs::absolute(destination).wstring(),
        fs::absolute(archive).wstring()
    };

    cout << "FICO: Extracting archive...\n";
    int result = runTool(tools.seven, args);

    if (result == 0)
        cout << "FICO: Extracted to "
             << narrow(fs::absolute(destination).wstring()) << "\n";
    else
        cerr << "FICO: Extraction failed with code " << result << "\n";

    return result;
}

int listArchive(const Tools& tools, int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: fico list <archive>\n";
        return 2;
    }

    fs::path archive = argPath(argv[2]);

    if (!regularFile(archive)) {
        cerr << "FICO: Archive not found.\n";
        return 2;
    }

    if (tools.seven.empty()) {
        cerr << "FICO: 7-Zip is required to list archives.\n";
        return 3;
    }

    return runTool(tools.seven, {
        L"l",
        fs::absolute(archive).wstring()
    });
}

int testArchive(const Tools& tools, int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: fico test <archive>\n";
        return 2;
    }

    fs::path archive = argPath(argv[2]);

    if (!regularFile(archive)) {
        cerr << "FICO: Archive not found.\n";
        return 2;
    }

    if (tools.seven.empty()) {
        cerr << "FICO: 7-Zip is required to test archives.\n";
        return 3;
    }

    cout << "FICO: Testing archive integrity...\n";

    int result = runTool(tools.seven, {
        L"t",
        fs::absolute(archive).wstring()
    });

    if (result == 0)
        cout << "FICO: Archive test passed.\n";
    else
        cerr << "FICO: Archive test failed with code "
             << result << "\n";

    return result;
}

// ------------------------------------------------------------
// SHA-256 using Windows CNG (bcrypt.dll)
// ------------------------------------------------------------

string sha256(const fs::path& file, bool& ok) {
    ok = false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    PUCHAR hashObject = nullptr;

    DWORD objectSize = 0;
    DWORD resultSize = 0;
    DWORD hashSize = 0;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);

    if (status < 0)
        return "";

    status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),
        sizeof(objectSize), &resultSize, 0);

    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return "";
    }

    status = BCryptGetProperty(
        algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashSize),
        sizeof(hashSize), &resultSize, 0);

    if (status < 0 || hashSize != 32) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return "";
    }

    vector<UCHAR> object(objectSize);
    vector<UCHAR> digest(hashSize);

    status = BCryptCreateHash(
        algorithm, &hash, object.data(), objectSize,
        nullptr, 0, 0);

    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return "";
    }

    ifstream input(file, ios::binary);

    if (!input) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return "";
    }

    vector<char> buffer(1024 * 1024);

    while (input) {
        input.read(buffer.data(),
                   static_cast<streamsize>(buffer.size()));

        streamsize count = input.gcount();

        if (count > 0) {
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count), 0);

            if (status < 0) {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return "";
            }
        }
    }

    if (input.bad()) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return "";
    }

    status = BCryptFinishHash(
        hash, digest.data(), hashSize, 0);

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (status < 0)
        return "";

    ostringstream out;
    out << hex << setfill('0');

    for (UCHAR b : digest)
        out << setw(2) << static_cast<unsigned int>(b);

    ok = true;
    return out.str();
}

int hashFile(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: fico hash <file>\n";
        return 2;
    }

    fs::path file = argPath(argv[2]);

    if (!regularFile(file)) {
        cerr << "FICO: File not found.\n";
        return 2;
    }

    bool ok = false;
    string digest = sha256(file, ok);

    if (!ok) {
        cerr << "FICO: SHA-256 calculation failed.\n";
        return 1;
    }

    cout << "SHA-256  " << digest << "\n";
    cout << "File     " << narrow(file.wstring()) << "\n";
    return 0;
}

int verifyHash(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Usage: fico verify-hash <file> <sha256>\n";
        return 2;
    }

    fs::path file = argPath(argv[2]);

    if (!regularFile(file)) {
        cerr << "FICO: File not found.\n";
        return 2;
    }

    string expected = lower(argv[3]);

    if (expected.size() != 64 ||
        !all_of(expected.begin(), expected.end(),
            [](unsigned char c) { return isxdigit(c); })) {
        cerr << "FICO: SHA-256 must contain 64 hexadecimal characters.\n";
        return 2;
    }

    bool ok = false;
    string actual = sha256(file, ok);

    if (!ok) {
        cerr << "FICO: SHA-256 calculation failed.\n";
        return 1;
    }

    if (actual == expected) {
        cout << "FICO: Hash matches.\n";
        return 0;
    }

    cout << "FICO: Hash does not match.\n";
    cout << "Expected: " << expected << "\n";
    cout << "Actual:   " << actual << "\n";
    return 1;
}

int benchmark(const Tools& tools, int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: fico benchmark <file-or-folder>\n";
        return 2;
    }

    if (tools.seven.empty()) {
        cerr << "FICO: 7-Zip is required for benchmarking.\n";
        return 3;
    }

    fs::path input = argPath(argv[2]);

    if (!existsPath(input)) {
        cerr << "FICO: Input does not exist.\n";
        return 2;
    }

    fs::path tempDir = fs::temp_directory_path() /
        (L"fico_benchmark_" + to_wstring(GetCurrentProcessId()) +
         L"_" + to_wstring(GetTickCount64()));

    error_code ec;

    if (!fs::create_directories(tempDir, ec) || ec) {
        cerr << "FICO: Could not create temporary directory.\n";
        return 1;
    }

    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{tempDir};

    vector<int> levels = {1, 5, 9};
    bool failed = false;

    cout << "FICO: Benchmarking " << narrow(input.wstring()) << "\n";
    cout << "Input size: " << humanSize(directorySize(input)) << "\n\n";

    for (int level : levels) {
        fs::path output = tempDir /
            (L"benchmark_" + to_wstring(level) + L".7z");

        vector<wstring> args = {
            L"a",
            L"-t7z",
            L"-mx=" + to_wstring(level),
            output.wstring(),
            fs::absolute(input).wstring()
        };

        auto start = chrono::steady_clock::now();
        int result = runTool(tools.seven, args);
        double seconds = chrono::duration<double>(
            chrono::steady_clock::now() - start).count();

        if (result != 0 || !regularFile(output)) {
            cout << "Level " << level << ": failed ("
                 << result << ")\n";
            failed = true;
            continue;
        }

        cout << "Level " << level
             << " | Archive: " << humanSize(fileSize(output))
             << " | Time: " << fixed << setprecision(2)
             << seconds << " s\n";
    }

    cout << "\nFICO: Benchmark complete.\n";
    return failed ? 1 : 0;
}

int toolsCommand(const Tools& tools) {
    wcout << L"FICO tools\n\n";

    wcout << L"7-Zip: "
          << (tools.seven.empty() ? L"not found" : L"available")
          << L"\n";

    if (!tools.seven.empty())
        wcout << L"  Path: " << tools.seven.wstring() << L"\n";

    wcout << L"WinRAR RAR creator: "
          << (tools.rar.empty() ? L"not found" : L"available")
          << L"\n";

    if (!tools.rar.empty())
        wcout << L"  Path: " << tools.rar.wstring() << L"\n";

    return 0;
}

} // namespace FICO

int wmain(int argc, wchar_t* wargv[]) {
    // Convert Windows Unicode arguments to UTF-8 for the CLI parser.
    vector<string> storage;
    vector<char*> argv;

    for (int i = 0; i < argc; ++i)
        storage.push_back(FICO::narrow(wargv[i]));

    for (auto& arg : storage)
        argv.push_back(arg.data());

    argv.push_back(nullptr);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc < 2) {
        FICO::help();
        return 0;
    }

    string command = FICO::lower(storage[1]);
    FICO::Tools tools = FICO::detectTools();

    if (command == "help" || command == "--help" || command == "-h") {
        FICO::help();
        return 0;
    }

    if (command == "version" || command == "--version") {
        cout << "FICO " << FICO::VERSION << " (Windows)\n";
        return 0;
    }

    if (command == "tools")
        return FICO::toolsCommand(tools);

    if (command == "compress")
        return FICO::compress(tools, argc, argv.data());

    if (command == "extract")
        return FICO::extract(tools, argc, argv.data());

    if (command == "list")
        return FICO::listArchive(tools, argc, argv.data());

    if (command == "test")
        return FICO::testArchive(tools, argc, argv.data());

    if (command == "hash")
        return FICO::hashFile(argc, argv.data());

    if (command == "verify-hash")
        return FICO::verifyHash(argc, argv.data());

    if (command == "benchmark")
        return FICO::benchmark(tools, argc, argv.data());

    cerr << "FICO: Unknown command: " << command << "\n";
    cerr << "Run 'fico help' for usage.\n";
    return 2;
}