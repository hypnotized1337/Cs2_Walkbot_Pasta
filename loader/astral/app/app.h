#pragma once
#include <d3d11.h>
#include <string>
#include "app/assets.h"
#include "core/types.h"

namespace app {

enum class Phase {
    idle,
    running,
    success,
    failure,
};

struct ViewState {
    Phase phase = Phase::idle;
    std::wstring status;
    std::wstring detail;
    int progress = 0;
    bool cancelling = false;
};

struct Actions {
    bool load = false;
    bool cancel = false;
};

constexpr float kTitleBarHeight = 32.f;

class App {
public:
    bool init(ID3D11Device* dev);
    void shutdown();

    Actions frame(float dt, const ViewState& state);

    core::Rect panelRect() const { return m_panel; }
    void requestClose() { m_closing = true; }
    bool closeRequested() const { return m_closing; }
    bool finished() const { return m_closing && m_closeT >= 1.f; }
    float fadeAlpha() const { return m_fadeIn * m_fade; }

private:
    void screenList(const core::Rect& panel, float alpha, Actions& actions);
    void screenStatus(const core::Rect& panel, float alpha, const ViewState& state, Actions& actions);

    Assets m_assets;
    core::Rect m_panel;
    float m_targetW = 0.f;
    float m_targetH = 0.f;
    float m_curW = 0.f;
    float m_curH = 0.f;
    float m_velW = 0.f;
    float m_velH = 0.f;
    bool m_sizeInit = false;
    float m_statusBlend = 0.f;
    float m_successT = 0.f;
    bool m_closing = false;
    float m_closeT = 0.f;
    float m_fade = 1.f;
    float m_fadeIn = 0.f;
    float m_openT = 0.f;
};

}
