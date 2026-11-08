#pragma once
#include <d3d11.h>
#include "gfx/image.h"

namespace app {

struct Assets {
    gfx::Image cs2Icon;
    gfx::Image cs2Hero;
    gfx::Image logo;

    void load(ID3D11Device* dev);
    void unload();
};

}
