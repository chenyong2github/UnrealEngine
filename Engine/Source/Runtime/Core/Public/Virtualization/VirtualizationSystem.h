// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateBase.h"
#include "Delegates/IDelegateInstance.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "IO/IoHash.h"
#include "Misc/ConfigCacheIni.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

class FPackagePath;
class FText;
class UObject;

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
enum class EStorageType : int8
{
	/** Store in the local cache backends, this can be called from any thread */
	Local = 0,
	/** Store in the persistent backends, this can only be called from the game thread due to limitations with ISourceControlModule. */
	Persistent
};

/** 
 * The result of a query.
 * Success indicates that the query worked and that the results are valid and can be used.
 * Any other value indicates that the query failed in some manner and that the results cannot be trusted and should be discarded.
 */
enum class EQueryResult : int8
{
	/** The query succeeded and the results are valid */
	Success = 0,
	/** The query failed with an unspecified error */
	Failure_Unknown,
	/** The query failed because the current virtualization system has not implemented it */
	Failure_NotImplemented
};

/** Describes the status of a payload in regards to a backend storage system */
enum class EPayloadStatus : int8
{
	/** The payload id was not value */
	Invalid = -1,
	/** The payload was not found in any backend for the given storage type */
	NotFound = 0,
	/** The payload was found in at least one backend but was not found in all backends available for the given storage type */
	FoundPartial,
	/** The payload was found in all of the backends available for the given storage type */
	FoundAll
};

/** 
 * This interface can be implemented and passed to a FPushRequest as a way of providing the payload 
 * to the virtualization system for a push operation but deferring the loading of the payload from 
 * disk until it is actually needed. In some cases this allows the loading of the payload to be 
 * skipped entirely (if the payload is already in all backends for example) or can prevent memory
 * spikes caused by loading a large number of payloads for a batched push request.
 * 
 * Note that if the backend graph contains multiple backends then payloads may be requested 
 * multiple times. It will be up to the provider implementation to decide if a requested
 * payload should be cached in case of future access or not. The methods are not const in order
 * to make it easier for derived classes to cache the results if needed without the use of
 * mutable.
 */
class IPayloadProvider
{
public:
	IPayloadProvider() = default;
	virtual ~IPayloadProvider() = default;

	/** 
	 * Should return the payload for the given FIoHash. If the provider fails to find the payload
	 * then it should return a null FCompressedBuffer to indicate the error.
	 */
	virtual FCompressedBuffer RequestPayload(const FIoHash& Identifier) = 0;

	/** Returns the current size of the payload on disk. */
	virtual uint64 GetPayloadSize(const FIoHash& Identifier) = 0;
};

/** 
 * Data structure representing a request to push a payload to a backend storage system. 
 * Note that a request can either before for payload already in memory (in which case the
 * payload should be passed into the constructor as a FCompressedBuffer) or by a 
 * IPayloadProvider which will provide the payload on demand.
*/
struct FPushRequest
{
	enum class EStatus
	{
		/** The request failed, or was not reached because of the failure of an earlier request */
		Failed,
		/** The payload does not have a valid identifier or is empty */
		Invalid,
		/** The payload is below the minimum length required for virtualization */
		BelowMinSize,
		/** The payload is owned by a package that is excluded from virtualization by path filtering */
		ExcludedByPackagPath,
		/** The payload in the request is now present in all backends */
		Success
	};

	FPushRequest() = delete;
	~FPushRequest() = default;

	/** 
	 * Create a request for a payload already in memory 
	 *
	 * @param InIdentifier	The hash of the payload in its uncompressed form
	 * @param InPayload		The payload, this can be in any compressed format that the caller wishes.
	 * @param InContext		Content showing where the payload came from. If it comes from a package then this should be the package path
	 */
	FPushRequest(const FIoHash& InIdentifier, const FCompressedBuffer& InPayload, const FString& InContext)
		: Identifier(InIdentifier)
		, Payload(InPayload)
		, Context(InContext)
	{

	}

	/**
	 * Create a request for a payload already in memory
	 *
	 * @param InIdentifier	The hash of the payload in its uncompressed form
	 * @param InPayload		The payload, this can be in any compressed format that the caller wishes.
	 * @param InContext		Content showing where the payload came from. If it comes from a package then this should be the package path
	 */
	FPushRequest(const FIoHash& InIdentifier, FCompressedBuffer&& InPayload, FString&& InContext)
		: Identifier(InIdentifier)
		, Payload(InPayload)
		, Context(InContext)
	{

	}

	/**
	 * Create a request for a payload to be loaded on demand
	 *
	 * @param InIdentifier	The hash of the payload in its uncompressed form
	 * @param InProvider	The provider that will load the payload when requested. The providers lifespan must exceed that of the FPushRequest
	 * @param InContext		Content showing where the payload came from. If it comes from a package then this should be the package path
	 */
	FPushRequest(const FIoHash& InIdentifier, IPayloadProvider& InProvider, FString&& InContext)
		: Identifier(InIdentifier)
		, Provider(&InProvider)
		, Context(InContext)
	{

	}

	/** Return the identifer used in the request */
	FIoHash GetIdentifier() const
	{
		return Identifier;
	}

	/** Returns the current status of the request */
	EStatus GetStatus() const
	{
		return Status;
	}

	/** Returns the size of the payload when it was on disk */
	uint64 GetPayloadSize() const
	{
		if (Provider != nullptr)
		{
			return Provider->GetPayloadSize(Identifier);
		}
		else
		{
			return Payload.GetCompressedSize();
		}
	}
	
	/** Returns the payload */
	FCompressedBuffer GetPayload() const
	{
		if (Provider != nullptr)
		{
			return Provider->RequestPayload(Identifier);
		}
		else
		{
			return Payload;
		}
	}

	/** Returns the context of the payload */
	const FString& GetContext() const
	{
		return Context;
	}

	/** Allows the status of the request to be set, this should only be done by the virtualization backends */
	void SetStatus(EStatus InStatus)
	{
		Status = InStatus;
	}

private:
	/** The identifier of the payload */
	FIoHash Identifier;

	/** The payload data */
	FCompressedBuffer Payload;

	/** Provider to retrieve the payload from */
	IPayloadProvider* Provider = nullptr;

	/** A string containing context for the payload, typically a package name */
	FString Context;

	/** Once the request has been processed this value will contains the results */
	EStatus Status = EStatus::Failed;
};

/** 
 * The set of parameters to be used when initializing the virtualization system. The 
 * members must remain valid for the duration of the call to ::Initialize. It is not
 * expected that any virtualization system will store a reference to the members, if
 * they want to retain the data then they will make their own copies.
 */
struct FInitParams
{
	FInitParams(FStringView InProjectName, const FConfigFile& InConfigFile)
		: ProjectName(InProjectName)
		, ConfigFile(InConfigFile)
	{

	}

	/** The name of the current project (will default to FApp::GetProjectName()) */
	FStringView ProjectName;

	/** The config file to load the settings from (will default to GEngineIni) */
	const FConfigFile& ConfigFile;
};
/**
 * Creates the global IVirtualizationSystem if it has not already been set up. This can be called explicitly
 * during process start up but it will also be called by IVirtualizationSystem::Get if it detects that the
 * IVirtualizationSystem has not yet been set up.
 * 
 * This version will use the default values of FInitParams.
 */
CORE_API void Initialize();

/**
 * This version of ::Initialize takes parameters via the FInitParams structure.
 */
CORE_API void Initialize(const FInitParams& InitParams);

/**
 * Shutdowns the global IVirtualizationSystem if it exists. 
 * Calling this is optional as the system will shut itself down along with the rest of the engine.
 */
CORE_API void Shutdown();

/** 
 * The base interface for the virtualization system. An Epic maintained version can be found in the Virtualization module.
 * To implement your own, simply derived from this interface and then use the
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

	/**
	 * Initialize the system from the parameters given in the FInitParams structure.
	 * The system can only rely on the members of FInitParams to be valid for the duration of the method call, so
	 * if a system needs to retain information longer term then it should make it's own copy of the required data.
	 * 
	 * NOTE: Although it is relatively easy to access cached FConfigFiles, systems should use the one provided 
	 * by InitParams to ensure that the correct settings are parsed.
	 * 
	 * @param InitParam The parameters used to initialize the system
	 * @return				True if the system was initialized correctly, otherwise false. Note that if the method
	 *						returns false then the system will be deleted and the default FNullVirtualizationSystem
	 *						will be used instead.
	 */
	virtual bool Initialize(const FInitParams& InitParams) = 0;

	/** Gain access to the current virtualization system active for the project */
	static IVirtualizationSystem& Get();

	/** Poll to see if content virtualization is enabled or not. */
	virtual bool IsEnabled() const = 0;

	/** Poll to see if pushing virtualized content to the given backend storage type is enabled or not. */
	virtual bool IsPushingEnabled(EStorageType StorageType) const = 0;

	/** 
	 * Poll to see if virtualization is disabled for the given asset type.
	 * 
	 * @param	Owner	The object to be tested, assumed to be an asset that 
	 *					can own virtualized payloads.
	 * 
	 * @return	True if payloads owned by this object should never virtualized
	 */
	virtual bool IsDisabledForObject(const UObject* Owner) const = 0;

	/**
	 * Push a payload to the virtualization backends.
	 *
	 * @param	Id				The identifier of the payload being pushed.
	 * @param	Payload			The payload itself in FCompressedBuffer form, it is assumed that if the buffer is to 
	 *							be compressed that it will have been done by the caller.
	 * @param	StorageType		The type of storage to push the payload to, @See EStorageType for details.
	 * @param	PackageContext	Context for the payload being submitted, typically the name from the UPackage that owns it.
	 * 
	 * @return	True if at least one backend now contains the payload, otherwise false.
	 */
	virtual bool PushData(const FIoHash& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FString& Context) = 0;
	
	/**
	 * Push one or more payloads to a backend storage system. @See FPushRequest.
	 * 
	 * @param	Requests	A list of one or more payloads
	 * @param	StorageType	The type of storage to push the payload to, @See EStorageType for details.
	 * 
	 * @return	When StorageType is Local, this method will return true assuming at least one backend
	 *			managed to push all of the payloads. 
	 *			When StorageType is Persistent, this method will only return true if ALL backends
	 *			manage to push all of the payloads.
	 * 			If this returns true then you can check the Status member of each request for more info 
	 *			about each payloads push operation.
	 *			If this returns false then you can assume that the payloads are not safely virtualized.
	 */
	virtual bool PushData(TArrayView<FPushRequest> Requests, EStorageType StorageType) = 0;

	/**
	 * Pull a payload from the virtualization backends.
	 *
	 * @param	Id The identifier of the payload being pulled.
	 * @return	The payload in the form of a FCompressedBuffer. No decompression will be applied to the payload, it is 
	 *			up to the caller if they want to retain the payload in compressed or uncompressed format.  If no
	 *			backend contained the payload then an empty invalid FCompressedBuffer will be returned.
	 */
	virtual FCompressedBuffer PullData(const FIoHash& Id) = 0;

	/**
	 * Query if a number of payloads exist or not in the given storage type. 
	 * 
	 * @param	Ids					One or more payload identifiers to test
	 * @param	StorageType			The type of storage to push the payload to, @See EStorageType for details.
	 * @param	OutStatuses [out]	An array containing the results for each payload. @See FPayloadStatus
	 * 								If the operation succeeds the array will be resized to match the size of Ids. 
	 * 
	 * @return	True if the operation succeeded and the contents of OutStatuses is valid. False if errors were 
	 * 			encountered in which case the contents of OutStatuses should be ignored.
	 */
	virtual EQueryResult QueryPayloadStatuses(TArrayView<const FIoHash> Ids, EStorageType StorageType, TArray<EPayloadStatus>& OutStatuses) = 0;

	UE_DEPRECATED(5.1, "Call ::QueryPayloadStatuses instead")
	bool DoPayloadsExist(TArrayView<const FIoHash> Ids, EStorageType StorageType, TArray<EPayloadStatus>& OutStatuses)
	{
		return QueryPayloadStatuses(Ids, StorageType, OutStatuses) != EQueryResult::Success;
	}

	/**
	 * Runs the virtualization process on a set of packages. All of the packages will be parsed and any found to be containing locally stored
	 * payloads will have them removed but before they are removed they will be pushed to persistent storage.
	 * 
	 * @param FilesToVirtualize		An array of file paths to packages that should be virtualized. If a path resolves to a file that is not 
	 *								a valid package then it will be silently skipped and will not be considered an error.
	 * @param OutDescriptionTags	The process may produce description tags associated with the packages which will be placed in this array.
	 *								These tags can be used to improve logging, or be appended to change list descriptions etc. Note that the 
	 *								array will be emptied before the process.
	 *								is run and will not contain any pre-existing entries.
	 * @param OutErrors				Any error encountered during the process will be added here. If any error is added to the array then it
	 *								can be assumed that the process will return false. Note that the array will be emptied before the process
	 *								is run and will not contain any pre-existing entries.
	 * 
	 * @return True if the process succeeded and false if it did not. If this returns false then OutErrors should contain at least one entry
	 */
	virtual bool TryVirtualizePackages(const TArray<FString>& FilesToVirtualize, TArray<FText>& OutDescriptionTags, TArray<FText>& OutErrors) = 0;
	
	/**
	 * Runs the re-hydration process on a set of packages. This involves downloading virtualized payloads and placing them back in the trailer of
	 * the given packages.
	 * 
	 * @param Packages	An array containing the absolute file paths of packages. It is assumed that the packages have already been checked out
	 *					of source control (if applicable) and will be writable.
	 * @param OutErrors	Any error encountered during the process will be added here. If any error is added to the array then it
	 *					can be assumed that the process will return false. Note that the array will be emptied before the process
	 *					is run and will not contain any pre-existing entries.
	 * 
	 * @return True if the process succeeded and false if it did not. If this returns false then OutErrors should contain at least one entry
	 */
	virtual bool TryRehydratePackages(const TArray<FString>& Packages, TArray<FText>& OutErrors) = 0;

	/** When called the system should write any performance stats that it has been gathering to the log file */
	virtual void DumpStats() const = 0;

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
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNotification, ENotification, const FIoHash&);

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
