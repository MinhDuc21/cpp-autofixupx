#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <deque>
#include <windows.h>

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <deque>
#include <string>

static std::deque<std::string> g_ClipboardItems;

std::string WideToUTF8(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

void CaptureClipboardNow() {
    if (!OpenClipboard(nullptr)) return;

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData));
            if (pText) {
                // g_ClipboardItems.push_front("[text] " + WideToUTF8(pText));
                g_ClipboardItems.push_front(WideToUTF8(pText));
                if (g_ClipboardItems.size() > 100)
                    g_ClipboardItems.pop_back();
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();
}

bool copy_to_clipboard(const char* text, HWND hwnd)
{
    if (OpenClipboard(hwnd))
    {
        EmptyClipboard();
        HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, strlen(text) + 1);
        if (hglbCopy)
        {
            char* pchData = (char*)GlobalLock(hglbCopy);
            if (pchData)
            {
                strcpy(pchData, text);
                GlobalUnlock(hglbCopy);
                SetClipboardData(CF_TEXT, hglbCopy);
                ImGui::SameLine();
                ImGui::Text("Link copied to clipboard!"); // Show a message that the link was copied
                // Optionally, you can also show a message box or use ImGui to display a confirmation message
                // MessageBox(hwnd, "Link copied to clipboard!", "Info", MB_OK | MB_ICONINFORMATION);
                CloseClipboard();
                return true;
            }
            GlobalFree(hglbCopy);
        }
        CloseClipboard();
    }
    return false;
}

char* replace_substring(const char* original, const char* target, const char* replacement) {
    const char* pos = strstr(original, target);
    if (!pos) return strdup(original); // target not found, return copy of original

    size_t before_len = pos - original;
    size_t target_len = strlen(target);
    size_t replacement_len = strlen(replacement);
    size_t original_len = strlen(original);

    // Calculate new length and allocate memory
    size_t new_len = before_len + replacement_len + (original_len - before_len - target_len);
    char* result = (char*)malloc(new_len + 1);
    if (!result) return NULL;

    // Copy parts into result
    strncpy(result, original, before_len);
    strcpy(result + before_len, replacement);
    strcpy(result + before_len + replacement_len, pos + target_len);

    return result;
}

bool check_if_valid_twitter_link(const std::string& link) {
    // Check if the link starts with "https://x.com/" or "https://www.x.com/"
    return link.starts_with("https://x.com/") || link.starts_with("https://www.x.com/");
}

std::string convert_twitter_link(const std::string& link) {
    // Convert "x.com" to "fixupx.com"
    if (link.starts_with("https://x.com/") || link.starts_with("https://www.x.com/")) {
        std::string new_link = link;
        size_t pos = new_link.find("x.com");
        if (pos != std::string::npos) {
            new_link.replace(pos, 5, "fixupx.com");
        }
        return new_link;
    }
    return link;
}

std::string convert_to_d_prefix(const std::string& link) {
    // Convert "fixupx.com" to "d.fixupx.com"
    if (link.starts_with("https://fixupx.com/") || link.starts_with("https://www.fixupx.com/")) {
        std::string new_link = link;
        size_t pos = new_link.find("fixupx.com");
        if (pos != std::string::npos) {
            new_link.replace(pos, 10, "d.fixupx.com");
        }
        return new_link;
    }    
    return link;
}

// Main code
int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"FxTwitter Link Converter", WS_OVERLAPPEDWINDOW, 100, 100, (int)(650 * main_scale), (int)(350 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);
    AddClipboardFormatListener(hwnd);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    style.FontSizeBase = 16.0f;
    //io.Fonts->AddFontDefault();
    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle lost D3D9 device
        if (g_DeviceLost)
        {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST)
            {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // get the main window size and set the size of the custom window
        RECT rect;
        GetClientRect(hwnd, &rect);
        ImVec2 window_size = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::Begin("Custom Title Bar Color", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        static int e = 0;
        ImGui::RadioButton("Manual", &e, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Automatic", &e, 1);

        if (e == 0) 
        {
            // enter a twitter link into a text input and transform it into a fxtwitter link then transform it into a clickable link
            static char twitter_link[256] = "https://x.com/ExampleUser/status/1234567890123456789"; // Default link to show in the input box
            ImGui::InputText("##l1", twitter_link, IM_ARRAYSIZE(twitter_link));
            ImGui::SameLine();
            // add a button to paste the link from clipboard
            if (ImGui::Button("Paste Link from Clipboard"))
            {
                if (OpenClipboard(hwnd))
                {
                    HANDLE hData = GetClipboardData(CF_TEXT);
                    if (hData)
                    {
                        char* clipboard_text = static_cast<char*>(GlobalLock(hData));
                        if (clipboard_text)
                        {
                            strncpy(twitter_link, clipboard_text, IM_ARRAYSIZE(twitter_link) - 1);
                            twitter_link[IM_ARRAYSIZE(twitter_link) - 1] = '\0'; // Ensure null-termination
                            GlobalUnlock(hData);
                        }
                    }
                    CloseClipboard();
                }
            }
            static unsigned int transform_count = 0; 
            static char transformed_link[256]; // default link to show after transformation
            // Track the previous input to detect changes
            static char prev_twitter_link[256] = "";

            // Update transformed_link in real time as twitter_link changes
            if (strcmp(prev_twitter_link, twitter_link) != 0) {
                strcpy(prev_twitter_link, twitter_link);
                if (strlen(twitter_link) > 0) {
                    char* pos = strstr(twitter_link, "x.com");
                    if (pos) {
                        char* new_link = replace_substring(twitter_link, "x.com", "fixupx.com");
                        if (new_link) {
                            strcpy(transformed_link, new_link);
                            free(new_link);
                        } else {
                            strcpy(transformed_link, "Error transforming link");
                        }
                    } else {
                        strcpy(transformed_link, "No 'x.com' found in link");
                    }
                } else {
                    transformed_link[0] = '\0';
                }
            }

            // add a checkbox to turn the fixupx part to d.fixupx
            static bool use_d_prefix = false;
            ImGui::Checkbox("Use 'd.' prefix", &use_d_prefix);
            // Always keep the original transformed link up to date
            static char original_transformed_link[256] = "";
            strcpy(original_transformed_link, transformed_link);

            // Prepare a display/copy buffer based on the prefix checkbox
            char display_link[256];
            if (use_d_prefix && strlen(original_transformed_link) > 0) {
                char* pos = strstr(original_transformed_link, "fixupx.com");
                if (pos) {
                    char* new_link = replace_substring(original_transformed_link, "fixupx.com", "d.fixupx.com");
                    if (new_link) {
                        strcpy(display_link, new_link);
                        free(new_link);
                    } 
                    else {
                        strcpy(display_link, "Error transforming link");
                    }
                } 
                else {
                    strcpy(display_link, original_transformed_link);
                }
            } 
            else {
                strcpy(display_link, original_transformed_link);
            }
            // Show the display_link in a read-only input box
            ImGui::InputText("Display Link", display_link, IM_ARRAYSIZE(display_link), ImGuiInputTextFlags_ReadOnly);
            // add button to copy the link to clipboard
            if (ImGui::Button("Copy Link to Clipboard##01"))
            {
                // copy the link to clipboard
                copy_to_clipboard(display_link, hwnd);
            }

            // add a display box that shows the transformed link with "/video/1" appended
            static char video_link[256] = "";
            if (strlen(display_link) > 0) {
                strcpy(video_link, display_link);
            } else {
                video_link[0] = '\0'; // Clear the video link if display link is empty
            }
            static int video_link_num = 0; // From 1 to 4
            ImGui::RadioButton("Video Link 1", &video_link_num, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Video Link 2", &video_link_num, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Video Link 3", &video_link_num, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Video Link 4", &video_link_num, 3);
            {
                strcat(video_link, "/video/");
                char video_link_num_str[4];
                sprintf(video_link_num_str, "%d", video_link_num + 1);
                strcat(video_link, video_link_num_str);
            }
            
            ImGui::InputText("Video Link", video_link, IM_ARRAYSIZE(video_link), ImGuiInputTextFlags_ReadOnly);
            
            // add button to copy the link to clipboard
            if (ImGui::Button("Copy Link to Clipboard##02"))
            {
                // copy the link to clipboard
                copy_to_clipboard(video_link, hwnd);
            }
            
        }
        else if (e == 1)
        {
            ImGui::SeparatorText("Options");
            static int f = 0;
            ImGui::RadioButton("fixupx##fx", &f, 0);
            ImGui::SameLine();
            ImGui::RadioButton("d.fixupx##dx", &f, 1);

            static bool beep_on_clipboard = true;
            ImGui::Checkbox("Beep on Clipboard Change", &beep_on_clipboard);
            
            // every time the clipboard is updated, put the new item in a text input box, and don't need buttons
            static std::string last_copied_clipboard_item;
            static int last_copied_mode = -1;
            if (g_ClipboardItems.size() > 0) {
                char clipboard_buf[256];
                strncpy(clipboard_buf, g_ClipboardItems.front().c_str(), sizeof(clipboard_buf) - 1);
                clipboard_buf[sizeof(clipboard_buf) - 1] = '\0';
                ImGui::InputText("Last Clipboard Item", clipboard_buf, sizeof(clipboard_buf), ImGuiInputTextFlags_ReadOnly);
                if (check_if_valid_twitter_link(g_ClipboardItems.front())) {
                    if (g_ClipboardItems.front() != last_copied_clipboard_item || f != last_copied_mode) {
                        if (f == 0) {
                            copy_to_clipboard(convert_twitter_link(g_ClipboardItems.front()).c_str(), hwnd);
                            if (beep_on_clipboard) { 
                                Beep(800, 200); // Optional: Beep to indicate the link was copied
                            }
                        } else if (f == 1) {
                            copy_to_clipboard(convert_to_d_prefix(convert_twitter_link(g_ClipboardItems.front())).c_str(), hwnd); 
                            if (beep_on_clipboard) { 
                                Beep(800, 200); // Optional: Beep to indicate the link was copied
                            }
                        }
                        last_copied_clipboard_item = g_ClipboardItems.front();
                        last_copied_mode = f;
                    }
                }
            }

            static bool clipboard_history = false;
            ImGui::Checkbox("Show Clipboard Log", &clipboard_history);
            if (clipboard_history) {
                ImGui::BeginChild("Clipboard Log", ImVec2(0, 100), true);
                for (const auto& item : g_ClipboardItems) {
                    ImGui::TextUnformatted(item.c_str());
                    if (ImGui::IsItemClicked()) {
                        // Copy the clicked item to clipboard
                        copy_to_clipboard(item.c_str(), hwnd);
                    }
                }
                ImGui::EndChild();
            }

        }
        ImGui::End();

        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x*clear_color.w*255.0f), (int)(clear_color.y*clear_color.w*255.0f), (int)(clear_color.z*clear_color.w*255.0f), (int)(clear_color.w*255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;
    }

    // Cleanup
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_CLIPBOARDUPDATE:
        CaptureClipboardNow();
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}