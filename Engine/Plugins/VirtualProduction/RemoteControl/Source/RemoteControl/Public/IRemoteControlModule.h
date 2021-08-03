// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlField.h"
#include "RemoteControlFieldPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/WeakFieldPtr.h"

REMOTECONTROL_API DECLARE_LOG_CATEGORY_EXTERN(LogRemoteControl, Log, All);

class IStructDeserializerBackend;
class IStructSerializerBackend;
class URemoteControlPreset;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FExposedProperty;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
struct FRemoteControlProperty;

/**
 * Delegate called to initialize an exposed entity metadata entry registered with the RegisterDefaultEntityMetadata method.
 * Use RemoteControlPreset::GetExposedEntity to retrieve the entity that will contain the metadata.
 * The delegate return value will be put into metadata map for the metadata key that was used when registering the default metadata entry.
 */
DECLARE_DELEGATE_RetVal_TwoParams(FString /*Value*/, FEntityMetadataInitializer, URemoteControlPreset* /*Preset*/, const FGuid& /*EntityId*/);

/**
 * Delegate called after a property has been modified through SetObjectProperties..
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostPropertyModifiedRemotely, const FRCObjectReference& /*ObjectRef*/);

/**
 * Deserialize payload type for interception purposes
 */
enum class ERCPayloadType : uint8
{
	Cbor,
	Json
};

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
	
	friend uint32 GetTypeHash(const FRCCallReference& CallRef)
	{
		return CallRef.IsValid() ? HashCombine(GetTypeHash(CallRef.Object), GetTypeHash(CallRef.Function)) : 0;
	}
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
 * Object to hold information necessary to resolve a preset property/function.
 */
struct FResolvePresetFieldArgs
{
	FString PresetName;
	FString FieldLabel;
};

/**
 * Requested access mode to a remote property
 */
UENUM()
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
	FRCObjectReference() = default;

	FRCObjectReference(ERCAccess InAccessType, UObject* InObject)
		: Access(InAccessType)
		, Object(InObject)
	{
		check(InObject);
		ContainerType = InObject->GetClass();
		ContainerAdress = static_cast<void*>(InObject);
	}

	FRCObjectReference(ERCAccess InAccessType, UObject* InObject, FRCFieldPathInfo InPathInfo)
		: Access(InAccessType)
		, Object(InObject)
		, PropertyPathInfo(MoveTemp(InPathInfo))
	{
		check(InObject);
		PropertyPathInfo.Resolve(InObject);
		Property = PropertyPathInfo.GetResolvedData().Field;
		ContainerAdress = PropertyPathInfo.GetResolvedData().ContainerAddress;
		ContainerType = PropertyPathInfo.GetResolvedData().Struct;
		PropertyPathInfo = MoveTemp(PropertyPathInfo);
	}

	bool IsValid() const
	{
		return Object.IsValid() && ContainerType.IsValid() && ContainerAdress != nullptr;
	}

	friend bool operator==(const FRCObjectReference& LHS, const FRCObjectReference& RHS)
	{
		return LHS.Object == RHS.Object && LHS.Property == RHS.Property && LHS.ContainerAdress == RHS.ContainerAdress;
	}

	friend uint32 GetTypeHash(const FRCObjectReference& ObjectReference)
	{
		return HashCombine(GetTypeHash(ObjectReference.Object), ObjectReference.PropertyPathInfo.PathHash);
	}

	/** Type of access on this object (read, write) */
	ERCAccess Access = ERCAccess::NO_ACCESS;

	/** UObject owning the target property */
	TWeakObjectPtr<UObject> Object;

	/** Actual property that is being referenced */
	TWeakFieldPtr<FProperty> Property;

	/** Address of the container of the property for serialization purposes in case of a nested property */
	void* ContainerAdress = nullptr;

	/** Type of the container where the property resides */
	TWeakObjectPtr<UStruct> ContainerType;

	/** Path to the property under the Object */
	FRCFieldPathInfo PropertyPathInfo;
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

	/** Delegate triggered when a preset has been registered */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresetRegistered, FName /*PresetName*/);
	UE_DEPRECATED(4.27, "OnPresetUnregistered is deprecated.")
	virtual FOnPresetRegistered& OnPresetRegistered() = 0;

	/** Delegate triggered when a preset has been unregistered */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresetUnregistered, FName /*PresetName*/);
	UE_DEPRECATED(4.27, "OnPresetUnregistered is deprecated.")
	virtual FOnPresetUnregistered& OnPresetUnregistered() = 0;

	/**
	 * Register the preset with the module, enabling using the preset remotely using its name.
	 * @return whether registration was successful.
	 */
	UE_DEPRECATED(4.27, "RegisterPreset is deprecated.")
	virtual bool RegisterPreset(FName Name, URemoteControlPreset* Preset) = 0;

	/** Unregister a preset */
	UE_DEPRECATED(4.27, "UnregisterPreset is deprecated.")
	virtual void UnregisterPreset(FName Name) = 0;

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
	 * @param InPayloadType the payload type archive.
	 * @param InInterceptPayload the payload reference archive for the interception.
	 * @return true if the call was allowed and done.
	 */
	virtual bool InvokeCall(FRCCall& InCall, ERCPayloadType InPayloadType = ERCPayloadType::Json, const TArray<uint8>& InInterceptPayload = TArray<uint8>()) = 0;

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
	 * Resolve a remote object reference to a property
	 * @param AccessType the requested access to the object, (i.e. read or write)
	 * @param Object the object to resolve the property on
	 * @param PropertyPath the path to or the name of the property to resolve. Specifying an empty path will return back the whole object when getting/setting it)
	 * @param OutObjectRef the object reference to resolve into
	 * @param OutErrorText an optional error string pointer to write errors into.
	 * @return true if resolving the object and its property succeeded or just the object if no property was specified.
	 */
	virtual bool ResolveObjectProperty(ERCAccess AccessType, UObject* Object, FRCFieldPathInfo PropertyPath, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) = 0;

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
	 * @param InPayloadType the payload type archive.
	 * @param InInterceptPayload the payload reference archive for the interception.
	 * @return true if the deserialization succeeded
	 */
	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend, ERCPayloadType InPayloadType = ERCPayloadType::Json, const TArray<uint8>& InInterceptPayload = TArray<uint8>()) = 0;

	/**
	 * Reset the property or the object the Object Reference is pointing to
	 * @param ObjectAccess the object reference to reset, it should be a write access reference
	 * @param bAllowIntercept interception flag, if that is set to true it should follow the interception path
	 * @return true if the reset succeeded.
	 */
	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess, const bool bAllowIntercept = false) = 0;

	/**
	 * Resolve the underlying function from a preset.
	 * @return the underlying function and objects that the property is exposed on.
	 */
	UE_DEPRECATED(4.27, "This function is deprecated, please resolve directly on the preset.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual TOptional<struct FExposedFunction> ResolvePresetFunction(const FResolvePresetFieldArgs& Args) const = 0;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	/**
	 * Resolve the underlying property from a preset.
	 * @return the underlying property and objects that the property is exposed on.
	 */
	UE_DEPRECATED(4.27, "This function is deprecated, please resolve directly on the preset.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual TOptional<struct FExposedProperty> ResolvePresetProperty(const FResolvePresetFieldArgs& Args) const = 0;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	/**
	 * Get a preset using its name.
	 * @arg PresetName name of the preset to resolve.
	 * @return the preset if found.
	 */
	virtual URemoteControlPreset* ResolvePreset(FName PresetName) const = 0;

	/**
     * Get a preset using its id.
     * @arg PresetId id of the preset to resolve.
     * @return the preset if found.
     */
	virtual URemoteControlPreset* ResolvePreset(const FGuid& PresetId) const = 0;

	/**
	 * Get all the presets currently registered with the module.
	 */
	virtual void GetPresets(TArray<TSoftObjectPtr<URemoteControlPreset>>& OutPresets) const = 0;

	/**
	 * Get all the preset asset currently registered with the module.
	 */
	virtual void GetPresetAssets(TArray<FAssetData>& OutPresetAssets) const = 0;
	
	/**
	 * Get the map of registered default entity metadata initializers. 
	 */
	virtual const TMap<FName, FEntityMetadataInitializer>& GetDefaultMetadataInitializers() const = 0;
	
	/**
	 * Register a default entity metadata which will show up in an entity's detail panel.
	 * The initializer will be called upon an entity being exposed or when a preset is loaded in
	 * order to update all existing entities that don't have that metadata key.
	 * @param MetadataKey The desired metadata key.
	 * @param MetadataInitializer The delegate to call to handle initializing the metadata.
	 */
	virtual bool RegisterDefaultEntityMetadata(FName MetadataKey, FEntityMetadataInitializer MetadataInitializer) = 0;

	/**
	 * Unregister a default entity metadata.
	 * @param MetadataKey The metadata entry to unregister.
	 */
	virtual void UnregisterDefaultEntityMetadata(FName MetadataKey) = 0;

	/**
	 * Returns whether the property can be modified through SetObjectProperties when running without an editor.
	 */
	virtual bool PropertySupportsRawModificationWithoutEditor(FProperty* Property, UClass* OwnerClass = nullptr) const = 0;
};
