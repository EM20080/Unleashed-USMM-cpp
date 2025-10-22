#include <windows.h>
#include <d3d11.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"rpcrt4.lib")
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <rpc.h>
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static ID3D11Device* gDev = nullptr; static ID3D11DeviceContext* gCtx = nullptr;
static IDXGISwapChain* gSwap = nullptr; static ID3D11RenderTargetView* gRTV = nullptr;

static bool CreateDeviceD3D(HWND w) {
    DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = w; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    D3D_FEATURE_LEVEL fl, arr[2] = { D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0 };
    return S_OK == D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, arr, 2, D3D11_SDK_VERSION, &sd, &gSwap, &gDev, &fl, &gCtx);
}

static void CreateRT() { ID3D11Texture2D* bb; gSwap->GetBuffer(0, IID_PPV_ARGS(&bb)); if (bb) { gDev->CreateRenderTargetView(bb, nullptr, &gRTV); bb->Release(); } }

static void CleanupRT() { if (gRTV) { gRTV->Release(); gRTV = nullptr; } }

static void CleanupD3D() { CleanupRT(); if (gSwap) gSwap->Release(); if (gCtx) gCtx->Release(); if (gDev) gDev->Release(); }

static void Resize(UINT w, UINT h) { if (gSwap) { CleanupRT(); gSwap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0); CreateRT(); } }

static std::string trim(std::string s) { auto ns = [](int c) {return !isspace(c); }; s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns)); s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end()); return s; }

static std::string unquote(std::string s) { return (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) ? s.substr(1, s.size() - 2) : s; }

struct ConfigElement { std::string name, display_name, type, default_value, current_value; std::vector<std::string> description;
    struct EnumOption { std::string display_name, value; std::vector<std::string> description; }; std::vector<EnumOption> enum_options; };
struct ConfigGroup { std::string name, display_name; std::vector<ConfigElement> elements; };
struct Mod { std::string id, path, title, config_schema_file; std::unordered_map<std::string, std::string> options;
    std::vector<ConfigGroup> config_groups; bool has_options = false, has_config_schema = false; };
static const char* CHEATS[] = { "UseOfficialTitleOnTitleBar","UseArrowsForTimeOfDayTransition","FixUnleashOutOfControlDrain","AllowCancellingUnleash",
    "SkipIntroLogos","SaveScoreAtCheckpoints","DisableBoostFilter","DisableAutoSaveWarning","HUDToggleKey","DisableDWMRoundedCorners","DisableDLCIcon",
    "EnableObjectCollisionDebugView","EnableEventCollisionDebugView","EnableStageCollisionDebugView","EnableGIMipLevelDebugView",
    "FixEggmanlandUsingEventGalleryTransition","DisableDPadMovement","HomingAttackOnJump" };


struct App {
    std::string ini_path; bool loaded = false; std::vector<Mod> mods; std::unordered_set<std::string> active, cheats;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> read_ini(const std::string& p) {
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m; std::ifstream in(p); if (!in) return m; std::string line, sec;
        while (std::getline(in, line)) { line = trim(line); if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') { sec = trim(line.substr(1, line.size() - 2)); continue; }
            auto q = line.find('='); if (q != std::string::npos) m[sec][trim(line.substr(0, q))] = trim(line.substr(q + 1)); } return m; }

    std::string get_title(const std::string& p) { auto m = read_ini(p);
        for (auto& s : { "Details", "details", "Desc", "desc" }) if (m.count(s)) for (auto& k : { "Title", "title" }) if (m[s].count(k)) return unquote(m[s][k]);
        return "<NoTitle>"; }

    std::string get_dir(const std::string& p) { size_t pos = p.find_last_of("\\/"); return (pos != std::string::npos) ? p.substr(0, pos) : "."; }

    std::string json_str(const std::string& j, const std::string& k) { auto p = j.find("\"" + k + "\""); if (p == std::string::npos) return {};
        p = j.find(":", p); if (p == std::string::npos) return {}; p = j.find("\"", p); if (p == std::string::npos) return {};
        auto e = j.find("\"", p + 1); if (e == std::string::npos) return {}; return j.substr(p + 1, e - p - 1); }

    std::vector<std::string> json_arr(const std::string& j, const std::string& k) { std::vector<std::string> r; auto p = j.find("\"" + k + "\"");
        if (p == std::string::npos) return r; p = j.find("[", p); if (p == std::string::npos) return r; auto e = j.find("]", p); if (e == std::string::npos) return r;
        std::string a = j.substr(p + 1, e - p - 1); size_t i = 0; while (i < a.size()) { i = a.find("\"", i); if (i == std::string::npos) break;
            auto ee = a.find("\"", i + 1); if (ee == std::string::npos) break; r.push_back(a.substr(i + 1, ee - i - 1)); i = ee + 1; } return r; }

    void load_config_schema(Mod& m) { if (m.config_schema_file.empty()) return; std::ifstream in(get_dir(m.path) + "\\" + m.config_schema_file); if (!in) return;
        std::string j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); m.config_groups.clear(); auto mi = read_ini(m.path);
        size_t gs = j.find("\"Groups\""); if (gs == std::string::npos) return; gs = j.find("[", gs); if (gs == std::string::npos) return;
        int d = 0; size_t i = gs; for (; i < j.size(); ++i) { if (j[i] == '[') d++; else if (j[i] == ']') { d--; if (d == 0) break; } }
        std::string gsec = j.substr(gs + 1, i - gs - 1); size_t p = 0;
        while (p < gsec.size()) { p = gsec.find("{", p); if (p == std::string::npos) break;
            int bd = 1; size_t e = p + 1; for (; e < gsec.size() && bd > 0; ++e) { if (gsec[e] == '{') bd++; else if (gsec[e] == '}') bd--; }
            std::string go = gsec.substr(p, e - p); ConfigGroup g; g.name = json_str(go, "Name"); g.display_name = json_str(go, "DisplayName");
            if (g.display_name.empty()) g.display_name = g.name; size_t es = go.find("\"Elements\"");
            if (es != std::string::npos) { es = go.find("[", es); if (es != std::string::npos) {
                    int ed = 0; size_t ee = es; for (; ee < go.size(); ++ee) { if (go[ee] == '[') ed++; else if (go[ee] == ']') { ed--; if (ed == 0) break; } }
                    std::string esec = go.substr(es + 1, ee - es - 1); size_t ep = 0;
                    while (ep < esec.size()) { ep = esec.find("{", ep); if (ep == std::string::npos) break;
                        int eb = 1; size_t eee = ep + 1; for (; eee < esec.size() && eb > 0; ++eee) { if (esec[eee] == '{') eb++; else if (esec[eee] == '}') eb--; }
                        std::string eo = esec.substr(ep, eee - ep); ConfigElement el; el.name = json_str(eo, "Name"); el.display_name = json_str(eo, "DisplayName");
                        if (el.display_name.empty()) el.display_name = el.name; el.type = json_str(eo, "Type"); el.default_value = json_str(eo, "DefaultValue");
                        el.current_value = el.default_value;
                        if (mi.count("Main") && mi["Main"].count(el.name)) el.current_value = unquote(mi["Main"][el.name]);
                        else if (mi.count("main") && mi["main"].count(el.name)) el.current_value = unquote(mi["main"][el.name]);
                        el.description = json_arr(eo, "Description"); g.elements.push_back(el); ep = eee; } } }
            if (!g.elements.empty()) m.config_groups.push_back(g); p = e; }
        size_t ens = j.find("\"Enums\""); if (ens == std::string::npos) ens = j.find("\"enums\""); // loads case sensitive "enums" just incase (i.e Unleashed visual overhaul mod.)
        if (ens != std::string::npos) { ens = j.find("{", ens); if (ens != std::string::npos) {
                int ed = 1; size_t ene = ens + 1; for (; ene < j.size() && ed > 0; ++ene) { if (j[ene] == '{') ed++; else if (j[ene] == '}') ed--; }
                std::string esec = j.substr(ens, ene - ens);
                for (auto& g : m.config_groups) { for (auto& el : g.elements) { size_t tp = esec.find("\"" + el.type + "\""); if (tp == std::string::npos) continue;
                        size_t as = esec.find("[", tp); if (as == std::string::npos) continue;
                        int ad = 0; size_t ae = as; for (; ae < esec.size(); ++ae) { if (esec[ae] == '[') ad++; else if (esec[ae] == ']') { ad--; if (ad == 0) break; } }
                        std::string ea = esec.substr(as + 1, ae - as - 1); size_t op = 0;
                        while (op < ea.size()) { op = ea.find("{", op); if (op == std::string::npos) break;
                            int ob = 1; size_t oe = op + 1; for (; oe < ea.size() && ob > 0; ++oe) { if (ea[oe] == '{') ob++; else if (ea[oe] == '}') ob--; }
                            std::string oo = ea.substr(op, oe - op); ConfigElement::EnumOption opt; opt.display_name = json_str(oo, "DisplayName");
                            opt.value = json_str(oo, "Value"); opt.description = json_arr(oo, "Description"); el.enum_options.push_back(opt); op = oe; } } } } } }
    std::string create_id() { UUID u; std::string g; if (UuidCreate(&u) == RPC_S_OK) { RPC_CSTR s; if (UuidToStringA(&u, &s) == RPC_S_OK) {
                g = (char*)s; RpcStringFreeA(&s); std::transform(g.begin(), g.end(), g.begin(), ::tolower); } }
        return g.empty() ? "mod_" + std::to_string(mods.size()) : g; }

    void find_modinis() { if (ini_path.empty()) return; std::unordered_set<std::string> ep; for (auto& m : mods) ep.insert(m.path);
        try { for (auto& e : std::filesystem::recursive_directory_iterator(get_dir(ini_path))) { if (e.is_regular_file()) {
                    std::string f = e.path().filename().string(); std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                    if (f == "mod.ini") { std::string mp = e.path().string(); if (ep.count(mp)) continue;
                        Mod m; m.path = mp; m.id = create_id(); m.title = get_title(mp); auto s = read_ini(m.path);
                        if (s.count("Options") || s.count("options")) { m.has_options = true; m.options = s.count("Options") ? s["Options"] : s["options"]; }
                        for (auto& sc : s) { if (sc.second.count("ConfigSchemaFile")) { m.config_schema_file = unquote(sc.second["ConfigSchemaFile"]);
                                m.has_config_schema = true; load_config_schema(m); break; } }
                        mods.push_back(std::move(m)); ep.insert(mp); } } } } catch (...) {} }

    bool load(const std::string& p) { auto ini = read_ini(p); if (ini.empty()) return false; active.clear(); cheats.clear(); mods.clear();
        int ac = std::atoi(ini["Main"]["ActiveModCount"].c_str()); if (!ac) ac = std::atoi(ini["main"]["activemodcount"].c_str());
        for (int i = 0; i < ac; i++) { auto v = ini["Main"]["ActiveMod" + std::to_string(i)]; if (v.empty()) v = ini["main"]["activemod" + std::to_string(i)]; if (!v.empty()) active.insert(unquote(v)); }
        int cc = std::atoi(ini["Codes"]["CodeCount"].c_str()); if (!cc) cc = std::atoi(ini["codes"]["codecount"].c_str());
        for (int i = 0; i < cc; i++) { auto v = ini["Codes"]["Code" + std::to_string(i)]; if (v.empty()) v = ini["codes"]["code" + std::to_string(i)]; if (!v.empty()) cheats.insert(unquote(v)); }
        for (auto& sec : { "Mods", "mods" }) if (ini.count(sec)) for (auto& kv : ini[sec]) { Mod m; m.id = kv.first; m.path = unquote(kv.second); m.title = get_title(m.path);
            auto s = read_ini(m.path); if (s.count("Options") || s.count("options")) { m.has_options = true; m.options = s.count("Options") ? s["Options"] : s["options"]; }
            for (auto& sc : s) { if (sc.second.count("ConfigSchemaFile")) { m.config_schema_file = unquote(sc.second["ConfigSchemaFile"]);
                    m.has_config_schema = true; load_config_schema(m); break; } } mods.push_back(std::move(m)); }
        ini_path = p; loaded = true; find_modinis(); return true; }

    bool save() { if (!loaded || ini_path.empty()) return false; auto ini = read_ini(ini_path);
        for (auto it = ini["Main"].begin(); it != ini["Main"].end(); ) if (it->first.rfind("ActiveMod", 0) == 0 && it->first != "ActiveModCount") it = ini["Main"].erase(it); else ++it;
        ini["Main"]["ActiveModCount"] = std::to_string((int)active.size());
        int i = 0; for (auto& m : mods) if (active.count(m.id)) ini["Main"]["ActiveMod" + std::to_string(i++)] = "\"" + m.id + "\"";
        for (auto& m : mods) ini["Mods"][m.id] = "\"" + m.path + "\"";
        for (auto it = ini["Codes"].begin(); it != ini["Codes"].end(); ) if (it->first.rfind("Code", 0) == 0 && it->first != "CodeCount") it = ini["Codes"].erase(it); else ++it;
        ini["Codes"]["CodeCount"] = std::to_string((int)cheats.size());
        int ci = 0; for (auto& c : cheats) ini["Codes"]["Code" + std::to_string(ci++)] = "\"" + c + "\""; std::ofstream out(ini_path, std::ios::binary); if (!out) return false;
        for (auto& s : { "Main", "Mods", "Codes" }) if (ini.count(s)) { out << "[" << s << "]\n"; for (auto& kv : ini[s]) out << kv.first << "=" << kv.second << "\n"; out << "\n"; }
        return true; }

    void save_mod_config(Mod& m) { if (!m.has_config_schema) return; auto a = read_ini(m.path);
        for (auto& g : m.config_groups) for (auto& el : g.elements) a["Main"][el.name] = el.current_value; std::ofstream out(m.path, std::ios::binary);
        if (out) { for (auto& s : a) { out << "[" << s.first << "]\n"; for (auto& kv : s.second) out << kv.first << "=" << kv.second << "\n"; out << "\n"; } } }
} g;


static void UI() { ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8; s.FrameRounding = 6; s.GrabRounding = 6; s.ScrollbarRounding = 6; s.WindowBorderSize = 1; s.FrameBorderSize = 1; s.TabRounding = 6;
    s.ItemSpacing = { 10,8 }; s.FramePadding = { 12,8 }; s.WindowPadding = { 14,12 }; auto& c = s.Colors;
    c[ImGuiCol_Text] = { 0.95f,0.95f,0.95f,1.0f }; c[ImGuiCol_WindowBg] = { 0.05f,0.06f,0.07f,1.0f }; c[ImGuiCol_ChildBg] = { 0.06f,0.07f,0.08f,1.0f };
    c[ImGuiCol_PopupBg] = { 0.06f,0.07f,0.08f,1.0f }; c[ImGuiCol_Border] = { 0.18f,0.18f,0.20f,1.0f }; c[ImGuiCol_FrameBg] = { 0.10f,0.11f,0.13f,1.0f };
    c[ImGuiCol_FrameBgHovered] = { 0.18f,0.15f,0.08f,1.0f }; c[ImGuiCol_FrameBgActive] = { 0.26f,0.21f,0.10f,1.0f }; c[ImGuiCol_Button] = { 0.18f,0.15f,0.08f,1.0f };
    c[ImGuiCol_ButtonHovered] = { 0.26f,0.21f,0.10f,1.0f }; c[ImGuiCol_ButtonActive] = { 0.36f,0.30f,0.14f,1.0f }; c[ImGuiCol_Header] = { 0.22f,0.19f,0.10f,1.0f };
    c[ImGuiCol_HeaderHovered] = { 0.30f,0.25f,0.12f,1.0f }; c[ImGuiCol_HeaderActive] = { 0.40f,0.33f,0.16f,1.0f }; c[ImGuiCol_CheckMark] = { 0.98f,0.80f,0.20f,1.0f };
    c[ImGuiCol_SliderGrab] = { 0.98f,0.80f,0.20f,1.0f }; c[ImGuiCol_SliderGrabActive] = { 1.00f,0.90f,0.35f,1.0f }; c[ImGuiCol_Tab] = { 0.16f,0.14f,0.09f,1.0f };
    c[ImGuiCol_TabHovered] = { 0.30f,0.25f,0.12f,1.0f }; c[ImGuiCol_TabActive] = { 0.24f,0.21f,0.11f,1.0f }; }

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX),CS_CLASSDC,[](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true; if (m == WM_SIZE && w != SIZE_MINIMIZED) { Resize(LOWORD(l), HIWORD(l)); return 0; }
        if (m == WM_DESTROY) { PostQuitMessage(0); return 0; } if (m == WM_SYSCOMMAND && (w & 0xfff0) == SC_KEYMENU) return 0;
        return DefWindowProc(h, m, w, l); },0,0,GetModuleHandle(NULL),NULL,NULL,NULL,NULL,L"UUMM",NULL };
    RegisterClassEx(&wc); HWND wnd = CreateWindow(wc.lpszClassName, L"Unleashed UWP Mod Manager", WS_OVERLAPPEDWINDOW, 100, 100, 1100, 700, NULL, NULL, wc.hInstance, NULL);
    if (!CreateDeviceD3D(wnd)) { CleanupD3D(); UnregisterClass(wc.lpszClassName, wc.hInstance); return 1; }
    CreateRT(); ShowWindow(wnd, SW_SHOWDEFAULT); UpdateWindow(wnd); IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark(); UI();
    ImGuiIO& io = ImGui::GetIO(); io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
    ImGui_ImplWin32_Init(wnd); ImGui_ImplDX11_Init(gDev, gCtx); bool cfg = false; int ci = -1;
    while (true) { MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) goto end; }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always); ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) { if (ImGui::BeginMenu("File")) { if (ImGui::MenuItem("Open ModsDB.ini...")) {
                    wchar_t b[MAX_PATH] = L""; OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = wnd;
                    o.lpstrFilter = L"INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0"; o.lpstrFile = b; o.nMaxFile = MAX_PATH;
                    o.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; o.lpstrTitle = L"Open ModsDB.ini";
                    if (GetOpenFileNameW(&o)) { char u[MAX_PATH * 3]{}; WideCharToMultiByte(CP_UTF8, 0, b, -1, u, sizeof(u), nullptr, nullptr); g.load(u); } }
                if (g.loaded && ImGui::MenuItem("Save")) g.save(); ImGui::EndMenu(); } ImGui::EndMenuBar(); }
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_None)) { if (ImGui::BeginTabItem("Mods")) {
                if (!g.loaded) ImGui::TextDisabled("Open ModsDB.ini to view Mods.");
                else { ImGui::BeginChild("mods", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysUseWindowPadding);
                    for (auto& m : g.mods) { bool on = g.active.count(m.id);
                        if (ImGui::Checkbox(m.title.c_str(), &on)) { if (on) g.active.insert(m.id); else g.active.erase(m.id); }
                        if ((m.has_options && !m.options.empty()) || (m.has_config_schema && !m.config_groups.empty())) {
                            ImGui::SameLine(); if (ImGui::SmallButton(("Config##" + m.id).c_str())) { ci = (int)(&m - &g.mods[0]); cfg = true; } } }
                    ImGui::EndChild(); } ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Codes")) { if (!g.loaded) ImGui::TextDisabled("Open ModsDB.ini to view Codes.");
                else { ImGui::BeginChild("codes", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysUseWindowPadding);
                    for (auto* c : CHEATS) { bool on = g.cheats.count(c);
                        if (ImGui::Checkbox(c, &on)) { if (on) g.cheats.insert(c); else g.cheats.erase(c); } }
                    ImGui::EndChild(); } ImGui::EndTabItem(); } ImGui::EndTabBar(); }
        ImGui::End(); if (cfg && ci >= 0 && ci < (int)g.mods.size()) { ImGui::OpenPopup("Configure Mod"); cfg = false; }
        if (ImGui::BeginPopupModal("Configure Mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto& m = g.mods[ci]; ImGui::Text("Configure %s", m.title.c_str()); ImGui::Separator();
            if (m.has_config_schema) { for (auto& grp : m.config_groups) {
                    if (ImGui::CollapsingHeader(grp.display_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (auto& el : grp.elements) { ImGui::PushID(el.name.c_str()); ImGui::TextUnformatted(el.display_name.c_str());
                            if (!el.description.empty() && !el.description[0].empty()) { ImGui::SameLine(); ImGui::TextDisabled("(?)");
                                if (ImGui::IsItemHovered()) { ImGui::BeginTooltip();
                                    for (auto& d : el.description) ImGui::TextUnformatted(d.c_str()); ImGui::EndTooltip(); } }
                            if (!el.enum_options.empty()) { int cur = 0;
                                for (int i = 0; i < (int)el.enum_options.size(); ++i) if (el.enum_options[i].value == el.current_value) { cur = i; break; }
                                if (ImGui::BeginCombo(("##" + el.name).c_str(), el.enum_options[cur].display_name.c_str())) {
                                    for (int i = 0; i < (int)el.enum_options.size(); ++i) { bool sel = (cur == i);
                                        if (ImGui::Selectable(el.enum_options[i].display_name.c_str(), sel)) el.current_value = el.enum_options[i].value;
                                        if (sel) ImGui::SetItemDefaultFocus(); } ImGui::EndCombo(); } }
                            else { char buf[512]; strncpy_s(buf, el.current_value.c_str(), sizeof(buf) - 1);
                                if (ImGui::InputText(("##" + el.name).c_str(), buf, sizeof(buf))) el.current_value = buf; }
                            ImGui::PopID(); } } } }
            else if (m.has_options) { static std::vector<std::pair<std::string, std::string>> opts; static bool first = true;
                if (first) { opts.clear(); for (auto& kv : m.options) opts.push_back(kv); first = false; }
                for (size_t i = 0; i < opts.size(); ++i) { ImGui::PushID((int)i); ImGui::TextUnformatted(opts[i].first.c_str()); ImGui::SameLine(260);
                    std::string v = opts[i].second, low = v; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if (low == "true" || low == "false") { bool b = (low == "true"); if (ImGui::Checkbox("##b", &b)) opts[i].second = b ? "true" : "false"; }
                    else { char buf[256]{}; strncpy_s(buf, v.c_str(), sizeof(buf) - 1); if (ImGui::InputText("##v", buf, sizeof(buf))) opts[i].second = buf; }
                    ImGui::PopID(); } }
            ImGui::Separator(); if (ImGui::Button("OK", ImVec2(110, 0))) { if (m.has_config_schema) g.save_mod_config(m);
                else if (m.has_options) { static std::vector<std::pair<std::string, std::string>> opts;
                    m.options.clear(); for (auto& p : opts) m.options[p.first] = p.second; auto a = g.read_ini(m.path); a["Options"] = m.options; std::ofstream out(m.path, std::ios::binary);
                    if (out) { for (auto& s : a) { if (s.first == "Options") continue; out << "[" << s.first << "]\n"; for (auto& kv : s.second) out << kv.first << "=" << kv.second << "\n"; out << "\n"; } out << "[Options]\n"; for (auto& kv : a["Options"]) out << kv.first << "=" << kv.second << "\n\n"; } }
                ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        ImGui::Render(); const float clr[4] = { 0.07f,0.08f,0.09f,1.0f };
        gCtx->OMSetRenderTargets(1, &gRTV, nullptr); gCtx->ClearRenderTargetView(gRTV, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); gSwap->Present(1, 0); }
end: ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupD3D(); DestroyWindow(wnd); UnregisterClass(L"UUMM", GetModuleHandle(NULL)); return 0; }
