// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRemoteControlModule.h"
#include "IRemoteControlInterceptionFeature.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreMisc.h"
#include "IStructSerializerBackend.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlInterceptionHelpers.h"
#include "RemoteControlInterceptionProcessor.h"
#include "RemoteControlPreset.h"
#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "UObject/FieldPath.h"


#if WITH_EDITOR
	#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY(LogRemoteControl);
#define LOCTEXT_NAMESPACE "RemoteControl"

namespace RemoteControlUtil
{
	const FName NAME_DisplayName(TEXT("DisplayName"));
	const FName NAME_ScriptName(TEXT("ScriptName"));
	const FName NAME_ScriptNoExport(TEXT("ScriptNoExport"));
	const FName NAME_DeprecatedFunction(TEXT("DeprecatedFunction"));
	const FName NAME_BlueprintGetter(TEXT("BlueprintGetter"));
	const FName NAME_BlueprintSetter(TEXT("BlueprintSetter"));
	const FName NAME_AllowPrivateAccess(TEXT("AllowPrivateAccess"));

	bool CompareFunctionName(const FString& FunctionName, const FString& ScriptName)
	{
		int32 SemiColonIndex = INDEX_NONE;
		if (ScriptName.FindChar(TEXT(';'), SemiColonIndex))
		{
			return FCString::Strncmp(*ScriptName, *FunctionName, SemiColonIndex) == 0;
		}
		return ScriptName.Equals(FunctionName);
	}

	UFunction* FindFunctionByNameOrMetaDataName(UObject* Object, const FString& FunctionName)
	{
		UFunction* Function = Object->FindFunction(FName(*FunctionName));
#if WITH_EDITOR
		// if the function wasn't found through the function map, try finding it through its `ScriptName` or `DisplayName` metadata
		if (Function == nullptr)
		{
			for (TFieldIterator<UFunction> FuncIt(Object->GetClass(), EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated, EFieldIteratorFlags::IncludeInterfaces); FuncIt; ++FuncIt)
			{
				if (FuncIt->HasMetaData(NAME_ScriptName) && CompareFunctionName(FunctionName, FuncIt->GetMetaData(NAME_ScriptName)))
				{
					Function = *FuncIt;
					break;
				}
				else if (FuncIt->HasMetaData(NAME_DisplayName) && CompareFunctionName(FunctionName, FuncIt->GetMetaData(NAME_DisplayName)))
				{
					Function = *FuncIt;
					break;
				}
			}
		}
#endif
		return Function;
	}

	bool IsPropertyAllowed(const FProperty* InProperty, ERCAccess InAccessType, bool bObjectInGamePackage)
	{
		// The property is allowed to be accessed if it exists...
		return InProperty && 
			// it doesn't have exposed getter/setter that should be used instead
#if WITH_EDITOR
			(!InProperty->HasMetaData(RemoteControlUtil::NAME_BlueprintGetter) || !InProperty->HasMetaData(RemoteControlUtil::NAME_BlueprintSetter)) &&
			// it isn't private or protected, except if AllowPrivateAccess is true
			(!InProperty->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected | CPF_NativeAccessSpecifierPrivate) || InProperty->GetBoolMetaData(RemoteControlUtil::NAME_AllowPrivateAccess)) &&
#endif
			// it isn't blueprint private
			!InProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance) &&
			// and it's either blueprint visible if in game or editable if in editor and it isn't read only if the access type is write
			(bObjectInGamePackage ?
				InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible) && (InAccessType == ERCAccess::READ_ACCESS || !InProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) :
				InProperty->HasAnyPropertyFlags(CPF_Edit) && (InAccessType == ERCAccess::READ_ACCESS || !InProperty->HasAnyPropertyFlags(CPF_EditConst)));
	};

	FARFilter GetBasePresetFilter()
	{
		FARFilter Filter;
        Filter.bIncludeOnlyOnDiskAssets = false;
        Filter.ClassNames = { URemoteControlPreset::StaticClass()->GetFName() };
        Filter.bRecursivePaths = true;
        Filter.PackagePaths = { TEXT("/") };
		
		return Filter;
	}
	
	void GetAllPresetAssets(TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.GetAssets(GetBasePresetFilter(), OutAssets);
	}

	URemoteControlPreset* GetFirstPreset(const FARFilter& Filter)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		return Assets.Num() ? CastChecked<URemoteControlPreset>(Assets[0].GetAsset()) : nullptr;
	}

	URemoteControlPreset* GetPresetById(const FGuid& Id)
	{
		FARFilter Filter = GetBasePresetFilter();
		Filter.TagsAndValues.Add(FName("PresetId"), Id.ToString());
		return GetFirstPreset(Filter);
	}

	URemoteControlPreset* GetPresetByName(FName PresetName)
	{
		FARFilter Filter = GetBasePresetFilter();
		Filter.PackageNames = { PresetName };
		return GetFirstPreset(Filter);
	}

	FGuid GetPresetId(const FAssetData& PresetAsset)
	{
		FGuid Id;
		FAssetDataTagMapSharedView::FFindTagResult Result = PresetAsset.TagsAndValues.FindTag(FName("PresetId"));
		if (Result.IsSet())
		{
			Id = FGuid{Result.GetValue()};
		}

		return Id;
	}
}

/**
 * Implementation of the RemoteControl interface
 */
class FRemoteControlModule : public IRemoteControlModule
{
public:
	virtual void StartupModule() override
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddRaw(this, &FRemoteControlModule::CachePresets);
		}
		else
		{
			CachePresets();
		}

		// Instantiate the RCI processor feature on module start
		RCIProcessor = MakeUnique<FRemoteControlInterceptionProcessor>();
		// Register the interceptor feature
		IModularFeatures::Get().RegisterModularFeature(IRemoteControlInterceptionFeatureProcessor::GetName(), RCIProcessor.Get());
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded(AssetRegistryConstants::ModuleName))
		{
			IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
			AssetRegistry.OnFilesLoaded().RemoveAll(this);
		}

		// Unregister the interceptor feature on module shutdown
		IModularFeatures::Get().UnregisterModularFeature(IRemoteControlInterceptionFeatureProcessor::GetName(), RCIProcessor.Get());
	}
	
	virtual FOnPresetRegistered& OnPresetRegistered() override
	{
		return OnPresetRegisteredDelegate;
	}

	virtual FOnPresetUnregistered& OnPresetUnregistered() override
	{
		return OnPresetUnregisteredDelegate;
	}

	/** Register the preset with the module, enabling using the preset remotely using its name. */
	bool RegisterPreset(FName Name, URemoteControlPreset* Preset)
	{
		check(Preset);
		if (CachedPresetsByName.Contains(Name))
		{
			return false;
		}

		CachedPresetsByName.Add(Name, Preset);
		CachedPresetNamesById.Add(Preset->GetPresetId(), Name);
		OnPresetRegistered().Broadcast(Name);
		return true;
	}

	/** Unregister the preset */
	void UnregisterPreset(FName Name)
	{
		OnPresetUnregistered().Broadcast(Name);
		if (FAssetData* PresetAsset = CachedPresetsByName.Find(Name))
		{
			FGuid PresetId = RemoteControlUtil::GetPresetId(*PresetAsset);
			if (PresetId.IsValid())
			{
				CachedPresetNamesById.Remove(PresetId);
			}
		}
		
		CachedPresetsByName.Remove(Name);
	}

	virtual bool ResolveCall(const FString& ObjectPath, const FString& FunctionName, FRCCallReference& OutCallRef, FString* OutErrorText) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			// Resolve the object
			UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (Object)
			{
				// Find the function to call
				UFunction* Function = RemoteControlUtil::FindFunctionByNameOrMetaDataName(Object, FunctionName);

				if (!Function)
				{
					ErrorText = FString::Printf(TEXT("Function: %s does not exist on object: %s"), *FunctionName, *ObjectPath);
					bSuccess = false;
				}
				else if (!Function->HasAllFunctionFlags(FUNC_BlueprintCallable | FUNC_Public) 
#if WITH_EDITOR
					|| Function->HasMetaData(RemoteControlUtil::NAME_DeprecatedFunction)
					|| Function->HasMetaData(RemoteControlUtil::NAME_ScriptNoExport)
#endif
					)
				{
					ErrorText = FString::Printf(TEXT("Function: %s is deprecated or unavailable remotely on object: %s"), *FunctionName, *ObjectPath);
					bSuccess = false;
				}
				else
				{
					OutCallRef.Object = Object;
					OutCallRef.Function = Function;
				}
			}
			else
			{
				ErrorText = FString::Printf(TEXT("Object: %s does not exist."), *ObjectPath);
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("Can't resolve object: %s while saving or garbage collecting."), *ObjectPath);
			bSuccess = false;
		}

		if (OutErrorText && !ErrorText.IsEmpty())
		{
			*OutErrorText = MoveTemp(ErrorText);
		}
		return bSuccess;
	}

	virtual bool InvokeCall(FRCCall& InCall) override
	{
		if (InCall.IsValid())
		{
#if WITH_EDITOR
			FScopedTransaction Transaction(LOCTEXT("RemoteCallTransaction", "Remote Call Transaction Wrap"), InCall.bGenerateTransaction);
#endif
			if (InCall.bGenerateTransaction)
			{
				InCall.CallRef.Object->Modify();
			}

			FEditorScriptExecutionGuard ScriptGuard;
			InCall.CallRef.Object->ProcessEvent(InCall.CallRef.Function.Get(), InCall.ParamStruct.GetStructMemory());
			return true;
		}
		return false;
	}

	virtual bool ResolveObject(ERCAccess AccessType, const FString& ObjectPath, const FString& PropertyName, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			// Resolve the object
			UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
			if (Object)
			{
				bSuccess = ResolveObjectProperty(AccessType, Object, PropertyName, OutObjectRef, OutErrorText);
			}
			else
			{
				ErrorText = FString::Printf(TEXT("Object: %s does not exist when trying to resolve property: %s"), *ObjectPath, *PropertyName);
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("Can't resolve object: %s while saving or garbage collecting."), *ObjectPath);
			bSuccess = false;
		}

		if (OutErrorText && !ErrorText.IsEmpty())
		{
			*OutErrorText = MoveTemp(ErrorText);
		}

		return bSuccess;
	}

	virtual bool ResolveObjectProperty(ERCAccess AccessType, UObject* Object, FRCFieldPathInfo PropertyPath, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			if (Object)
			{
				const bool bObjectInGame = !GIsEditor || IsRunningGame();

				if (PropertyPath.GetSegmentCount() != 0)
				{
					//Build a FieldPathInfo using property name to facilitate resolving
					if (PropertyPath.Resolve(Object))
					{
						FProperty* ResolvedProperty = PropertyPath.GetResolvedData().Field;
						if (RemoteControlUtil::IsPropertyAllowed(ResolvedProperty, AccessType, bObjectInGame))
						{
							OutObjectRef = FRCObjectReference{ AccessType , Object, MoveTemp(PropertyPath) };
						}
						else
						{
							ErrorText = FString::Printf(TEXT("Object property: %s is unavailable remotely on object: %s"), *PropertyPath.GetFieldName().ToString(), *Object->GetPathName());
							bSuccess = false;
						}
					}
					else
					{
						ErrorText = FString::Printf(TEXT("Object property: %s could not be resolved on object: %s"), *PropertyPath.GetFieldName().ToString(), *Object->GetPathName());
						bSuccess = false;
					}
				}
				else
				{
					OutObjectRef = FRCObjectReference{ AccessType , Object };
				}
			}
			else
			{
				ErrorText = FString::Printf(TEXT("Invalid object to resolve property '%s'"), *PropertyPath.GetFieldName().ToString());
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("Can't resolve object '%s' properties '%s' : %s while saving or garbage collecting."), *Object->GetPathName(), *PropertyPath.GetFieldName().ToString());
			bSuccess = false;
		}

		if (OutErrorText && !ErrorText.IsEmpty())
		{
			*OutErrorText = MoveTemp(ErrorText);
		}

		return bSuccess;
	}

	virtual bool GetObjectProperties(const FRCObjectReference& ObjectAccess, IStructSerializerBackend& Backend) override
	{
		if (ObjectAccess.IsValid() && ObjectAccess.Access == ERCAccess::READ_ACCESS)
		{
			bool bCanSerialize = true;
			UObject* Object = ObjectAccess.Object.Get();
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();

			FStructSerializerPolicies Policies;
			if (ObjectAccess.Property.IsValid())
			{
				if (ObjectAccess.PropertyPathInfo.IsResolved())
				{
					Policies.MapSerialization = EStructSerializerMapPolicies::Array;
					Policies.PropertyFilter = [&ObjectAccess](const FProperty* CurrentProp, const FProperty* ParentProp)
					{
						return CurrentProp == ObjectAccess.Property || ParentProp != nullptr;
					};
				}
				else
				{
					bCanSerialize = false;
				}
			}
			else
			{
				bool bObjectInGame = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				Policies.PropertyFilter = [&ObjectAccess, bObjectInGame](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return RemoteControlUtil::IsPropertyAllowed(CurrentProp, ObjectAccess.Access, bObjectInGame) || ParentProp != nullptr;
				};
			}

			if (bCanSerialize)
			{
				//Serialize the element if we're looking for a member or serialize the full object if not
				if (ObjectAccess.PropertyPathInfo.IsResolved())
				{
					const FRCFieldPathSegment& LastSegment = ObjectAccess.PropertyPathInfo.GetFieldSegment(ObjectAccess.PropertyPathInfo.GetSegmentCount() - 1);
					int32 Index = LastSegment.ArrayIndex != INDEX_NONE ? LastSegment.ArrayIndex : LastSegment.ResolvedData.MapIndex;

					FStructSerializer::SerializeElement(ObjectAccess.ContainerAdress, LastSegment.ResolvedData.Field, Index, Backend, Policies);
				}
				else
				{
					FStructSerializer::Serialize(ObjectAccess.ContainerAdress, *ObjectAccess.ContainerType, Backend, Policies);
				}
				return true;
			}
		}
		return false;
	}

	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend, ERCPayloadType InPayloadType, const TArray<uint8>& InPayload) override
	{
		// Check the replication path before apply property values
		if (InPayload.Num() != 0 && ObjectAccess.Object.IsValid())
		{
			// Build interception command
			FString PropertyPathString = TFieldPath<FProperty>(ObjectAccess.Property.Get()).ToString();
			FRCIPropertiesMetadata PropsMetadata(ObjectAccess.Object->GetPathName(), PropertyPathString, ObjectAccess.PropertyPathInfo.ToString(), ToExternal(ObjectAccess.Access), ToExternal(InPayloadType), InPayload);

			// Initialization
			IModularFeatures& ModularFeatures = IModularFeatures::Get();
			const FName InterceptorFeatureName = IRemoteControlInterceptionFeatureInterceptor::GetName();
			const int32 InterceptorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureInterceptor::GetName());

			// Pass interception command data to all available interceptors
			bool bShouldApply = false;
			for (int32 InterceptorIdx = 0; InterceptorIdx < InterceptorsAmount; ++InterceptorIdx)
			{
				IRemoteControlInterceptionFeatureInterceptor* const Interceptor = static_cast<IRemoteControlInterceptionFeatureInterceptor*>(ModularFeatures.GetModularFeatureImplementation(InterceptorFeatureName, InterceptorIdx));
				if (Interceptor)
				{
					// Update response flag
					bShouldApply |= (Interceptor->SetObjectProperties(PropsMetadata) == ERCIResponse::Apply);
				}
			}

			// Don't process the RC message if any of interceptors returned ERCIResponse::Intercept
			if (bShouldApply)
			{
				return true;
			}
		}

		//Setting object properties require a property and can't be done at the object level. Property must be valid to move forward
		if (ObjectAccess.IsValid()
			&& (ObjectAccess.Access == ERCAccess::WRITE_ACCESS || ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS)
			&& ObjectAccess.Property.IsValid()
			&& ObjectAccess.PropertyPathInfo.IsResolved())
		{
			UObject* Object = ObjectAccess.Object.Get();
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();

#if WITH_EDITOR
			bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
			FScopedTransaction Transaction(LOCTEXT("RemoteSetPropertyTransaction", "Remote Set Object Property"), bGenerateTransaction);

			if (bGenerateTransaction)
			{
				FEditPropertyChain PreEditChain;
				ObjectAccess.PropertyPathInfo.ToEditPropertyChain(PreEditChain);
				Object->PreEditChange(PreEditChain);
			}
#endif

			FStructDeserializerPolicies Policies;
			if (ObjectAccess.Property.IsValid())
			{
				Policies.PropertyFilter = [&ObjectAccess](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return CurrentProp == ObjectAccess.Property || ParentProp != nullptr;
				};
			}
			else
			{
				bool bObjectInGame = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				Policies.PropertyFilter = [&ObjectAccess, bObjectInGame](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return RemoteControlUtil::IsPropertyAllowed(CurrentProp, ObjectAccess.Access, bObjectInGame) || ParentProp != nullptr;
				};
			}

			//Serialize the element if we're looking for a member or serialize the full object if not
			bool bSuccess = false;
			if (ObjectAccess.PropertyPathInfo.IsResolved())
			{
				const FRCFieldPathSegment& LastSegment = ObjectAccess.PropertyPathInfo.GetFieldSegment(ObjectAccess.PropertyPathInfo.GetSegmentCount() - 1);
				int32 Index = LastSegment.ArrayIndex != INDEX_NONE ? LastSegment.ArrayIndex : LastSegment.ResolvedData.MapIndex;

				bSuccess = FStructDeserializer::DeserializeElement(ObjectAccess.ContainerAdress, *LastSegment.ResolvedData.Struct, Index, Backend, Policies);
			}
			else
			{
				bSuccess = FStructDeserializer::Deserialize(ObjectAccess.ContainerAdress, *ContainerType, Backend, Policies);
			}

			// if we are generating a transaction, also generate post edit property event, event if the change ended up unsuccessful
			// this is to match the pre edit change call that can unregister components for example
#if WITH_EDITOR
			if (bGenerateTransaction)
			{
				FPropertyChangedEvent PropertyEvent = ObjectAccess.PropertyPathInfo.ToPropertyChangedEvent();
				Object->PostEditChangeProperty(PropertyEvent);
			}
#endif
			return bSuccess;
		}
		return false;
	}

	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess, const bool bAllowIntercept) override
	{
		// Check the replication path before reset the property on this instance
		if (bAllowIntercept && ObjectAccess.Object.IsValid())
		{
			// Build interception command
			FString PropertyPathString = TFieldPath<FProperty>(ObjectAccess.Property.Get()).ToString();
			FRCIObjectMetadata ObjectMetadata(ObjectAccess.Object->GetPathName(), PropertyPathString, ObjectAccess.PropertyPathInfo.ToString(), ToExternal(ObjectAccess.Access));

			// Initialization
			IModularFeatures& ModularFeatures = IModularFeatures::Get();
			const FName InterceptorFeatureName = IRemoteControlInterceptionFeatureInterceptor::GetName();
			const int32 InterceptorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureInterceptor::GetName());

			// Pass interception command data to all available interceptors
			bool bShouldApply = false;
			for (int32 InterceptorIdx = 0; InterceptorIdx < InterceptorsAmount; ++InterceptorIdx)
			{
				IRemoteControlInterceptionFeatureInterceptor* const Interceptor = static_cast<IRemoteControlInterceptionFeatureInterceptor*>(ModularFeatures.GetModularFeatureImplementation(InterceptorFeatureName, InterceptorIdx));
				if (Interceptor)
				{
					// Update response flag
					bShouldApply |= (Interceptor->ResetObjectProperties(ObjectMetadata) == ERCIResponse::Apply);
				}
			}

			// Don't process the RC message if any of interceptors returned ERCIResponse::Intercept
			if (bShouldApply)
			{
				return true;
			}
		}


		if (ObjectAccess.IsValid()
			&& (ObjectAccess.Access == ERCAccess::WRITE_ACCESS || ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS))
		{
			UObject* Object = ObjectAccess.Object.Get();
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();

#if WITH_EDITOR
			bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
			FScopedTransaction Transaction(LOCTEXT("RemoteResetPropertyTransaction", "Remote Reset Object Property"), bGenerateTransaction);
			if (bGenerateTransaction)
			{
				FEditPropertyChain PreEditChain;
				ObjectAccess.PropertyPathInfo.ToEditPropertyChain(PreEditChain);
				Object->PreEditChange(PreEditChain);
			}
#endif

			// Copy the value from the field on the CDO.
			FRCFieldPathInfo FieldPathInfo = ObjectAccess.PropertyPathInfo;
			void* TargetAddress = FieldPathInfo.GetResolvedData().ContainerAddress;
			UObject* DefaultObject = Object->GetClass()->GetDefaultObject();
			FieldPathInfo.Resolve(DefaultObject);
			FRCFieldResolvedData DefaultObjectResolvedData = FieldPathInfo.GetResolvedData();
			ObjectAccess.Property->CopyCompleteValue_InContainer(TargetAddress, DefaultObjectResolvedData.ContainerAddress);

			// if we are generating a transaction, also generate post edit property event, event if the change ended up unsuccessful
			// this is to match the pre edit change call that can unregister components for example
#if WITH_EDITOR
			if (bGenerateTransaction)
			{
				FPropertyChangedEvent PropertyEvent(ObjectAccess.Property.Get());
				Object->PostEditChangeProperty(PropertyEvent);
			}
#endif
			return true;
		}
		return false;
	}

	virtual TOptional<FExposedFunction> ResolvePresetFunction(const FResolvePresetFieldArgs& Args) const override
	{
		TOptional<FExposedFunction> ExposedFunction;

		if (URemoteControlPreset* Preset = ResolvePreset(FName(*Args.PresetName)))
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ExposedFunction = Preset->ResolveExposedFunction(FName(*Args.FieldLabel));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		return ExposedFunction;
	}

	virtual TOptional<FExposedProperty> ResolvePresetProperty(const FResolvePresetFieldArgs& Args) const override
	{
		TOptional<FExposedProperty> ExposedProperty;

		if (URemoteControlPreset* Preset = ResolvePreset(FName(*Args.PresetName)))
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			ExposedProperty = Preset->ResolveExposedProperty(FName(*Args.FieldLabel));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		return ExposedProperty;
	}

	virtual URemoteControlPreset* ResolvePreset(FName PresetName) const override
	{
		if (const FAssetData* Asset = CachedPresetsByName.Find(PresetName))
		{
			return Cast<URemoteControlPreset>(Asset->GetAsset());
		}

		if (URemoteControlPreset* FoundPreset = RemoteControlUtil::GetPresetByName(PresetName))
		{
			CachedPresetsByName.Emplace(PresetName, FoundPreset);
			return FoundPreset;
		}
		
		return nullptr;
	}
	
	virtual URemoteControlPreset* ResolvePreset(const FGuid& PresetId) const override
	{
		if (const FName* AssetName = CachedPresetNamesById.Find(PresetId))
		{
			if (FAssetData* Asset = CachedPresetsByName.Find(*AssetName))
			{
				return Cast<URemoteControlPreset>(Asset->GetAsset());
			}
			else
			{
				ensureMsgf(false, TEXT("Preset id should be cached if the asset name already is."));
			}
	
		}

		if (URemoteControlPreset* FoundPreset = RemoteControlUtil::GetPresetById(PresetId))
		{
			CachedPresetNamesById.Emplace(PresetId, FoundPreset->GetName());
			return FoundPreset;
		}
		
		return nullptr;
	}

	virtual void GetPresets(TArray<TSoftObjectPtr<URemoteControlPreset>>& OutPresets) const override
	{
		OutPresets.Reserve(CachedPresetsByName.Num());
		Algo::Transform(CachedPresetsByName, OutPresets,[this](const TPair<FName, FAssetData>& Entry){ return Cast<URemoteControlPreset>(Entry.Value.GetAsset()); });
	}

	virtual void GetPresetAssets(TArray<FAssetData>& OutPresetAssets) const override
	{
		CachedPresetsByName.GenerateValueArray(OutPresetAssets);
	}

	virtual const TMap<FName, FEntityMetadataInitializer>& GetDefaultMetadataInitializers() const override
	{
		return DefaultMetadataInitializers;
	}
	
	virtual bool RegisterDefaultEntityMetadata(FName MetadataKey, FEntityMetadataInitializer MetadataInitializer) override
	{
		if (!DefaultMetadataInitializers.Contains(MetadataKey))
		{
			DefaultMetadataInitializers.FindOrAdd(MetadataKey) = MoveTemp(MetadataInitializer);
			return true;
		}
		return false;
	}

	virtual void UnregisterDefaultEntityMetadata(FName MetadataKey) override
	{
		DefaultMetadataInitializers.Remove(MetadataKey);
	}

private:
	void CachePresets()
	{
		TArray<FAssetData> Assets;
		RemoteControlUtil::GetAllPresetAssets(Assets);
			
		for (const FAssetData& AssetData : Assets)
		{
			CachedPresetsByName.Add(AssetData.AssetName, AssetData);
			
			FGuid PresetId = RemoteControlUtil::GetPresetId(AssetData);
			if (PresetId.IsValid())
			{
				CachedPresetNamesById.Add(MoveTemp(PresetId), AssetData.AssetName);
			}
		}
	}

private:
	
	/** Cache of preset names to preset assets */
	mutable TMap<FName, FAssetData> CachedPresetsByName;

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
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);

