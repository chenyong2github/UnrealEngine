// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGContext.h"
#include "PCGData.h"

class IPCGElement;
class UPCGComponent;
class UPCGSettings;
class UPCGNode;

typedef TSharedPtr<IPCGElement, ESPMode::ThreadSafe> FPCGElementPtr;

#define PCGE_LOG_C(Verbosity, CustomContext, Format, ...) \
	UE_LOG(LogPCG, \
		Verbosity, \
		TEXT("[%s - %s]: " Format), \
		*((CustomContext)->GetComponentName()), \
		*((CustomContext)->GetTaskName()), \
		##__VA_ARGS__)

#if WITH_EDITOR
#define PCGE_LOG(Verbosity, Format, ...) do{ if(ShouldLog()) { PCGE_LOG_C(Verbosity, Context, Format, ##__VA_ARGS__); } }while(0)
#else
#define PCGE_LOG(Verbosity, Format, ...) PCGE_LOG_C(Verbosity, Context, Format, ##__VA_ARGS__)
#endif

/**
* Base class for the processing bit of a PCG node/settings
*/
class PCG_API IPCGElement
{
public:
	virtual ~IPCGElement() = default;
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent, const UPCGNode* Node) = 0;

	virtual bool CanExecuteOnlyOnMainThread(const UPCGSettings* InSettings) const { return false; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const { return true; }

	bool Execute(FPCGContext* Context) const;

	/** Note: the following methods must be called from the main thread */
#if WITH_EDITOR
	void DebugDisplay(FPCGContext* Context) const;
	const TArray<double>& GetTimers() const { return Timers; }
	void ResetTimers();
#endif

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const = 0;
	virtual bool IsCancellable() const { return true; }
	virtual bool IsPassthrough() const { return false; }
#if WITH_EDITOR
	virtual bool ShouldLog() const { return true; }
#endif

private:
	void CleanupAndValidateOutput(FPCGContext* Context) const;

#if WITH_EDITOR
	// Set mutable because we need to modify them in the execute call, which is const
	// TODO: Should be a map with PCG Components. We need a mechanism to make sure that this map is cleaned up when component doesn't exist anymore.
	// For now, it will track all calls to execute (excluding call where the result is already in cache).
	mutable TArray<double> Timers;
	mutable int CurrentTimerIndex = 0;
	// Perhaps overkill but there is a slight chance that we need to protect the timers array. If we call reset from the UI while an element is executing,
	// it could crash while writing to the timers array.
	mutable FCriticalSection TimersLock;
#endif // WITH_EDITOR
};

/**
* Basic PCG element class for elements that do not store any intermediate data in the context
*/
class PCG_API FSimplePCGElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent, const UPCGNode* Node) override;
};