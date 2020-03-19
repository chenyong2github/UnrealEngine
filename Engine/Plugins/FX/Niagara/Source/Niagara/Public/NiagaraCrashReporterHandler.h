// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSingleton.h"
#include "HAL/CriticalSection.h"

class FNiagaraSystemInstance;
class FNiagaraSystemSimulation;

/** Manages Niagara's integration with the Crash Reporter. Allows Niagara code to add information to a crash report which can help track down rare bugs. */
class FNiagaraCrashReporterHandler : public TThreadSingleton<FNiagaraCrashReporterHandler>
{
	//Each thread we execute on can add it's own named data to the CR.
	FString Name;

	/** String to clear to when we're not inside the scope of a system instance. */
	FString NoneInfoString;

	/** Stack of scope infos, allows multi level scope tracking. */
	TArray<FString> ScopeInfoStack;

	//Allows us to clear out all data if we disable reporting.
	static TArray<FNiagaraCrashReporterHandler*> AllHandlers;

	/** Critical section ensuring thread-safe access to shared Crash Reporter state. */
	static FCriticalSection CritSec;

	void SetInfo(const FString& Info);

	/** Push an arbitrary string onto info stack and set as Niagara's current CR info for this thread. */
	void PushInfo_Internal(const FString& Info);
public:

	FNiagaraCrashReporterHandler();
	~FNiagaraCrashReporterHandler();

	/** Clears out Niagara's CR entry for this thread. */
	void Clear();

	/** Push info on a system instance to the info stack and set as Niagara's current CR info for this thread. */
	void PushInfo(FNiagaraSystemInstance* Inst);
	/** Push info on a system simulation to the info stack and set as Niagara's current CR info for this thread. */
	void PushInfo(FNiagaraSystemSimulation* SystemSim);
	/** Pop the current info from the stack and set the previous info as Niagara's current CR info for this thread. */
	void PopInfo();

	/** Handle a change in the enabled CVar for Niagara's CR info. */
	static void OnEnabledChanged(IConsoleVariable* InVariable);
};

/** Helper object allowing easy tracking of Niagara code in it's crash reporter integration.  */
class FNiagaraCrashReporterScope
{
private:
	bool bWasEnabled = false;
public:
	FNiagaraCrashReporterScope(FNiagaraSystemInstance* Inst);
	FNiagaraCrashReporterScope(FNiagaraSystemSimulation* Sim);
	~FNiagaraCrashReporterScope();
};