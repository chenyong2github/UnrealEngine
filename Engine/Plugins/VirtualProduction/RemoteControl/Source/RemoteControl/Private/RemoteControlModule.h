// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IRemoteControlModule.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class IRemoteControlInterceptionFeatureProcessor;
struct FAssetData;

/**
 * Implementation of the RemoteControl interface
 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS

class FRemoteControlModule : public IRemoteControlModule
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	//~ Begin IRemoteControlModule
	virtual FOnPresetRegistered& OnPresetRegistered() override;
	virtual FOnPresetUnregistered& OnPresetUnregistered() override;
	virtual bool RegisterPreset(FName Name, URemoteControlPreset* Preset) override;
	virtual void UnregisterPreset(FName Name) override;
	virtual bool ResolveCall(const FString& ObjectPath, const FString& FunctionName, FRCCallReference& OutCallRef, FString* OutErrorText) override;
	virtual bool InvokeCall(FRCCall& InCall, ERCPayloadType InPayloadType = ERCPayloadType::Json, const TArray<uint8>& InInterceptPayload = TArray<uint8>()) override;
	virtual bool ResolveObject(ERCAccess AccessType, const FString& ObjectPath, const FString& PropertyName, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override;
	virtual bool ResolveObjectProperty(ERCAccess AccessType, UObject* Object, FRCFieldPathInfo PropertyPath, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override;
	virtual bool GetObjectProperties(const FRCObjectReference& ObjectAccess, IStructSerializerBackend& Backend) override;
	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend, ERCPayloadType InPayloadType, const TArray<uint8>& InPayload, ERCModifyOperation Operation) override;
	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess, const bool bAllowIntercept) override;
	virtual TOptional<FExposedFunction> ResolvePresetFunction(const FResolvePresetFieldArgs& Args) const override;
	virtual TOptional<FExposedProperty> ResolvePresetProperty(const FResolvePresetFieldArgs& Args) const override;
	virtual URemoteControlPreset* ResolvePreset(FName PresetName) const override;
	virtual URemoteControlPreset* ResolvePreset(const FGuid& PresetId) const override;
	virtual void GetPresets(TArray<TSoftObjectPtr<URemoteControlPreset>>& OutPresets) const override;
	virtual void GetPresetAssets(TArray<FAssetData>& OutPresetAssets) const override;
	virtual const TMap<FName, FEntityMetadataInitializer>& GetDefaultMetadataInitializers() const override;
	virtual bool RegisterDefaultEntityMetadata(FName MetadataKey, FEntityMetadataInitializer MetadataInitializer) override;
	virtual void UnregisterDefaultEntityMetadata(FName MetadataKey) override;
	virtual bool PropertySupportsRawModificationWithoutEditor(FProperty* Property, UClass* OwnerClass = nullptr) const override;
	virtual void RegisterEntityFactory( const FName InFactoryName, const TSharedRef<IRemoteControlPropertyFactory>& InFactory) override;
	virtual void UnregisterEntityFactory( const FName InFactoryName ) override;
	virtual const TMap<FName, TSharedPtr<IRemoteControlPropertyFactory>>& GetEntityFactories() const override { return EntityFactories; };
	//~ End IRemoteControlModule

private:
	/** Cache all presets in the project for the ResolvePreset function. */
	void CachePresets() const;
	
	//~ Asset registry callbacks
	void OnAssetAdded(const FAssetData& AssetData);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString&);

	/** Determines if a property modification should use a setter or default to deserializing directly onto an object. */
	static bool PropertyModificationShouldUseSetter(UObject* Object, FProperty* Property);

	/**
	 * Deserialize data for a non-EQUAL modification request and apply the operation to the resulting data.
	 * 
	 * @param ObjectAccess Data about the object/property for which modification was requested.
	 * @param Backend Deserialization backend for the modification request.
	 * @param Operation Type of operation to apply to the value.
	 * @param OutData Buffer used to deserialize data from the backend and contain the result value.
	 * @return True if the data was successfully deserialized and modified.
	 */
	static bool DeserializeDeltaModificationData(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend, ERCModifyOperation Operation, TArray<uint8>& OutData);

#if WITH_EDITOR
	/** Finalize an ongoing change, triggering post edit change on the tracked object. */
	void TestOrFinalizeOngoingChange(bool bForceEndChange = false);

	// End ongoing change on map preload.
	void HandleMapPreLoad(const FString& MapName);
	
	/** Callback to handle registering delegates once the engine has finished its startup. */
	void HandleEnginePostInit();

	//~ Register/Unregister editor delegates.
	void RegisterEditorDelegates();
	void UnregisterEditorDelegates();
	
#endif

private:
	/** Cache of preset names to preset assets */
	mutable TMap<FName, TArray<FAssetData>> CachedPresetsByName;

	/** Cache of ids to preset names. */
	mutable TMap<FGuid, FName> CachedPresetNamesById;

	/** Map of registered default metadata initializers. */
	TMap<FName, FEntityMetadataInitializer> DefaultMetadataInitializers;

	/** Delegate for preset registration */
	FOnPresetRegistered OnPresetRegisteredDelegate;

	/** Delegate for preset unregistration */
	FOnPresetUnregistered OnPresetUnregisteredDelegate;

	/** RC Processor feature instance */
	TUniquePtr<IRemoteControlInterceptionFeatureProcessor> RCIProcessor;

#if WITH_EDITOR
	/** Flags for a given RC change. */
	struct FOngoingChange
	{
		FOngoingChange(FRCObjectReference InReference);
		FOngoingChange(FRCCallReference InReference);
		
		friend uint32 GetTypeHash(const FOngoingChange& Modification)
		{
			if (Modification.Reference.IsType<FRCObjectReference>())
			{
				return GetTypeHash(Modification.Reference.Get<FRCObjectReference>());
			}
			else
			{
				return GetTypeHash(Modification.Reference.Get<FRCCallReference>());
			}
		}

		/** Reference to either the property we're modifying or the function we're calling. */
		TVariant<FRCObjectReference, FRCCallReference> Reference;
		/** Whether this change was triggered with a transaction or not. */
		bool bHasStartedTransaction = false;
		/** Whether this change was triggered since the last tick of OngoingChangeTimer. */
		bool bWasTriggeredSinceLastPass = true;
	};
	
	/** Ongoing change that needs to end its transaction and call PostEditChange in the case of a property modification. */
	TOptional<FOngoingChange> OngoingModification;

	/** Handle to the timer that that ends the ongoing change in regards to PostEditChange and transactions. */
	FTimerHandle OngoingChangeTimer;

	/** Delay before we check if a modification is no longer ongoing. */
	static constexpr float SecondsBetweenOngoingChangeCheck = 0.2f;
#endif

	/** Map of the factories which is responsible for the Remote Control property creation */
	TMap<FName, TSharedPtr<IRemoteControlPropertyFactory>> EntityFactories;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);
