// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/StringView.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "Templates/UniquePtr.h"
#include "Virtualization/VirtualizationSystem.h"

struct FIoHash;

namespace UE::Virtualization
{

/** Describes the result of a IVirtualizationBackend::Push operation */
enum class EPushResult
{
	/** The push failed, the backend should print an error message to 'LogVirtualization'.*/
	Failed = 0,
	/** The payload already exists in the backend and does not need to be pushed. */
	PayloadAlreadyExisted,
	/** The payload was successfully pushed to the backend. */
	Success
};

/**
 * The interface to derive from to create a new backend implementation.
 * 
 * Note that virtualization backends are instantiated FVirtualizationManager via  
 * IVirtualizationBackendFactory so each new backend derived from IVirtualizationBackend  
 * will also need a factory derived from IVirtualizationBackendFactory. You can either do
 * this manually or use the helper macro 'UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY' to 
 * generate the code for you.
 *
 */
class IVirtualizationBackend
{
public: 
	/** Enum detailing which operations a backend can support */
	enum class EOperations : uint8
	{
		/** Supports no operations, this should only occur when debug settings are applied */
		None = 0,
		/** Supports only push operations */
		Push = 1 << 0,
		/** Supports only pull operations */
		Pull = 1 << 1,
	};

	FRIEND_ENUM_CLASS_FLAGS(EOperations);

protected:
	
	IVirtualizationBackend(FStringView InConfigName, FStringView InDebugName, EOperations InSupportedOperations)
		: SupportedOperations(InSupportedOperations)
		, DebugDisabledOperations(EOperations::None)
		, ConfigName(InConfigName)
		, DebugName(InDebugName)
	{
		checkf(InSupportedOperations != EOperations::None, TEXT("Cannot create a backend with no supported operations!"));
	}

public:
	virtual ~IVirtualizationBackend() = default;

	/**
	 * This will be called during the setup of the backend hierarchy. The entry config file
	 * entry that caused the backend to be created will be passed to the method so that any
	 * additional settings may be parsed from it.
	 * Take care to clearly log any error that occurs so that the end user has a clear way 
	 * to fix them.
	 * 
	 * @param ConfigEntry	The entry for the backend from the config ini file that may 
	 *						contain additional settings.
	 * @return				Returning false indicates that initialization failed in a way 
	 *						that the backend will not be able to function correctly.
	 */
	virtual bool Initialize(const FString& ConfigEntry) = 0;

	/**
	 * The backend will attempt to store the given payload by what ever method the backend uses.
	 * NOTE: It is assumed that the virtualization manager will run all appropriate validation
	 * on the payload and it's id and that the inputs to PushData can be trusted.
	 * 
	 * @param Id		The Id of the payload
	 * @param Payload	A potentially compressed buffer representing the payload
	 * @return			The result of the push operation
	 */
	virtual EPushResult PushData(const FIoHash& Id, const FCompressedBuffer& Payload, const FString& PackageContext) = 0;

	virtual bool PushData(TArrayView<FPushRequest> Requests)
	{
		// TODO: Improve the error codes in the future
		for (FPushRequest& Request : Requests)
		{
			EPushResult Result = PushData(Request.GetIdentifier(), Request.GetPayload(), Request.GetContext());
			switch (Result)
			{
			case EPushResult::Failed:
				Request.SetStatus(FPushRequest::EStatus::Failed);
				return false;

			case EPushResult::PayloadAlreadyExisted:
				// falls through
			case EPushResult::Success:
				Request.SetStatus(FPushRequest::EStatus::Success);
				break;

			default:
				Request.SetStatus(FPushRequest::EStatus::Failed);
				checkNoEntry();
				break;
			}
		}

		return true;
	}

	/** 
	 * The backend will attempt to retrieve the given payload by what ever method the backend uses.
	 * NOTE: It is assumed that the virtualization manager will validate the returned payload to 
	 * make sure that it matches the requested id so there is no need for each backend to do this/
	 * 
	 * @param Id	The Id of a payload to try and pull from the backend.
	 * 
	 * @return		A valid FCompressedBuffer containing the payload if the pull
	 *				operation succeeded and a null FCompressedBuffer
	 *				if it did not.
	 */
	virtual FCompressedBuffer PullData(const FIoHash& Id) = 0;
	
	/**
	 * Checks if a payload exists in the backends storage.
	 * 
	 * @param	Id	The identifier of the payload to check
	 * 
	 * @return True if the backend storage already contains the payload, otherwise false
	 */
	virtual bool DoesPayloadExist(const FIoHash& Id) = 0;
	
	/**
	 * Checks if a number of payload exists in the backends storage.
	 *
	 * @param[in]	PayloadIds	An array of FIoHash that should be checked
	 * @param[out]	OutResults	An array to contain the result, true if the payload
	 *							exists in the backends storage, false if not.
	 *							This array will be resized to match the size of PayloadIds.
	 * 
	 * @return True if the operation completed without error, otherwise false
	 */
	virtual bool DoPayloadsExist(TArrayView<const FIoHash> PayloadIds, TArray<bool>& OutResults)
	{
		// This is the default implementation that just calls ::DoesExist on each FIoHash in the
		// array, one at a time. 
		// Backends may override this with their own implementations if it can be done with less
		// overhead by performing the check on the entire batch instead.

		OutResults.SetNum(PayloadIds.Num());

		for (int32 Index = 0; Index < PayloadIds.Num(); ++Index)
		{
			OutResults[Index] = DoesPayloadExist(PayloadIds[Index]);
		}

		return true;
	}
	
	/** 
	 * Returns true if the given operation is supported, this is set when the backend is created
	 * and should not change over it's life time.
	 */
	bool IsOperationSupported(EOperations Operation) const
	{
		return EnumHasAnyFlags(SupportedOperations, Operation);
	}

	/** Enable or disable the given operation based on the 'bIsDisabled' parameter */
	void SetOperationDebugState(EOperations Operation, bool bIsDisabled)
	{
		if (bIsDisabled)
		{
			EnumAddFlags(DebugDisabledOperations, Operation);
			
		}
		else
		{
			EnumRemoveFlags(DebugDisabledOperations, Operation);
		}
	}

	/** Returns true if the given operation is disabled for debugging purposes */
	bool IsOperationDebugDisabled(EOperations Operation) const
	{
		return EnumHasAnyFlags(DebugDisabledOperations, Operation);
	}

	/** Returns a string containing the name of the backend as it appears in the virtualization graph in the config file */
	const FString& GetConfigName() const
	{
		return ConfigName;
	}

	/** Returns a string that can be used to identify the backend for debugging and logging purposes */
	const FString& GetDebugName() const
	{
		return DebugName;
	}

private:

	/** The operations that this backend supports */
	EOperations SupportedOperations;

	EOperations DebugDisabledOperations;

	/** The name assigned to the backend by the virtualization graph */
	FString ConfigName;

	/** Combination of the backend type and the name used to create it in the virtualization graph */
	FString DebugName;
};

/** 
 * Derive from this interface to implement a factory to return a backend type.
 * An instance of the factory should be created and then registered to 
 * IModularFeatures with the feature name "VirtualizationBackendFactory" to
 * give 'FVirtualizationManager' access to it. 
 * The macro 'UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY' can be used to create 
 * a factory easily if you do not want to specialize the behaviour.
*/
class IVirtualizationBackendFactory : public IModularFeature
{
public:
	/** 
	 * Creates a new backend instance.
	 * 
	 * @param ProjectName	The name of the current project
	 * @param ConfigName	The name given to the back end in the config ini file
	 * @return A new backend instance
	 */
	virtual TUniquePtr<IVirtualizationBackend> CreateInstance(FStringView ProjectName, FStringView ConfigName) = 0;

	/** Returns the name used to identify the type in config ini files */
	virtual FName GetName() = 0;
};

ENUM_CLASS_FLAGS(IVirtualizationBackend::EOperations);

/**
 * This macro is used to generate a backend factories boilerplate code if you do not
 * need anything more than the default behavior.
 * As well as creating the class, a single instance will be created which will register the factory with
 * 'IModularFeatures' so that it is ready for use.
 * 
 * @param BackendClass The name of the class derived from 'IVirtualizationBackend' that the factory should create
 * @param The name used in config ini files to reference this backend type.
 */
#define UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(BackendClass, ConfigName) \
	class F##BackendClass##Factory : public IVirtualizationBackendFactory \
	{ \
	public: \
		F##BackendClass##Factory() { IModularFeatures::Get().RegisterModularFeature(FName("VirtualizationBackendFactory"), this); }\
		virtual ~F##BackendClass##Factory() { IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationBackendFactory"), this); } \
	private: \
		virtual TUniquePtr<IVirtualizationBackend> CreateInstance(FStringView ProjectName, FStringView ConfigName) override \
		{ \
			return MakeUnique<BackendClass>(ProjectName, ConfigName, WriteToString<256>(#ConfigName, TEXT(" - "), ConfigName).ToString()); \
		} \
		virtual FName GetName() override { return FName(#ConfigName); } \
	}; \
	static F##BackendClass##Factory BackendClass##Factory##Instance;

} // namespace UE::Virtualization
