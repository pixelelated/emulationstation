#pragma once
#ifndef ES_CORE_RECOVERY_OUTCOME_H
#define ES_CORE_RECOVERY_OUTCOME_H

// What a store's load did with its last-known-good record (T21 PL-18,
// #1206): SystemConf and Settings each report one, and the interface tells
// the player once, in the words that are true of it.
enum class RecoveryOutcome
{
	None,               // the live file was read
	Restored,           // damaged; the record was loaded and written back
	Missing,            // missing; the record was loaded and written back
	RestoredInMemory    // the record is in use; writing it back is owed to the next save
};

#endif // ES_CORE_RECOVERY_OUTCOME_H
