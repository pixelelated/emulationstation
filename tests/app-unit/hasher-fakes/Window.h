#pragma once
// Double for es-core/src/Window.h, for hasher-tests: what the hasher shows
// is recorded, and nothing is drawn.
#include <string>
class AsyncNotificationComponent;
class GuiComponent;
class Window
{
public:
	AsyncNotificationComponent* createAsyncNotificationComponent(bool actionLine = false);
	void displayNotificationMessage(const std::string& message, int duration = -1);
	void pushGui(GuiComponent* gui);
};
