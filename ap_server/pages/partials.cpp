#include "template.h"

// Shared SoftAP chrome. Active link: pass gallery_active / settings_active as
// class="menu-active" (or empty). Brand text: {{device_name}}.
static const char kNavbar[] = R"HTML(
  <div class="navbar bg-base-100 shadow-sm">
    <div class="flex-1">
      <a class="btn btn-ghost text-xl" href="/">{{device_name}}</a>
    </div>
    <div class="flex-none">
      <ul class="menu menu-horizontal px-1">
        <li><a {{gallery_active}} href="/gallery">Gallery</a></li>
        <li><a {{settings_active}} href="/settings">Settings</a></li>
      </ul>
    </div>
  </div>
)HTML";

static TmplImportReg regNavbar("navbar", kNavbar);
