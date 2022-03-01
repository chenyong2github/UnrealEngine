// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Misc/OutputDevice.h"
#include "Templates/PimplPtr.h"
#include "UObject/NameTypes.h"

/*-----------------------------------------------------------------------------
FOutputDeviceRedirector.
-----------------------------------------------------------------------------*/

namespace UE::Private { struct FOutputDeviceRedirectorState; }

/** The type of lines buffered by secondary threads. */
struct CORE_API FBufferedLine
{
	enum EBufferedLineInit
	{
		EMoveCtor = 0
	};

	const TCHAR* Data;
	const FLazyName Category;
	const double Time;
	const ELogVerbosity::Type Verbosity;
	bool bExternalAllocation;

	FBufferedLine(const TCHAR* InData, const FName& InCategory, ELogVerbosity::Type InVerbosity, const double InTime = -1);

	FBufferedLine(FBufferedLine& InBufferedLine, EBufferedLineInit Unused)
		: Data(InBufferedLine.Data)
		, Category(InBufferedLine.Category)
		, Time(InBufferedLine.Time)
		, Verbosity(InBufferedLine.Verbosity)
		, bExternalAllocation(InBufferedLine.bExternalAllocation)
	{
		InBufferedLine.Data = nullptr;
		InBufferedLine.bExternalAllocation = false;
	}

	/** Noncopyable for now, could be made movable */
	FBufferedLine(const FBufferedLine&) = delete;
	FBufferedLine& operator=(const FBufferedLine&) = delete;
	~FBufferedLine();
};

/**
 * Class used for output redirection to allow logs to show in multiple output devices.
 */
class CORE_API FOutputDeviceRedirector final : public FOutputDevice
{
public:
	UE_DEPRECATED(5.1, "TLocalOutputDevicesArray is being removed. Use TArray<FOutputDevice*, TInlineAllocator<16>>.")
	typedef TArray<FOutputDevice*, TInlineAllocator<16>> TLocalOutputDevicesArray;

	/** Initialization constructor. */
	FOutputDeviceRedirector();

	/** Get the GLog singleton. */
	static FOutputDeviceRedirector* Get();

	/**
	 * Adds an output device to the chain of redirections.
	 *
	 * @param OutputDevice   Output device to add.
	 */
	void AddOutputDevice(FOutputDevice* OutputDevice);

	/**
	 * Removes an output device from the chain of redirections.
	 *
	 * @param OutputDevice   Output device to remove.
	 */
	void RemoveOutputDevice(FOutputDevice* OutputDevice);

	/**
	 * Returns whether an output device is in the list of redirections.
	 *
	 * @param OutputDevice   Output device to check the list against.
	 * @return true if messages are currently redirected to the the passed in output device, false otherwise.
	 */
	bool IsRedirectingTo(FOutputDevice* OutputDevice);

	/** Flushes lines buffered by secondary threads. */
	void FlushThreadedLogs();

	/**
	 * Flushes lines buffered by secondary threads.
	 *
	 * Only used if a background thread crashed and we needed to push the callstack into the log.
	 */
	void PanicFlushThreadedLogs();

	/**
	 * Serializes the current backlog to the specified output device.
	 * @param OutputDevice   Output device that will receive the current backlog.
	 */
	void SerializeBacklog(FOutputDevice* OutputDevice);

	/**
	 * Enables or disables the backlog.
	 *
	 * @param bEnable   Starts saving a backlog if true, disables and discards any backlog if false.
	 */
	void EnableBacklog(bool bEnable);

	/** Sets the current thread to be the master thread redirects logs without buffering. */
	void SetCurrentThreadAsMasterThread();

	/**
	 * Serializes the passed in data via all current output devices.
	 *
	 * @param Data   Text to log.
	 * @param Event  Event name used for suppression purposes.
	 */
	void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) final;

	/**
	 * Serializes the passed in data via all current output devices.
	 *
	 * @param Data   Text to log.
	 * @param Event  Event name used for suppression purposes.
	 */
	void Serialize(const TCHAR* Data, ELogVerbosity::Type Verbosity, const FName& Category) final;
	
	/** Same as Serialize(). */
	void RedirectLog(const FName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data);
	void RedirectLog(const FLazyName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data);

	/** Passes on the flush request to all current output devices. */
	void Flush() final;

	/**
	 * Closes output device and cleans up.
	 *
	 * This can't happen in the destructor as we might have to call "delete" which cannot be done for static/global objects.
	 */
	void TearDown() final;

	/**
	 * Determine if the backlog is enabled.
	 */
	bool IsBacklogEnabled() const;

private:
	/**
	 * The unsynchronized version of FlushThreadedLogs.
	 * Assumes that the caller holds a lock on the output devices.
	 * @param bUseAllDevices   If true, this method will use all buffered output devices.
	 */
	void FlushBufferedLines(TConstArrayView<FOutputDevice*> BufferedDevices, bool bUseAllDevices);

	TPimplPtr<UE::Private::FOutputDeviceRedirectorState> State;
};
