// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "Math/NumericLimits.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

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

	/** Creates a custom context object paired to this element */
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) = 0;

	/** Returns true if the element, in its current phase can be executed only from the main thread */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const { return false; }

	/** Returns true if the node can be cached - also checks for instance flags, if any. */
	bool IsCacheableInstance(const UPCGSettingsInterface* InSettingsInterface) const;

	/** Returns true if the node can be cached (e.g. does not create artifacts & does not depend on untracked data */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const { return true; }

	/** Public function that executes the element on the appropriately created context.
	* The caller should call the Execute function until it returns true.
	*/
	bool Execute(FPCGContext* Context) const;

	/** Note: the following methods must be called from the main thread */
#if WITH_EDITOR
	struct FCallTime
	{
		// sum of all frames
		double ExecutionTime = 0;
		// how many frames
		int32 ExecutionFrameCount = 0;
		double MinExecutionFrameTime = MAX_dbl;
		double MaxExecutionFrameTime = 0.0;
		double PrepareDataTime = 0.0;
		double PostExecuteTime = 0.0;
	};

	struct FCapturedMessage
	{
		int32 Index=0;
		FName Namespace;
		FString Message;
		ELogVerbosity::Type Verbosity;
	};

	void DebugDisplay(FPCGContext* Context) const;

	// returns a copy because they can be modified on another thread otherwise
	TArray<FCallTime> GetTimers() const;
	TArray<FCapturedMessage> GetCapturedMessages() const;

	void ResetTimers();
	void ResetMessages();
#endif

protected:
	/** This function will be called once and once only, at the beginning of an execution */
	void PreExecute(FPCGContext* Context) const;
	/** The prepare data phase is one where it is more likely to be able to multithread */
	virtual bool PrepareDataInternal(FPCGContext* Context) const;
	/** Core execution method for the given element. Will be called until it returns true. */
	virtual bool ExecuteInternal(FPCGContext* Context) const = 0;
	/** This function will be called once and once only, at the end of an execution */
	void PostExecute(FPCGContext* Context) const;

	/** Controls whether an element can skip its execution wholly when the input data has the cancelled tag */
	virtual bool IsCancellable() const { return true; }
	/** Used to specify that the element passes through the data without any manipulation - used to correct target pins, etc. */
	virtual bool IsPassthrough(const UPCGSettings* InSettings) const { return false; }

	/** Passes through data when the element is Disabled. Can be implemented to override what gets passed through. */
	virtual void DisabledPassThroughData(FPCGContext* Context) const;

#if WITH_EDITOR
	virtual bool ShouldLog() const { return true; }
#endif

private:
	void CleanupAndValidateOutput(FPCGContext* Context) const;

#if WITH_EDITOR

	struct FScopedCall : public FOutputDevice
	{
		FScopedCall(const IPCGElement& InOwner, FPCGContext* InContext);
		virtual ~FScopedCall();

		const IPCGElement& Owner;
		FPCGContext* Context;
		double StartTime;
		EPCGExecutionPhase Phase;
		const uint32 ThreadID;
		TArray<FCapturedMessage> CapturedMessages;

		// FOutputDevice
		virtual bool IsMemoryOnly() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override;
	};

	// Set mutable because we need to modify them in the execute call, which is const
	// TODO: Should be a map with PCG Components. We need a mechanism to make sure that this map is cleaned up when component doesn't exist anymore.
	// For now, it will track all calls to execute (excluding call where the result is already in cache).
	mutable TArray<FCallTime> Timers;
	mutable int32 CurrentTimerIndex = 0;
	mutable TArray<FCapturedMessage> CapturedMessages;

	// Perhaps overkill but there is a slight chance that we need to protect the timers array. If we call reset from the UI while an element is executing,
	// it could crash while writing to the timers array.
	mutable FCriticalSection CapturedDataLock;
#else // !WITH_EDITOR
	struct FScopedCall
	{
		FScopedCall(const IPCGElement& InOwner, FPCGContext* InContext) {}
		void AdvanceTimerIndex() {}
	};
#endif // WITH_EDITOR
};

/**
* Basic PCG element class for elements that do not store any intermediate data in the context
*/
class PCG_API FSimplePCGElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;
};