#include "app/app.h"
#include "resources.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace ui;
using core::Col;
using core::Rect;
using core::Vec2;

namespace app {

static const float kBarHeight = kTitleBarHeight;

static std::string to_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), needed, nullptr, nullptr);
    return result;
}

// The draw list does not wrap text. Keep the status/detail lines inside the panel instead of letting a long
// launcher message run under the close button or outside the rounded body.
static std::string fit_text(gfx::Font& font, const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.f) return {};
    if (font.measure(text.c_str()) <= maxWidth) return text;

    const std::string ellipsis = "...";
    const float ellipsisWidth = font.measure(ellipsis.c_str());
    std::string result;
    int i = 0;
    while (i < (int)text.size()) {
        gfx::Font::decode(text.c_str(), i);
        if (i <= 0) break;
        const std::string candidate = text.substr(0, (size_t)i);
        if (font.measure(candidate.c_str()) + ellipsisWidth > maxWidth) break;
        result = candidate;
    }
    return result + ellipsis;
}

static void drawProgress(const Rect& r, int progress, float alpha) {
    Theme& t = theme();
    const float radius = r.h * 0.5f;
    const float fraction = std::clamp((float)progress / 100.f, 0.f, 1.f);

    dl().rect(r, Col::hex(0x2B2024, 0.86f * alpha), radius);
    dl().border(r.shrink(0.5f), Col::hex(0xFFFFFF, 0.055f * alpha), 1.f, radius - 0.5f);
    if (fraction <= 0.001f) return;

    Rect fill(r.x, r.y, (std::max)(r.h, r.w * fraction), r.h);
    dl().shadow(fill.shrink(0.5f), t.focus.alpha(0.30f * alpha), 8.f, radius, Vec2());
    dl().gradientHMasked(fill, t.barGlow.alpha(alpha), t.focus.alpha(alpha), fill, radius);
}

static Vec2 between(const Vec2& a, const Vec2& b, float t) {
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

static void drawCheckMark(const Vec2& center, float radius, float progress, float alpha) {
    Theme& t = theme();
    const float p = core::clamp01(progress);
    const float ring = core::easeOutCubic(core::clamp01(p / 0.55f));
    const float check = core::clamp01((p - 0.18f) / 0.57f);
    Rect badge(center.x - radius, center.y - radius, radius * 2.f, radius * 2.f);

    dl().shadow(badge.shrink(2.f), t.focus.alpha(0.22f * alpha), 22.f, radius, Vec2(0.f, 4.f));
    dl().circle(center, radius, Col::hex(0x1D1316, 0.94f * alpha));
    dl().border(badge.shrink(0.5f), Col::hex(0xFFFFFF, 0.07f * alpha), 1.f, radius - 0.5f);
    dl().arc(center, radius, 2.f, -core::kPi * 0.5f, -core::kPi * 0.5f + ring * core::kPi * 2.f,
             t.focus.alpha(alpha));

    const Vec2 a(center.x - radius * 0.38f, center.y + radius * 0.02f);
    const Vec2 b(center.x - radius * 0.10f, center.y + radius * 0.30f);
    const Vec2 c(center.x + radius * 0.40f, center.y - radius * 0.28f);
    const float first = core::clamp01(check / 0.42f);
    const float second = core::clamp01((check - 0.42f) / 0.58f);
    const Vec2 e1 = between(a, b, first);
    const Vec2 e2 = between(b, c, second);
    const Col shadow = Col::hex(0x000000, 0.32f * alpha);
    const Col stroke = Col::hex(0xFFF3D7, alpha);

    if (first > 0.f) dl().line(Vec2(a.x + 0.5f, a.y + 0.9f), Vec2(e1.x + 0.5f, e1.y + 0.9f), 4.1f, shadow);
    if (second > 0.f) dl().line(Vec2(b.x + 0.5f, b.y + 0.9f), Vec2(e2.x + 0.5f, e2.y + 0.9f), 4.1f, shadow);
    if (first > 0.f) dl().line(a, e1, 3.8f, stroke);
    if (second > 0.f) dl().line(b, e2, 3.8f, stroke);
}

static bool font_resource(const int id, const void*& data, size_t& size) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return false;

    const HGLOBAL loaded = LoadResource(nullptr, resource);
    data = loaded ? LockResource(loaded) : nullptr;
    size = SizeofResource(nullptr, resource);
    return data && size > 0;
}

bool App::init(ID3D11Device* dev) {
    const void* semiBold = nullptr;
    size_t semiBoldSize = 0;
    const void* bold = nullptr;
    size_t boldSize = 0;
    font_resource(IDR_INTER_SEMIBOLD, semiBold, semiBoldSize);
    font_resource(IDR_INTER_BOLD, bold, boldSize);

    ui::FontData fd;
    fd.semiBold = semiBold;
    fd.semiBoldSize = semiBoldSize;
    fd.bold = bold;
    fd.boldSize = boldSize;
    if (!ui::init(dev, fd)) return false;

    m_assets.load(dev);
    return true;
}

void App::shutdown() {
    m_assets.unload();
    ui::shutdown();
}

void App::screenList(const Rect& panel, float alpha, Actions& actions) {
    if (alpha <= 0.004f) return;

    Rect row(panel.x + 14.f, panel.y + kBarHeight + 14.f, panel.w - 28.f, 36.f);
    if (gameRow("pasta.load", row, &m_assets.cs2Icon, "Load PASTA")) actions.load = true;
}

void App::screenStatus(const Rect& panel, float alpha, const ViewState& state, Actions& actions) {
    if (alpha <= 0.004f) return;

    Theme& t = theme();
    Rect content(panel.x, panel.y + kBarHeight, panel.w, panel.h - kBarHeight);
    const bool running = state.phase == Phase::running;
    const int progress = std::clamp(state.progress, 0, 100);

    if (m_assets.cs2Hero.valid()) {
        const float imageAspect = m_assets.cs2Hero.aspect();
        const float rectAspect = content.w / content.h;
        Rect uv(0.f, 0.f, 1.f, 1.f);
        if (imageAspect > rectAspect) uv.w = rectAspect / imageAspect;
        else uv.h = imageAspect / rectAspect;
        uv.x = (1.f - uv.w) * 0.70f;
        uv.y = (1.f - uv.h) * 0.44f;

        dl().imageShaped(m_assets.cs2Hero.srv, content, Col(0.70f, 0.27f, 0.24f, 0.24f * alpha), panel,
                         t.radius, uv, 0.82f);
        dl().gradientVMasked(content, Col::hex(0x171216, 0.54f * alpha), Col::hex(0x100B0D, 0.96f * alpha), panel,
                             t.radius);
    } else {
        dl().gradientVMasked(content, Col::hex(0x1A1013, 0.98f * alpha), Col::hex(0x100B0D, 1.f * alpha), panel,
                             t.radius);
    }

    const std::wstring fallback = running ? L"Launching PASTA" : (state.phase == Phase::success ? L"PASTA  is  running" : L"Could not start PASTA");
    const std::string status = fit_text(fonts().body, to_utf8(state.status.empty() ? fallback : state.status),
                                        content.w - 36.f - (running ? 52.f : 0.f));
    const std::string detail = fit_text(fonts().caption, to_utf8(state.detail), content.w - 36.f);

    const float textX = content.x + 18.f;
    const float textW = content.w - 36.f;
    const float statusY = content.y + 9.f;
    ui::text(fonts().body, Rect(textX, statusY, textW - (running ? 52.f : 0.f), 17.f), status.c_str(),
             t.text.alpha(alpha), AlignH::Left, AlignV::Middle);
    if (running) {
        const std::string percent = std::to_string(progress) + "%";
        ui::text(fonts().caption, Rect(textX, statusY, textW, 17.f), percent.c_str(), t.text.alpha(0.72f * alpha),
                 AlignH::Right, AlignV::Middle);
    }
    ui::text(fonts().caption, Rect(textX, content.y + 27.f, textW, 22.f), detail.c_str(),
             t.text.alpha(0.56f * alpha), AlignH::Left, AlignV::Top);

    if (state.phase == Phase::success) {
        drawCheckMark(Vec2(content.center().x, content.y + 102.f), 36.f, m_successT / 0.75f, alpha);
    } else if (m_assets.logo.valid()) {
        const float beat = 0.5f - 0.5f * std::cos(g().time * 2.2f);
        const float aspect = m_assets.logo.aspect();
        const float logoW = 140.f * (1.f + beat * 0.012f);
        const float logoH = logoW / (aspect > 0.f ? aspect : 1.f);
        Rect logoBox(content.center().x - logoW * 0.5f, content.y + 52.f, logoW, logoH);
        logoMark(&m_assets.logo, logoBox, alpha * (0.92f + beat * 0.08f));
    }

    if (running) drawProgress(Rect(textX, content.y + 151.f, textW, 3.f), progress, alpha);

    Rect action(panel.x + 14.f, panel.b() - 10.f - 34.f, panel.w - 28.f, 34.f);
    if (running) {
        if (gameRow("pasta.cancel", action, nullptr, state.cancelling ? "Cancelling..." : "Cancel") &&
            !state.cancelling) {
            actions.cancel = true;
        }
    } else if (state.phase == Phase::failure) {
        if (gameRow("pasta.retry", action, nullptr, "Try again")) actions.load = true;
    }
}

Actions App::frame(float dt, const ViewState& state) {
    Actions actions;

    m_openT = core::clamp01(m_openT + dt / 0.26f);
    m_fadeIn = core::easeOutCubic(m_openT);
    if (m_closing) {
        m_closeT = core::clamp01(m_closeT + dt / 0.19f);
        m_fade = 1.f - m_closeT * m_closeT;
    }

    if (state.phase == Phase::success) {
        m_successT += dt;
        if (m_successT >= 1.35f && !m_closing) requestClose();
    } else {
        m_successT = 0.f;
    }

    const bool active = state.phase != Phase::idle;
    m_statusBlend = anim(id("status.blend"), 0, active ? 1.f : 0.f, 10.f);

    m_targetW = active ? 372.f : 320.f;
    m_targetH = active ? 236.f : 184.f;
    if (!m_sizeInit) {
        m_curW = m_targetW;
        m_curH = m_targetH;
        m_sizeInit = true;
    }
    core::spring(m_curW, m_velW, m_targetW, 260.f, 30.f, dt);
    core::spring(m_curH, m_velH, m_targetH, 260.f, 30.f, dt);

    const float scale = (1.f - m_closeT * 0.05f) * core::lerp(0.955f, 1.f, m_fadeIn);
    const float panelW = std::floor(m_curW * scale);
    const float panelH = std::floor(m_curH * scale);
    m_panel = Rect(std::floor((g().width - panelW) * 0.5f), std::floor((g().height - panelH) * 0.5f), panelW, panelH);

    dl().shadow(m_panel.shrink(3.f), Col(0.f, 0.f, 0.f, 0.44f), 28.f, theme().radius, Vec2(0.f, 9.f));
    dl().rect(m_panel, theme().body, theme().radius);

    dl().pushClip(m_panel);
    screenList(m_panel, 1.f - m_statusBlend, actions);
    screenStatus(m_panel, m_statusBlend, state, actions);
    dl().popClip();

    titleBar(m_panel, kBarHeight, "PASTA", g().time);

    Rect closeBtn(m_panel.r() - 30.f, m_panel.y + kBarHeight * 0.5f - 11.f, 22.f, 22.f);
    if (closeButton("chrome.close", closeBtn) && !m_closing) m_closing = true;

    return actions;
}

}
