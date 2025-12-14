#include "browser/client.h"

#if defined(CEF_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

#include <string>

#include "include/base/cef_logging.h"
#include "include/cef_browser.h"

namespace rethread {

void BrowserClient::PlatformTitleChange(CefRefPtr<CefBrowser> browser,
                                        const CefString& title) {
#if defined(CEF_X11)
  std::string utf8_title(title);

  ::Display* display = cef_get_xdisplay();
  DCHECK(display);

  ::Window window = browser->GetHost()->GetWindowHandle();
  if (window == kNullWindowHandle) {
    return;
  }

  const char* kAtoms[] = {"_NET_WM_NAME", "UTF8_STRING"};
  Atom atoms[2];
  int result = XInternAtoms(display, const_cast<char**>(kAtoms), 2, false, atoms);
  if (!result) {
    NOTREACHED();
  }

  XChangeProperty(display, window, atoms[0], atoms[1], 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(utf8_title.c_str()),
                  utf8_title.size());

  XStoreName(display, window, utf8_title.c_str());
#endif  // defined(CEF_X11)
}

}  // namespace rethread
