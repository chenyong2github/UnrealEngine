// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/WeakFieldPtr.h"

REMOTECONTROL_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteControl, Log, All);

class IStructDeserializerBackend;
class IStructSerializerBackend;

/**
 * Reference to a function in a UObject
 */
struct FRCCallReference
{
	FRCCallReference()
		: Object(nullptr)
		, Function(nullptr)
	{}

	bool IsValid() const
	{
		return Object.IsValid() && Function.IsValid();
	}

	TWeakObjectPtr<UObject> Object; 
	TWeakObjectPtr<UFunction> Function;
};

/**
 * Object to hold a UObject remote call
 */
struct FRCCall
{
	bool IsValid() const
	{
		return CallRef.IsValid() && ParamStruct.IsValid();
	}

	FRCCallReference CallRef;
	FStructOnScope ParamStruct;
	bool bGenerateTransaction = false;
};

/**
 * Requested access mode to a remote property
 */
enum class ERCAccess : uint8
{
	NO_ACCESS,
	READ_ACCESS,
	WRITE_ACCESS,
	WRITE_TRANSACTION_ACCESS,
};

/**
 * Reference to a UObject or one of its properties
 */
struct FRCObjectReference
{
	FRCObjectReference()
		: Access(ERCAccess::NO_ACCESS)
		, Object(nullptr)
		, Property(nullptr)
	{}

	bool IsValid() const
	{
		return Object.IsValid();
	}

	ERCAccess Access;
	TWeakObjectPtr<UObject> Object;
	TWeakFieldPtr<FProperty> Property;
};

/**
 * Interface for the remote control module.
 */
class IRemoteControlModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static IRemoteControlModule& Get()
	{
		static const FName ModuleName = "RemoteControl";
		return FModuleManager::LoadModuleChecked<IRemoteControlModule>(ModuleName);
	}

	/**
	 * Resolve a RemoteCall Object and Function.
	 * This will look for object and function to resolve. 
	 * It will only successfully resolve function that are blueprint callable. 
	 * The blueprint function name can be used.
	 * @param ObjectPath	The object path to the UObject we want to resolve
	 * @param FunctionName	The function name of the function we want to resolve
	 * @param OutCallRef	The RemoteCallReference in which the object and function will be resolved into
	 * @param OutErrorText	Optional pointer to an error text in case of resolving error.
	 * @return true if the resolving was successful
	 */
	virtual bool ResolveCall(const FString& ObjectPath, const FString& FunctionName, FRCCallReference& OutCallRef, FString* OutErrorText = nullptr) = 0;

	/**
	 * Invoke a Remote Call
	 * This is a thin wrapper around UObject::ProcessEvent
	 * This expects that the caller has already validated the call as it will assert otherwise.
	 * @param InCall the remote call structure to call.
	 * @return true if the call was allowed and done.
	 */
	virtual bool InvokeCall(FRCCall& InCall) = 0;

	/**
	 * Resolve a remote object reference to a property
	 * @param AccessType the requested access to the object, (i.e. read or write)
	 * @param ObjectPath the object path to resolve
	 * @param PropertyName the property to resolve, if any (specifying no property will return back the whole object when getting/setting it)
	 * @param OutObjectRef the object reference to resolve into
	 * @param OutErrorText an optional error string pointer to write errors into.
	 * @return true if resolving the object and its property succeeded or just the object if no property was specified.
	 */
	virtual bool ResolveObject(ERCAccess AccessType, const FString& ObjectPath, const FString& PropertyName, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) = 0;

	/**
	 * Serialize the Object Reference into the specified backend.
	 * @param ObjectAccess the object reference to serialize, it should be a read access reference.
	 * @param Backend the struct serializer backend to use to serialize the object properties.
	 * @return true if the serialization succeeded
	 */
	virtual bool GetObjectProperties(const FRCObjectReference& ObjectAccess, IStructSerializerBackend& Backend) = 0;

	/**
	 * Deserialize the Object Reference from the specified backend.
	 * @param ObjectAccess the object reference to deserialize into, it should be a write access reference. if the object is WRITE_TRANSACTION_ACCESS, the setting will be wrapped in a transaction.
	 * @param Backend the struct deserializer backend to use to deserialize the object properties.
	 * @return true if the deserialization succeeded
	 */
	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend) = 0;

	/**
	 * Reset the property or the object the Object Reference is pointing to
	 * @param ObjectAccess the object reference to reset, it should be a write access reference
	 * @return true if the reset succeeded.
	 */
	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess) = 0;
};