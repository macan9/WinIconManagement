#include "Desktop/DesktopIconService.h"

#include <CommCtrl.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "Infrastructure/Win32Handles.h"

namespace {
constexpr DWORD kListViewSendTimeoutMs = 1000;
constexpr SIZE_T kIconTextMaxChars = 512;
constexpr int kMinDesktopCoordinate = -32768;
constexpr int kMaxDesktopCoordinate = 32767;
constexpr int kDesktopHitTestUnavailable = -2;

bool SendListViewMessage(
    HWND listViewWindow,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    DWORD_PTR* outResult) {
    DWORD_PTR localResult = 0;
    const LRESULT sendResult = SendMessageTimeoutW(
        listViewWindow,
        message,
        wParam,
        lParam,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        kListViewSendTimeoutMs,
        &localResult);
    if (outResult != nullptr) {
        *outResult = localResult;
    }
    return sendResult != 0;
}

class RemoteBuffer {
public:
    RemoteBuffer(HANDLE processHandle, SIZE_T byteSize)
        : processHandle_(processHandle), byteSize_(byteSize), remoteAddress_(nullptr) {
        if (processHandle_ != nullptr && byteSize_ > 0) {
            remoteAddress_ =
                VirtualAllocEx(processHandle_, nullptr, byteSize_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
    }

    ~RemoteBuffer() {
        if (processHandle_ != nullptr && remoteAddress_ != nullptr) {
            VirtualFreeEx(processHandle_, remoteAddress_, 0, MEM_RELEASE);
            remoteAddress_ = nullptr;
        }
    }

    RemoteBuffer(const RemoteBuffer&) = delete;
    RemoteBuffer& operator=(const RemoteBuffer&) = delete;

    [[nodiscard]] bool IsValid() const {
        return remoteAddress_ != nullptr;
    }

    [[nodiscard]] void* Get() const {
        return remoteAddress_;
    }

    [[nodiscard]] SIZE_T Size() const {
        return byteSize_;
    }

    template <typename T>
    [[nodiscard]] bool WriteObject(const T& value) const {
        return WriteBytes(&value, sizeof(T), 0);
    }

    [[nodiscard]] bool WriteBytes(const void* source, SIZE_T byteCount, SIZE_T offset) const {
        if (!IsRangeValid(byteCount, offset)) {
            return false;
        }

        SIZE_T bytesWritten = 0;
        BYTE* base = static_cast<BYTE*>(remoteAddress_);
        return WriteProcessMemory(processHandle_, base + offset, source, byteCount, &bytesWritten) != 0 &&
               bytesWritten == byteCount;
    }

    [[nodiscard]] bool ReadBytes(void* destination, SIZE_T byteCount, SIZE_T offset) const {
        if (!IsRangeValid(byteCount, offset)) {
            return false;
        }

        SIZE_T bytesRead = 0;
        const BYTE* base = static_cast<const BYTE*>(remoteAddress_);
        return ReadProcessMemory(processHandle_, base + offset, destination, byteCount, &bytesRead) != 0 &&
               bytesRead == byteCount;
    }

private:
    [[nodiscard]] bool IsRangeValid(SIZE_T byteCount, SIZE_T offset) const {
        return processHandle_ != nullptr && remoteAddress_ != nullptr && offset <= byteSize_ &&
               byteCount <= (byteSize_ - offset);
    }

    HANDLE processHandle_;
    SIZE_T byteSize_;
    void* remoteAddress_;
};

[[nodiscard]] bool IsCoordinateInRange(int value) {
    return value >= kMinDesktopCoordinate && value <= kMaxDesktopCoordinate;
}

[[nodiscard]] std::wstring ExtractRemoteString(const std::vector<wchar_t>& buffer, DWORD_PTR charsWritten) {
    if (buffer.empty()) {
        return L"";
    }

    SIZE_T maxChars = buffer.size() - 1;
    SIZE_T length = std::min<SIZE_T>(static_cast<SIZE_T>(charsWritten), maxChars);
    while (length < maxChars && buffer[length] != L'\0') {
        ++length;
    }
    if (length == maxChars) {
        length = 0;
        while (length < maxChars && buffer[length] != L'\0') {
            ++length;
        }
    }
    return std::wstring(buffer.data(), length);
}

bool ReadDesktopIconPosition(
    HWND listViewWindow,
    int iconIndex,
    const RemoteBuffer& remotePointBuffer,
    POINT* outPoint) {
    if (outPoint == nullptr || !remotePointBuffer.IsValid()) {
        return false;
    }

    DWORD_PTR messageResult = 0;
    if (!SendListViewMessage(
            listViewWindow,
            LVM_GETITEMPOSITION,
            static_cast<WPARAM>(iconIndex),
            reinterpret_cast<LPARAM>(remotePointBuffer.Get()),
            &messageResult) ||
        messageResult == FALSE) {
        return false;
    }

    POINT localPoint{};
    if (!remotePointBuffer.ReadBytes(&localPoint, sizeof(localPoint), 0)) {
        return false;
    }

    *outPoint = localPoint;
    return true;
}

bool ReadDesktopIconName(
    HWND listViewWindow,
    int iconIndex,
    const RemoteBuffer& remoteItemBuffer,
    const RemoteBuffer& remoteTextBuffer,
    std::wstring* outName) {
    if (outName == nullptr || !remoteItemBuffer.IsValid() || !remoteTextBuffer.IsValid()) {
        return false;
    }

    LVITEMW remoteItem{};
    remoteItem.iSubItem = 0;
    remoteItem.pszText = static_cast<LPWSTR>(remoteTextBuffer.Get());
    remoteItem.cchTextMax = static_cast<int>(remoteTextBuffer.Size() / sizeof(wchar_t));

    if (!remoteItemBuffer.WriteObject(remoteItem)) {
        return false;
    }

    DWORD_PTR charsWritten = 0;
    if (!SendListViewMessage(
            listViewWindow,
            LVM_GETITEMTEXTW,
            static_cast<WPARAM>(iconIndex),
            reinterpret_cast<LPARAM>(remoteItemBuffer.Get()),
            &charsWritten)) {
        return false;
    }

    std::vector<wchar_t> textBuffer(kIconTextMaxChars, L'\0');
    if (!remoteTextBuffer.ReadBytes(textBuffer.data(), textBuffer.size() * sizeof(wchar_t), 0)) {
        return false;
    }

    *outName = ExtractRemoteString(textBuffer, charsWritten);
    return true;
}

bool SetDesktopIconPositionCore(
    HWND listViewWindow,
    int iconIndex,
    POINT targetPosition,
    const RemoteBuffer& remotePointBuffer) {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow) || iconIndex < 0) {
        return false;
    }
    if (!IsCoordinateInRange(targetPosition.x) || !IsCoordinateInRange(targetPosition.y)) {
        return false;
    }
    if (!remotePointBuffer.IsValid() || remotePointBuffer.Size() < sizeof(POINT)) {
        return false;
    }
    if (!remotePointBuffer.WriteObject(targetPosition)) {
        return false;
    }

    DWORD_PTR ignoredResult = 0;
    return SendListViewMessage(
        listViewWindow,
        LVM_SETITEMPOSITION32,
        static_cast<WPARAM>(iconIndex),
        reinterpret_cast<LPARAM>(remotePointBuffer.Get()),
        &ignoredResult);
}
}  // namespace

namespace Desktop {
int DesktopIconService::GetDesktopIconCount(HWND listViewWindow) const {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow)) {
        return 0;
    }

    DWORD_PTR count = 0;
    if (!SendListViewMessage(listViewWindow, LVM_GETITEMCOUNT, 0, 0, &count)) {
        return 0;
    }

    if (count > static_cast<DWORD_PTR>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(count);
}

std::vector<DesktopIcon> DesktopIconService::EnumerateDesktopIcons(
    HWND listViewWindow,
    DWORD explorerProcessId) const {
    std::vector<DesktopIcon> icons;
    const int iconCount = GetDesktopIconCount(listViewWindow);
    if (iconCount <= 0 || explorerProcessId == 0) {
        return icons;
    }

    Infrastructure::UniqueKernelHandle explorerProcess(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        explorerProcessId));
    if (!explorerProcess.IsValid()) {
        return icons;
    }

    const SIZE_T iconTextByteSize = kIconTextMaxChars * sizeof(wchar_t);
    RemoteBuffer remotePointBuffer(explorerProcess.Get(), sizeof(POINT));
    RemoteBuffer remoteItemBuffer(explorerProcess.Get(), sizeof(LVITEMW));
    RemoteBuffer remoteTextBuffer(explorerProcess.Get(), iconTextByteSize);
    if (!remotePointBuffer.IsValid() || !remoteItemBuffer.IsValid() || !remoteTextBuffer.IsValid()) {
        return icons;
    }

    icons.reserve(static_cast<size_t>(iconCount));
    for (int index = 0; index < iconCount; ++index) {
        DesktopIcon icon{};
        icon.index = index;

        if (!ReadDesktopIconPosition(listViewWindow, index, remotePointBuffer, &icon.position)) {
            continue;
        }
        if (!ReadDesktopIconName(listViewWindow, index, remoteItemBuffer, remoteTextBuffer, &icon.displayName)) {
            continue;
        }

        icons.push_back(std::move(icon));
    }
    return icons;
}

bool DesktopIconService::CanAccessExplorerProcess(DWORD explorerProcessId) const {
    if (explorerProcessId == 0) {
        return false;
    }

    Infrastructure::UniqueKernelHandle explorerProcess(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        explorerProcessId));
    return explorerProcess.IsValid();
}

bool DesktopIconService::SetDesktopIconPosition(
    HWND listViewWindow,
    DWORD explorerProcessId,
    int iconIndex,
    POINT targetPosition) const {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow) || explorerProcessId == 0 || iconIndex < 0) {
        return false;
    }
    if (!IsCoordinateInRange(targetPosition.x) || !IsCoordinateInRange(targetPosition.y)) {
        return false;
    }
    if (!CanAccessExplorerProcess(explorerProcessId)) {
        return false;
    }

    Infrastructure::UniqueKernelHandle explorerProcess(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        explorerProcessId));
    if (!explorerProcess.IsValid()) {
        return false;
    }

    RemoteBuffer remotePointBuffer(explorerProcess.Get(), sizeof(POINT));
    if (!remotePointBuffer.IsValid()) {
        return false;
    }

    if (!SetDesktopIconPositionCore(listViewWindow, iconIndex, targetPosition, remotePointBuffer)) {
        return false;
    }

    POINT verifyPosition{};
    if (!ReadDesktopIconPosition(listViewWindow, iconIndex, remotePointBuffer, &verifyPosition)) {
        return false;
    }

    const bool refreshed = RefreshDesktopIcon(listViewWindow, iconIndex);
    (void)refreshed;
    return true;
}

int DesktopIconService::MoveDesktopIcons(
    HWND listViewWindow,
    DWORD explorerProcessId,
    const std::vector<DesktopIcon>& iconsToMove) const {
    if (iconsToMove.empty()) {
        return 0;
    }
    if (listViewWindow == nullptr || !IsWindow(listViewWindow) || explorerProcessId == 0) {
        return 0;
    }
    if (!CanAccessExplorerProcess(explorerProcessId)) {
        return 0;
    }

    Infrastructure::UniqueKernelHandle explorerProcess(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        explorerProcessId));
    if (!explorerProcess.IsValid()) {
        return 0;
    }

    RemoteBuffer remotePointBuffer(explorerProcess.Get(), sizeof(POINT));
    if (!remotePointBuffer.IsValid()) {
        return 0;
    }

    int successCount = 0;
    for (const DesktopIcon& icon : iconsToMove) {
        if (SetDesktopIconPositionCore(listViewWindow, icon.index, icon.position, remotePointBuffer)) {
            const bool refreshed = RefreshDesktopIcon(listViewWindow, icon.index);
            (void)refreshed;
            ++successCount;
        }
    }
    return successCount;
}

bool DesktopIconService::RefreshDesktopIcon(HWND listViewWindow, int iconIndex) const {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow) || iconIndex < 0) {
        return false;
    }

    DWORD_PTR messageResult = 0;
    return SendListViewMessage(
        listViewWindow,
        LVM_UPDATE,
        static_cast<WPARAM>(iconIndex),
        0,
        &messageResult);
}

int DesktopIconService::HitTestDesktopIcon(
    HWND listViewWindow,
    DWORD explorerProcessId,
    POINT screenPoint) const {
    if (listViewWindow == nullptr || !IsWindow(listViewWindow) || explorerProcessId == 0) {
        return kDesktopHitTestUnavailable;
    }

    POINT clientPoint = screenPoint;
    if (ScreenToClient(listViewWindow, &clientPoint) == FALSE) {
        return kDesktopHitTestUnavailable;
    }

    RECT clientRect{};
    if (GetClientRect(listViewWindow, &clientRect) == FALSE || PtInRect(&clientRect, clientPoint) == FALSE) {
        return -1;
    }

    Infrastructure::UniqueKernelHandle explorerProcess(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        explorerProcessId));
    if (!explorerProcess.IsValid()) {
        return kDesktopHitTestUnavailable;
    }

    RemoteBuffer remoteHitTestBuffer(explorerProcess.Get(), sizeof(LVHITTESTINFO));
    if (!remoteHitTestBuffer.IsValid()) {
        return kDesktopHitTestUnavailable;
    }

    LVHITTESTINFO hitTestInfo{};
    hitTestInfo.pt = clientPoint;
    if (!remoteHitTestBuffer.WriteObject(hitTestInfo)) {
        return kDesktopHitTestUnavailable;
    }

    DWORD_PTR hitIndex = static_cast<DWORD_PTR>(-1);
    if (!SendListViewMessage(
            listViewWindow,
            LVM_HITTEST,
            0,
            reinterpret_cast<LPARAM>(remoteHitTestBuffer.Get()),
            &hitIndex)) {
        return kDesktopHitTestUnavailable;
    }

    if (hitIndex > static_cast<DWORD_PTR>(std::numeric_limits<int>::max())) {
        return kDesktopHitTestUnavailable;
    }
    return static_cast<int>(hitIndex);
}
}  // namespace Desktop
