#include "app/assets.h"
#include "app/embedded_assets.h"
#include "resources.h"

#include <windows.h>

namespace app {

static bool resource_bytes(const int id, const void*& data, DWORD& size) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return false;

    const HGLOBAL loaded = LoadResource(nullptr, resource);
    data = loaded ? LockResource(loaded) : nullptr;
    size = SizeofResource(nullptr, resource);
    return data && size > 0;
}

void Assets::load(ID3D11Device* dev) {
    gfx::loadImageMemory(dev, kCs2IconPng, kCs2IconPng_size, cs2Icon);
    gfx::loadImageMemory(dev, kCs2HeroJpg, kCs2HeroJpg_size, cs2Hero);

    const void* logoData = nullptr;
    DWORD logoSize = 0;
    if (!resource_bytes(IDR_PASTA_LOGO, logoData, logoSize) ||
        !gfx::loadImageMemory(dev, logoData, logoSize, logo)) {
        // Keep the window usable if the embedded product logo is ever missing from a build.
        gfx::loadImageMemory(dev, kCsLogoPng, kCsLogoPng_size, logo);
    }
}

void Assets::unload() {
    gfx::releaseImage(cs2Icon);
    gfx::releaseImage(cs2Hero);
    gfx::releaseImage(logo);
}

}
