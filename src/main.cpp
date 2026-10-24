#include "overlay.h"
#include "renderer.h"
#include "memory.h"

#include <imgui.h>
#include <Windows.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static constexpr wchar_t TARGET_WINDOW[]  = L"Roblox";
static constexpr wchar_t TARGET_PROCESS[] = L"RobloxPlayerBeta.exe";

static Memory        g_mem;
static bool          g_menuOpen = true;
static int           g_menuKey  = VK_HOME;
static bool          g_streamproof = true;

// ==========================================================================
// Config
// ==========================================================================
static std::string GetConfigDir()
{
    static std::string s_dir;
    if (!s_dir.empty()) return s_dir;
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* last = strrchr(buf, '\\');
    if (last) *(last + 1) = '\0';
    s_dir = buf;
    return s_dir;
}

static void WriteConfigEntries(FILE* f)
{
    fprintf(f, "[Misc]\n");
    fprintf(f, "misc_menuKey=%d\n", g_menuKey);
    fprintf(f, "misc_streamproof=%d\n", g_streamproof ? 1 : 0);
}

static void ReadConfigEntries(FILE* f)
{
    char line[256];
#define BOOL_KEY(k, field)  if (!strcmp(key, k)) { field = (val[0] == '1'); }
#define INT_KEY(k, field)   if (!strcmp(key, k)) { field = atoi(val); }
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '[' || line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;
        char key[64] = {}, val[64] = {};
        if (sscanf(line, " %63[^=]=%63[^\r\n]", key, val) != 2) continue;
        int klen = (int)strlen(key);
        while (klen > 0 && key[klen-1] == ' ') key[--klen] = '\0';
        INT_KEY("misc_menuKey", g_menuKey)
        BOOL_KEY("misc_streamproof", g_streamproof)
    }
#undef BOOL_KEY
#undef INT_KEY
}

static void SaveConfigSlot(const char* name)
{
    std::string dir = GetConfigDir() + "configs\\";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string path = dir + name + std::string(".ini");
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    WriteConfigEntries(f);
    fclose(f);
}

static bool LoadConfigSlot(const char* name, Overlay* overlay = nullptr)
{
    std::string path = GetConfigDir() + "configs\\" + name + std::string(".ini");
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    ReadConfigEntries(f);
    fclose(f);
    if (overlay) overlay->SetStreamproof(g_streamproof);
    return true;
}

static std::vector<std::string> ListConfigSlots()
{
    std::vector<std::string> slots;
    std::string pattern = GetConfigDir() + "configs\\*.ini";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return slots;
    do {
        std::string name = fd.cFileName;
        if (name.size() > 4) name = name.substr(0, name.size() - 4);
        slots.push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(slots.begin(), slots.end());
    return slots;
}

static std::string GetMetaPath() { return GetConfigDir() + "rss_meta.ini"; }
static void LoadMeta(char* outSlot, int slotBuf, bool& outAutoLoad)
{
    outSlot[0] = '\0'; outAutoLoad = false;
    FILE* f = fopen(GetMetaPath().c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char key[64]={}, val[64]={};
        if (sscanf(line, " %63[^=]=%63[^\r\n]", key, val) != 2) continue;
        if (!strcmp(key, "autoLoad")) outAutoLoad = (val[0] == '1');
        if (!strcmp(key, "autoSlot")) { strncpy(outSlot, val, slotBuf-1); outSlot[slotBuf-1]='\0'; }
    }
    fclose(f);
}

// Corrigido: usa scan code estendido para teclas especiais
static const char* VKToName(int vk)
{
    UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    
    // Adiciona o bit de tecla estendida para teclas que precisam
    switch (vk)
    {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_UP: case VK_DOWN:
        case VK_LEFT: case VK_RIGHT: case VK_DIVIDE: case VK_NUMLOCK:
            sc |= 0x100;
            break;
    }
    
    static wchar_t buf[64];
    if (GetKeyNameTextW(static_cast<LONG>(sc << 16), buf, sizeof(buf) / sizeof(wchar_t)) > 0)
    {
        static char utf8Buf[128];
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8Buf, sizeof(utf8Buf), nullptr, nullptr);
        return utf8Buf;
    }
    return "?";
}

// ==========================================================================
// Vape V4 Style
// ==========================================================================
static void ApplyVapeStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 4.0f;
    style.ChildRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 6);
    style.ItemSpacing       = ImVec2(10, 8);
    style.ItemInnerSpacing  = ImVec2(8, 6);

    ImVec4 accent = ImVec4(0.17f, 0.47f, 0.88f, 1.0f);
    ImVec4 bg     = ImVec4(0.08f, 0.08f, 0.08f, 0.96f);
    ImVec4 bg_hov = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    ImVec4 text   = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
    ImVec4 text_d = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

    colors[ImGuiCol_Text]                  = text;
    colors[ImGuiCol_TextDisabled]          = text_d;
    colors[ImGuiCol_WindowBg]              = bg;
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.10f, 0.10f, 0.96f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.12f, 0.12f, 0.12f, 0.96f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.20f, 0.5f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]        = bg_hov;
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_TitleBg]               = bg;
    colors[ImGuiCol_TitleBgActive]         = bg;
    colors[ImGuiCol_TitleBgCollapsed]      = bg;
    colors[ImGuiCol_MenuBarBg]             = bg;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = accent;
    colors[ImGuiCol_Button]                = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_ButtonHovered]         = bg_hov;
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_Header]                = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    colors[ImGuiCol_HeaderHovered]         = bg_hov;
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    colors[ImGuiCol_SeparatorHovered]      = accent;
    colors[ImGuiCol_SeparatorActive]       = accent;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_TabHovered]            = bg_hov;
    colors[ImGuiCol_TabActive]             = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_PlotLines]             = accent;
    colors[ImGuiCol_PlotLinesHovered]      = accent;
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = accent;
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.40f, 0.80f, 0.5f);
    colors[ImGuiCol_DragDropTarget]        = accent;
    colors[ImGuiCol_NavHighlight]          = accent;
}

// ==========================================================================
// UI
// ==========================================================================
static void DrawMenu(Overlay& overlay)
{
    if (!g_menuOpen) return;

    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.95f);

    if (ImGui::Begin("Vape V4", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.17f, 0.47f, 0.88f, 1.0f), "Vape V4");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (g_mem.IsValid())
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Connected  PID: %lu", g_mem.GetPID());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Waiting for Roblox...");
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        ImGui::TextDisabled("FPS: %.0f", ImGui::GetIO().Framerate);
        ImGui::Separator();

        float sidebarWidth = 180.0f;
        
        // Sidebar lateral
        ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, 0), true);
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            
            // Apenas uma aba: Misc (sempre selecionada)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.47f, 0.88f, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.17f, 0.47f, 0.88f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
            ImGui::Button("Misc", ImVec2(-1, 35));
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Conteúdo
        ImGui::BeginChild("Content", ImVec2(0, 0), true);
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            
            ImGui::Text("Miscellaneous");
            ImGui::Separator();
            
            if (ImGui::Checkbox("Streamproof", &g_streamproof))
                overlay.SetStreamproof(g_streamproof);
            
            ImGui::Spacing();
            ImGui::Text("Menu Toggle Key:");
            static bool s_waitingKey = false;
            ImGui::SameLine();
            if (s_waitingKey)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.0f, 1.f));
                ImGui::Button("Press any key... (Esc to cancel)", ImVec2(-1, 0));
                ImGui::PopStyleColor();
                for (int vk = 1; vk < 256; ++vk)
                {
                    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
                    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
                    if (GetAsyncKeyState(vk) & 0x8000)
                    {
                        if (vk == VK_ESCAPE) s_waitingKey = false;
                        else { g_menuKey = vk; s_waitingKey = false; }
                        break;
                    }
                }
            }
            else
            {
                char btnLabel[64];
                std::snprintf(btnLabel, sizeof(btnLabel), "[ %s ]", VKToName(g_menuKey));
                if (ImGui::Button(btnLabel, ImVec2(-1, 0)))
                    s_waitingKey = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Configuration");
            
            static char s_newSlotName[64] = "";
            static std::vector<std::string> s_slots = ListConfigSlots();
            static int s_selectedSlot = -1;
            static char s_configMsg[128] = {};
            static float s_configMsgTimer = 0.f;
            
            // Lista de configs salvas
            ImGui::TextDisabled("Saved configs:");
            float listH = ImGui::GetTextLineHeightWithSpacing() * 4.5f;
            if (ImGui::BeginListBox("##slots", ImVec2(-1, listH)))
            {
                if (s_slots.empty())
                    ImGui::TextDisabled("  (no saved configs)");
                for (int i = 0; i < (int)s_slots.size(); ++i)
                {
                    bool sel = (s_selectedSlot == i);
                    if (ImGui::Selectable(s_slots[i].c_str(), sel))
                    {
                        s_selectedSlot = i;
                        strncpy(s_newSlotName, s_slots[i].c_str(), sizeof(s_newSlotName)-1);
                    }
                }
                ImGui::EndListBox();
            }

            // Campo de nome
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##slotname", "Config name...", s_newSlotName, sizeof(s_newSlotName));
            
            // Botões
            float bw = (ImGui::GetContentRegionAvail().x - 8.f) / 3.f;
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.7f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.55f, 0.9f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.65f, 1.0f, 1.f));
            if (ImGui::Button("Save", ImVec2(bw, 0)) && s_newSlotName[0])
            {
                SaveConfigSlot(s_newSlotName);
                s_slots = ListConfigSlots();
                std::snprintf(s_configMsg, sizeof(s_configMsg), "Saved: %s", s_newSlotName);
                s_configMsgTimer = 2.f;
            }
            ImGui::PopStyleColor(3);
            
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.2f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.7f, 0.3f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.9f, 0.4f, 1.f));
            if (ImGui::Button("Load", ImVec2(bw, 0)) && s_newSlotName[0])
            {
                if (LoadConfigSlot(s_newSlotName, &overlay))
                {
                    std::snprintf(s_configMsg, sizeof(s_configMsg), "Loaded: %s", s_newSlotName);
                    s_configMsgTimer = 2.f;
                }
                else
                {
                    std::snprintf(s_configMsg, sizeof(s_configMsg), "Not found: %s", s_newSlotName);
                    s_configMsgTimer = 2.f;
                }
            }
            ImGui::PopStyleColor(3);
            
            ImGui::SameLine();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.f));
            if (ImGui::Button("Delete", ImVec2(-1, 0)) && s_selectedSlot >= 0 && s_selectedSlot < (int)s_slots.size())
            {
                std::string path = GetConfigDir() + "configs\\" + s_slots[s_selectedSlot] + ".ini";
                DeleteFileA(path.c_str());
                std::snprintf(s_configMsg, sizeof(s_configMsg), "Deleted: %s", s_slots[s_selectedSlot].c_str());
                s_configMsgTimer = 2.f;
                s_selectedSlot = -1;
                s_slots = ListConfigSlots();
            }
            ImGui::PopStyleColor(3);

            // Mensagem de feedback
            if (s_configMsgTimer > 0.f)
            {
                s_configMsgTimer -= ImGui::GetIO().DeltaTime;
                ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%s", s_configMsg);
            }

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Unload / Exit", ImVec2(-1, 30)))
                PostQuitMessage(0);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// --------------------------------------------------------------------------
static bool TryAttach()
{
    if (g_mem.IsValid()) return true;
    return g_mem.Attach(TARGET_PROCESS);
}

// --------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    Overlay  overlay;
    Renderer renderer;

    if (!overlay.Init(TARGET_WINDOW))
    {
        MessageBoxW(nullptr, L"Falha ao criar janela overlay.", L"Erro", MB_ICONERROR);
        return 1;
    }

    if (!renderer.Init(overlay.GetHWND(), overlay.GetWidth(), overlay.GetHeight()))
    {
        MessageBoxW(nullptr, L"Falha ao inicializar DirectX 11.", L"Erro", MB_ICONERROR);
        return 1;
    }

    ApplyVapeStyle();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    TryAttach();

    {
        char  autoSlot[64] = {};
        bool  autoLoad     = false;
        LoadMeta(autoSlot, sizeof(autoSlot), autoLoad);
        if (autoLoad && autoSlot[0])
            LoadConfigSlot(autoSlot, &overlay);
    }

    while (overlay.IsRunning())
    {
        if (!overlay.ProcessMessages()) break;

        overlay.SyncWithTarget();
        renderer.Resize(overlay.GetWidth(), overlay.GetHeight());

        TryAttach();

        bool robloxActive = overlay.GetTargetHWND() != nullptr && IsWindow(overlay.GetTargetHWND());
        overlay.SetVisible(robloxActive);

        if (!robloxActive) { Sleep(50); continue; }

        // Poll hotkey do menu
        static int  prevMenuKey  = -1;
        static bool prevMenuDown = false;
        bool menuDown = (GetAsyncKeyState(g_menuKey) & 0x8000) != 0;
        if (menuDown && !prevMenuDown) g_menuOpen = !g_menuOpen;
        if (g_menuKey != prevMenuKey) { prevMenuDown = false; prevMenuKey = g_menuKey; }
        else                           prevMenuDown = menuDown;

        overlay.SetClickThrough(!g_menuOpen);
        overlay.SetFocused(g_menuOpen);

        renderer.BeginFrame();
        DrawMenu(overlay);
        renderer.EndFrame();
    }

    renderer.Shutdown();
    g_mem.Detach();
    return 0;
}
