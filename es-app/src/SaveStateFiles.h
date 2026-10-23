#pragma once
#ifndef ES_APP_SAVE_STATE_FILES_H
#define ES_APP_SAVE_STATE_FILES_H

#include <functional>
#include <string>

// A save state's copy, move and removal, reporting what the file
// operations did (pixelelated/distribution#649, PL-016). SaveState's
// copyToSlot ran them and returned true whatever they returned, so a copy
// into a directory that refused it was reported to the manager as made.
// The operations are handed in -- Utils::FileSystem's in SaveState.cpp --
// so es-unit-tests holds the rule without a filesystem
// (SaveStateFilesTests.cpp).
namespace SaveStateFiles
{
	struct Ops
	{
		std::function<bool(const std::string&, const std::string&)> move, copy;
		std::function<bool(const std::string&)> remove;
	};

	// `state` is the outcome for the state file, which is what the caller
	// acts on; `image` for its screenshot, true when there was none.
	struct Result
	{
		bool state = false;
		bool image = true;
	};

	// The state, then its image. A state that did not move or copy leaves
	// the image where it was.
	inline Result transfer(const Ops& ops, bool move, const std::string& state, const std::string& image, const std::string& destState, const std::string& destImage)
	{
		const auto& op = move ? ops.move : ops.copy;

		Result r;
		r.state = op(state, destState);
		if (r.state && !image.empty())
			r.image = op(image, destImage);
		return r;
	}

	// Both files are removed whatever the first did, and each says so.
	inline Result removeFiles(const Ops& ops, const std::string& state, const std::string& image)
	{
		Result r;
		r.state = state.empty() || ops.remove(state);
		if (!image.empty())
			r.image = ops.remove(image);
		return r;
	}
}

#endif // ES_APP_SAVE_STATE_FILES_H
