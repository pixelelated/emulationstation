#pragma once
// Double for es-core/src/guis/GuiMsgBox.h: its text kept, for the tests.
#include <functional>
#include <string>
class Window;
class GuiComponent { public: virtual ~GuiComponent() {} };
class GuiMsgBox : public GuiComponent
{
public:
	GuiMsgBox(Window* window, const std::string& text, const std::string& name1 = "OK",
		const std::function<void()>& func1 = nullptr, const std::string& name2 = "",
		const std::function<void()>& func2 = nullptr) : text(text) { (void) window; (void) name1; (void) func1; (void) name2; (void) func2; }
	std::string text;
};
