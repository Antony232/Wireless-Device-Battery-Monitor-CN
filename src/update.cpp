// The update check: one HTTPS HEAD request to /releases/latest, read the redirect target, compare.
//
// Nothing about the machine or the user is sent; the request carries only a User-Agent with the app
// name. Redirects are disabled on purpose so the Location header is the answer we want (the tag), and
// so a compromised or mistaken redirect can never make the app talk to another host.
#include "update.h"

#include <shellapi.h>
#include <winhttp.h>
#include <winver.h>

#include <string>
#include <vector>

namespace update {
namespace {

const wchar_t kLatest[] = L"/fuyue521/Wireless-Device-Battery-Monitor-CN/releases/latest";
const wchar_t kReleasesPage[] = L"https://github.com/fuyue521/Wireless-Device-Battery-Monitor-CN/releases/latest";
const wchar_t kUserAgent[] = L"Wireless-Device-Battery-Monitor-CN/update-check";

// 0 = idle, 1 = a check is running. Prevents a second worker from piling up on a slow network.
volatile LONG g_running = 0;

struct Job {
    HWND notify;
    UINT msg;
};

void Numbers(const std::wstring& version, int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    int field = 0;
    for (size_t i = 0; i < version.size() && field < 4; ++i) {
        const wchar_t c = version[i];
        if (c >= L'0' && c <= L'9') {
            out[field] = out[field] * 10 + (c - L'0');
        } else if (c == L'.') {
            ++field;
        } else {
            break;
        }
    }
}

// Keeps the leading dotted number run of a tag: "v1.0.3" -> "1.0.3", "1.2" -> "1.2".
std::wstring VersionOf(const std::wstring& tag) {
    size_t i = 0;
    while (i < tag.size() && (tag[i] == L'v' || tag[i] == L'V' || tag[i] == L' ' || tag[i] == L'\t')) ++i;
    std::wstring out;
    for (; i < tag.size(); ++i) {
        const wchar_t c = tag[i];
        if ((c >= L'0' && c <= L'9') || c == L'.') {
            out.push_back(c);
        } else {
            break;
        }
    }
    while (!out.empty() && out.back() == L'.') out.pop_back();
    return out;
}

// The last path segment of the redirect target, i.e. the tag in .../releases/tag/v1.0.3.
std::wstring TagOf(const std::wstring& location) {
    const size_t slash = location.find_last_of(L'/');
    if (slash == std::wstring::npos) return std::wstring();
    return VersionOf(location.substr(slash + 1));
}

std::wstring QueryLatestVersion() {
    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return std::wstring();
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);

    std::wstring result;
    HINTERNET connect = WinHttpConnect(session, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect) {
        HINTERNET request = WinHttpOpenRequest(connect, L"HEAD", kLatest, nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request) {
            DWORD policy = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &policy, sizeof(policy));
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0;
                DWORD size = sizeof(status);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
                    status >= 300 && status < 400) {
                    wchar_t buffer[1024] = {};
                    size = sizeof(buffer);
                    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Location", buffer, &size,
                                            WINHTTP_NO_HEADER_INDEX)) {
                        result = TagOf(buffer);
                    }
                }
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return result;
}

DWORD WINAPI Worker(LPVOID param) {
    Job* job = static_cast<Job*>(param);
    const std::wstring latest = QueryLatestVersion();
    const std::wstring mine = CurrentVersion();
    int verdict = -1;
    if (!latest.empty() && !mine.empty()) verdict = IsNewer(latest, mine) ? 1 : 0;

    auto* text = new std::wstring(latest);
    if (!PostMessageW(job->notify, job->msg, static_cast<WPARAM>(verdict), reinterpret_cast<LPARAM>(text))) {
        delete text;
    }
    delete job;
    InterlockedExchange(&g_running, 0);
    return 0;
}

}  // namespace

std::wstring CurrentVersion() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::wstring();

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (size == 0) return std::wstring();
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return std::wstring();

    VS_FIXEDFILEINFO* info = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &bytes) || !info) {
        return std::wstring();
    }
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) +
           L"." + std::to_wstring(HIWORD(info->dwFileVersionLS));
}

bool IsNewer(const std::wstring& a, const std::wstring& b) {
    int left[4] = {};
    int right[4] = {};
    Numbers(VersionOf(a), left);
    Numbers(VersionOf(b), right);
    for (int i = 0; i < 4; ++i) {
        if (left[i] != right[i]) return left[i] > right[i];
    }
    return false;
}

bool CheckAsync(HWND notify, UINT msg) {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) return false;
    auto* job = new Job{ notify, msg };
    HANDLE thread = CreateThread(nullptr, 0, Worker, job, 0, nullptr);
    if (!thread) {
        delete job;
        InterlockedExchange(&g_running, 0);
        return false;
    }
    CloseHandle(thread);
    return true;
}

void OpenDownloadPage() {
    ShellExecuteW(nullptr, L"open", kReleasesPage, nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace update
