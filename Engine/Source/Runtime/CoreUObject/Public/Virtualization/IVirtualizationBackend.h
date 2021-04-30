// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/StringView.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

namespace UE::Virtualization
{

class FPayloadId;

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
protected:
	/** Enum detailing which operations a backend can support */
	enum class EOperations : uint8
	{
		/** Supports only push operations */
		Push,
		/** Supports only pull operations */
		Pull,
		/** Supports both push and pull operations */
		Both
	};

	IVirtualizationBackend(EOperations InSupportedOperations) 
		: SupportedOperations(InSupportedOperations)
	{

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
	virtual bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload) = 0;

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
	virtual FCompressedBuffer PullData(const FPayloadId& Id) = 0;

	/** Return true if the backend supports push operations. Returning true allows ::PushData to be called.  */
	bool SupportsPushOperations() const { return SupportedOperations == EOperations::Push || SupportedOperations == EOperations::Both;  }

	/** Return true if the backend supports pull operations. Returning true allows ::PullData to be called.  */
	bool SupportsPullOperations() const { return SupportedOperations == EOperations::Pull || SupportedOperations == EOperations::Both;  }

	/** Returns a string that can be used to identify the backend for debugging and logging purposes */
	virtual FString GetDebugString() const = 0;

private:

	/** The operations that this backend supports */
	EOperations SupportedOperations;
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
	 * @param ConfigName	The name given to the back end in the config ini file
	 * @return A new backend instance
	 */
	virtual IVirtualizationBackend* CreateInstance(FStringView ConfigName) = 0;

	/** Returns the name used to identify the type in config ini files */
	virtual FName GetName() = 0;
};

/**
 * This macro is used to generate a backend factories boilerplate code if you do not
 * need anything more than the default behaviour.
 * As well as creating the class, a single instance will be created which will register the factory with
 * 'IModularFeatures' so that it is ready for use.
 * 
 * @param BackendClass The name of the class derived from 'IVirtualizationBackend' that the factory should create
 * @param The name used in config ini files to reference this backend type.
 */
#define UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(BackendClass, ConfigName) \
	class BackendClass##Factory : public IVirtualizationBackendFactory \
	{ \
	public: \
		BackendClass##Factory() { IModularFeatures::Get().RegisterModularFeature(FName("VirtualizationBackendFactory"), this); }\
		virtual ~BackendClass##Factory() { IModularFeatures::Get().UnregisterModularFeature(FName("VirtualizationBackendFactory"), this); } \
	private: \
		virtual IVirtualizationBackend* CreateInstance(FStringView ConfigName) override { return new BackendClass(ConfigName); } \
		virtual FName GetName() override { return FName(#ConfigName); } \
	}; \
	static BackendClass##Factory BackendClass##Factory##Instance;

} // namespace UE::Virtualization
