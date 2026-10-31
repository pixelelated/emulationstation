#pragma once
#ifndef ES_CORE_COMPONENTS_TEXT_EDIT_COMPONENT_H
#define ES_CORE_COMPONENTS_TEXT_EDIT_COMPONENT_H

#include "components/NinePatchComponent.h"
#include "GuiComponent.h"

class Font;
class TextCache;

// Used to enter text.
class TextEditComponent : public GuiComponent
{
public:
	TextEditComponent(Window* window);
	
	void textInput(const char* text) override;
	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;

	void onFocusGained() override;
	void onFocusLost() override;

	void onSizeChanged() override;

	void setValue(const std::string& val) override;
	std::string getValue() const override;

	inline bool isEditing() const { return mEditing; };
	inline const std::shared_ptr<Font>& getFont() const { return mFont; }

	void setCursor(size_t pos);

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

	void startEditing(); 
	void stopEditing(); 

private:
	bool hasAnyKeyPressed();

	void onTextChanged();
	void onCursorChanged();

	void updateCursorRepeat(int deltaTime);
	void moveCursor(int amt);

	bool isMultiline();
	Vector2f getTextAreaPos() const;
	Vector2f getTextAreaSize() const;

	std::string mText;
	bool mFocused;
	// #1311: the focus handlers' posts hold this weakly and run nothing once
	// the field is gone; made before the constructor's first onFocusLost().
	std::shared_ptr<void> mAlive = std::make_shared<int>(0);
	bool mEditing;
	unsigned int mCursor; // cursor position in characters

	int mCursorRepeatTimer;
	int mCursorRepeatDir;

	Vector2f mScrollOffset;

	NinePatchComponent mBox;

	std::shared_ptr<Font> mFont;
	std::unique_ptr<TextCache> mTextCache;

	int mBlinkTime;
	bool mDeferTextInputStart;
};

#endif // ES_CORE_COMPONENTS_TEXT_EDIT_COMPONENT_H
