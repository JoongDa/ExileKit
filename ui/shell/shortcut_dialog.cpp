#include "shortcut_dialog.h"
#include "dialogs.h"
#include "utf.h"
#include <commctrl.h>
#include <cstring>
#include <vector>
namespace poetoolbox::ui {
namespace {
constexpr int InputId = 2101, BrowseId = 2102;
struct State {
    ApplicationServices &services;
    std::string result;
};
std::wstring T(State &state, std::string_view key) {
    return Utf16(state.services.Tr(key));
}
HWND Control(HWND dialog, DWORD extended, const wchar_t *type, const std::wstring &label, DWORD style, int x, int y,
             int width, int height, int id) {
    RECT rect{x, y, x + width, y + height};
    MapDialogRect(dialog, &rect);
    auto control =
        CreateWindowExW(extended, type, label.c_str(), WS_CHILD | WS_VISIBLE | style, rect.left, rect.top,
                        rect.right - rect.left, rect.bottom - rect.top, dialog,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), TRUE);
    return control;
}
INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *state = reinterpret_cast<State *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State *>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, T(*state, "shortcut.add").c_str());
        Control(dialog, 0, L"STATIC", T(*state, "shortcut.prompt"), 0, 12, 10, 352, 16, 0);
        const auto input =
            Control(dialog, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 12, 31, 267, 18, InputId);
        Control(dialog, 0, L"BUTTON", T(*state, "shortcut.browse"), WS_TABSTOP | BS_PUSHBUTTON, 287, 30, 80, 20,
                BrowseId);
        Control(dialog, 0, L"BUTTON", T(*state, "action.cancel"), WS_TABSTOP | BS_PUSHBUTTON, 203, 70, 76, 22,
                IDCANCEL);
        Control(dialog, 0, L"BUTTON", T(*state, "action.add"), WS_TABSTOP | BS_DEFPUSHBUTTON, 287, 70, 80, 22, IDOK);
        SendMessageW(input, EM_SETLIMITTEXT, 32760, 0);
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        RECT parent{}, rect{};
        GetWindowRect(GetParent(dialog), &parent);
        GetWindowRect(dialog, &rect);
        SetWindowPos(dialog, nullptr, parent.left + (parent.right - parent.left - (rect.right - rect.left)) / 2,
                     parent.top + (parent.bottom - parent.top - (rect.bottom - rect.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(input);
        return FALSE;
    }
    if (!state)
        return FALSE;
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == InputId && HIWORD(wparam) == EN_CHANGE) {
            EnableWindow(GetDlgItem(dialog, IDOK), GetWindowTextLengthW(GetDlgItem(dialog, InputId)) > 0);
            return TRUE;
        }
        if (LOWORD(wparam) == BrowseId) {
            const auto path = PickShortcut(dialog, T(*state, "shortcut.browse"));
            if (path)
                SetWindowTextW(GetDlgItem(dialog, InputId), path->c_str());
            else if (path.error().code != ErrorCode::Cancelled)
                MessageBoxW(dialog, T(*state, "error.invalidShortcut").c_str(), T(*state, "shortcut.add").c_str(),
                            MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK) {
            const auto input = GetDlgItem(dialog, InputId);
            const int size = GetWindowTextLengthW(input);
            std::wstring value(static_cast<size_t>(size) + 1, L'\0');
            GetWindowTextW(input, value.data(), size + 1);
            value.resize(size);
            state->result = Utf8(value);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
} // namespace
Result<std::string> ShowAddShortcutDialog(HWND owner, ApplicationServices &services) {
    DLGTEMPLATE definition{};
    definition.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
    definition.cx = 380;
    definition.cy = 104;
    std::vector<unsigned char> bytes(sizeof(definition));
    std::memcpy(bytes.data(), &definition, sizeof(definition));
    auto word = [&](WORD value) {
        bytes.push_back(static_cast<unsigned char>(value));
        bytes.push_back(static_cast<unsigned char>(value >> 8));
    };
    word(0);
    word(0);
    word(0);
    word(9);
    for (wchar_t ch : std::wstring(L"Segoe UI"))
        word(ch);
    word(0);
    State state{services, {}};
    const auto result =
        DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<DLGTEMPLATE *>(bytes.data()), owner,
                                DialogProc, reinterpret_cast<LPARAM>(&state));
    if (result == IDOK && !state.result.empty())
        return state.result;
    if (result == -1)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot create shortcut dialog.", GetLastError()});
    return std::unexpected(Error{ErrorCode::Cancelled, "Shortcut addition cancelled."});
}
} // namespace poetoolbox::ui
