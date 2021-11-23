// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "Virtualization/PayloadId.h"

class FPackagePath;

namespace UE::Virtualization
{

/** Profiling data containing all activity relating to payloads. */
struct FPayloadActivityInfo
{
	struct FActivity
	{
		/** The number of payloads that have been involved by the activity. */
		int64 PayloadCount = 0;
		/** The total size of all payloads involved in the activity, in bytes. */
		int64 TotalBytes = 0;
		/** The total number of cycles spent on the activity across all threads. */
		int64 CyclesSpent = 0;
	};

	FActivity Pull;
	FActivity Push;
	FActivity Cache;
};

/** Describes the type of storage to use for a given action */
enum EStorageType
{
	/** Store in the local cache backends, this can be called from any thread */
	Local = 0,
	/** Store in the persistent backends, this can only be called from the game thread due to limitations with ISourceControlModule. */
	Persistent
};

/**
 * Creates the global IVirtualizationSystem if it has not already been set up. This can be called explicitly
 * during process start up but it will also be called by IVirtualizationSystem::Get if it detects that the
 * IVirtualizationSystem has not yet bene set up.
 */
CORE_API void Initialize();

/** 
 * The base interface for the virtualization system. An Epic maintained version can be found in the Virtualization module.
 To implement your own, simply derived from this interface and then use the
 * UE_REGISTER_VIRTUALIZATION_SYSTEM macro in the cpp to register it as an option. 
 * You can then set the config file option [Core.ContentVirtualization]SystemName=FooBar, where FooBar should be the 
 * SystemName parameter you used when registering with the macro.
 * 
 * Special Cases:
 * SystemName=Off		-	This is the default set up and means a project will not use content virtualization
 *							Note that calling IVirtualizationSystem::Get() will still return a valid 
 *							IVirtualizationSystem implementation, but all push and pull operations will result 
 *							in failure and IsEnabled will always return false.
 * SystemName=Default	-	This will cause the default Epic implementation to be used @see VirtualizationManager
 */
class CORE_API IVirtualizationSystem
{
public:
	IVirtualizationSystem() = default;
	virtual ~IVirtualizationSystem() = default;

	/** Gain access to the current virtualization system active for the project */
	static IVirtualizationSystem& Get();

	/** Poll to see if content virtualization is enabled or not. */
	virtual bool IsEnabled() const = 0;

	/**
	 * Push a payload to the virtualization backends.
	 *
	 * @param	Id				The identifier of the payload being pushed.
	 * @param	Payload			The payload itself in FCompressedBuffer form, it is
	 *							assumed that if the buffer is to be compressed that
	 *							it will have been done by the caller.
	 * @param	StorageType		The type of storage to push the payload to, see EStorageType
	 *							for details.
	 * @param	PackageContext	Name of the owning package, which can be used to provide context
	 *							about the payload.
	 * @return	True if at least one backend now contains the payload, otherwise false.
	 */
	virtual bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FPackagePath& PackageContext) = 0;

	/**
	 * Pull a payload from the virtualization backends.
	 *
	 * @param	Id The identifier of the payload being pulled.
	 * @return	The payload in the form of a FCompressedBuffer. No decompression will
	 *			be applied to the payload, it is up to the caller if they want to
	 *			retain the payload in compressed or uncompressed format.
	 *			If no backend contained the payload then an empty invalid FCompressedBuffer
	 *			will be returned.
	 */
	virtual FCompressedBuffer PullData(const FPayloadId& Id) = 0;

	using GetPayloadActivityInfoFuncRef = TFunctionRef<void(const FString& DebugName, const FString& ConfigName, const FPayloadActivityInfo& PayloadInfo)>;

	/** Access profiling info relating to payload activity per backend. Stats will only be collected if ENABLE_COOK_STATS is enabled.*/
	virtual void GetPayloadActivityInfo( GetPayloadActivityInfoFuncRef ) const = 0;

	/** Access profiling info relating to accumulated payload activity. Stats will only be collected if ENABLE_COOK_STATS is enabled.*/
	virtual FPayloadActivityInfo GetAccumualtedPayloadActivityInfo() const = 0;

	//* Notification messages
	enum ENotification
	{
		PushBegunNotification,
		PushEndedNotification,
		PushFailedNotification,

		PullBegunNotification,
		PullEndedNotification,
		PullFailedNotification,
	};

	/** Declare delegate for notifications*/
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNotification, ENotification, const FPayloadId&);

	virtual FOnNotification& GetNotificationEvent() = 0;
};

namespace Private
{

/** 
 * Factory interface for creating virtualization systems. This is not intended to be derived from 
 * directly. Use the provided UE_REGISTER_VIRTUALIZATION_SYSTEM macro instead 
 */
class IVirtualizationSystemFactory : public IModularFeature
{
public:
	/** Creates and returns a new virtualization system instance */
	virtual TUniquePtr<IVirtualizationSystem> Create() = 0;

	/** Returns the name of the system that this factory created */
	virtual FName GetName() = 0;
};

} // namespace Private

/**
 * Registers a class derived from IVirtualizationSystem so that it can be set as the virtualization system for
 * the process to use.
 * 
 * @param SystemClass	The class derived from IVirtualizationSystem
 * @param SystemName	The name of the system that will be used to potentially select the system for use
 */
#define UE_REGISTER_VIRTUALIZATION_SYSTEM(SystemClass, SystemName) \
	class FVirtualizationSystem##Factory : public Private::IVirtualizationSystemFactory \
	{ \
	public: \
		FVirtualizationSystem##Factory() { IModularFeatures::Get().RegisterModularFeature(FName("VirtualizationSystem"), this); }\
		virtual ~FVirtualizationSystem##Factory() { IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationSystem"), this); } \
	private: \
		virtual TUniquePtr<IVirtualizationSystem> Create() override { return MakeUnique<SystemClass>(); } \
		virtual FName GetName() override { return FName(#SystemName); } \
	}; \
	static FVirtualizationSystem##Factory FVirtualizationSystem##Factory##Instance;

namespace Experimental
{

class IVirtualizationSourceControlUtilities : public IModularFeature
{
public:
	/**
	 * Given a package path this method will attempt to sync th e.upayload file that is compatible with
	 * the .uasset file of the package.
	 *
	 * We can make the following assumptions about the relationship between .uasset and .upayload files:
	 * 1) The .uasset may be submitted to perforce without the .upayload (if the payload is unmodified)
	 * 2) If the payload is modified then the .uasset and .upayload file must be submitted at the same time.
	 * 3) The caller has already checked the existing .upayload file (if any) to see if it contains the payload
	 * that they are looking for.
	 *
	 * If the above is true then we can sync the .upayload file to the same perforce changelist as the
	 * * .uasset and be sure that we have the correct version.
	 *
	 * Note that this has only been tested with perforce and so other source control solutions are currently
	 * unsupported.
	 */
	virtual bool SyncPayloadSidecarFile(const FPackagePath& PackagePath) = 0;
};

} // namespace Experimental

} // namespace UE::Virtualization
