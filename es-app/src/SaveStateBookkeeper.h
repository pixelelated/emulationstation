#pragma once
//
// SaveStateBookkeeper - the save state manager's bookkeeping, off the
// interface thread (fork #205 / #206, D-UI-073).
//
// DELETE used to run `cloud_capture --retire` and `--rescan` through popen
// on the interface thread and redraw the grid only when both had exited:
// two bash starts, a dozen jq starts and a fork of a 350 MB process each,
// about a third of a second on the x86_64 guest and a second on a Cortex-A53,
// felt as the dialog hanging after YES. Now the manager hides the tile the
// same frame and hands the work to one worker thread, one job at a time.
//
// A deletion is one unit inside the script: `cloud_capture --retire --unlink`
// writes the retired row and then unlinks the files itself, and the script
// ignores SIGTERM while it runs, so a `systemctl stop` that ends
// EmulationStation mid-deletion leaves either both or neither (D-CLOUD-133;
// build 9 could leave the row and the file). Should the script be absent or
// fail before the unlink, the worker unlinks what is left: the player asked
// for the deletion and it proceeds whatever the record said. The rescan is
// gone (D-CLOUD-132): it re-keyed slots the renumber had moved, and nothing
// renumbers since D-UI-069.
//
// COPY TO FREE SLOT runs on the worker too (#1242): it waits for and takes
// the cloud transfer lock as a deletion does, copies the state and its
// thumbnail, and checks the copy against the source's size -- the file, not
// a copy call's answer -- before it is recorded through `cloud_capture
// --adopt` (#206) as the source's version at the new path. A copy that fails
// leaves nothing at the new slot. The page reads the outcome by its ticket
// (copyOutcome) and says how it went; until #1242 the copy was made at the
// press, on the interface thread, and nothing said when it failed.
//
// The rules (D-UI-073), because each guards a way this can crash or lie:
//   - the worker holds path strings per job and never a pointer to a page
//     or to the repository's SaveState, both of which die under it; a page
//     learns a job is done by polling completed() in update();
//   - one queue, one job at a time (SaveStateJobQueue), so two quick jobs
//     never race the manifest;
//   - shutdown() joins the worker at exit; a joinable std::thread destroyed
//     at static teardown would terminate the process instead.

#include <string>

class SaveStateBookkeeper
{
public:
	// Record and unlink on the worker; returns at once. The manager hides
	// the tile the same frame (isPending) and, when completed() moves, rebuilds
	// only if the disk disagrees with the page (#207).
	static void deleteLater(const std::string& stateFile, const std::string& screenshot);

	// Copy `source` to the free slot `stateFile`, and `sourceShot` to
	// `screenshot` when there is one, on the worker under the transfer lock;
	// then record it. Returns at once with the ticket copyOutcome reads, or 0
	// when nothing was queued.
	static unsigned copyLater(const std::string& source, const std::string& sourceShot,
		const std::string& stateFile, const std::string& screenshot,
		const std::string& system, const std::string& rom);

	// A copy's outcome by its ticket: -1 while it is queued or running, 1 when
	// the copy is made and the same size as its source, 0 when it is not (and
	// nothing is left at the new slot). A final answer is given once.
	static int copyOutcome(unsigned ticket);

	// Queued or running for deletion: the tile is hidden, the file may
	// still be on disk.
	static bool isPending(const std::string& stateFile);

	// Finished jobs since the process started.
	static unsigned completed();

	// A deletion or a copy is running with the cloud transfer lock held:
	// the lock the manager's DELETE and COPY refusal asks about is this
	// process's own for that moment, not a transfer's.
	static bool holdsTransferLock();

	// Drain the queue and join the worker. Called at exit; harmless when
	// nothing ever ran, and a second call does nothing.
	static void shutdown();
};
