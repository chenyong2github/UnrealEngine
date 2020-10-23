// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRemoteControlModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/CoreMisc.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"

#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "IStructSerializerBackend.h"
#include "RemoteControlPreset.h"

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
}

/**
 * Implementation of the RemoteControl interface
 */
class FRemoteControlModule : public IRemoteControlModule
{
public:

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
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
		if (RegisteredPresetMap.Contains(Name))
		{
			return false;
		}

		RegisteredPresetMap.Add(Name, Preset);
		OnPresetRegistered().Broadcast(Name);
		return true;
	}

	/** Unregister the preset */
	void UnregisterPreset(FName Name)
	{
		OnPresetUnregistered().Broadcast(Name);
		RegisteredPresetMap.Remove(Name);
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

	virtual bool ResolveObjectProperty(ERCAccess AccessType, UObject* Object, const FString& PropertyName, FRCObjectReference& OutObjectRef, FString* OutErrorText = nullptr) override
	{
		bool bSuccess = true;
		FString ErrorText;
		if (!GIsSavingPackage && !IsGarbageCollecting())
		{
			if (Object)
			{
				const bool bObjectInGame = !GIsEditor || IsRunningGame();

				if (!PropertyName.IsEmpty())
				{
					//Build a FieldPathInfo using property name to facilitate resolving
					FRCFieldPathInfo PathInfo(PropertyName);
					if (PathInfo.Resolve(Object))
					{
						FProperty* ResolvedProperty = PathInfo.GetResolvedData().Field;
						if (RemoteControlUtil::IsPropertyAllowed(ResolvedProperty, AccessType, bObjectInGame))
						{
							OutObjectRef.Object = Object;
							OutObjectRef.Property = ResolvedProperty;
							OutObjectRef.Access = AccessType;
							OutObjectRef.ContainerAdress = PathInfo.GetResolvedData().ContainerAddress;
							OutObjectRef.ContainerType = PathInfo.GetResolvedData().Struct;
							OutObjectRef.PropertyPathInfo = MoveTemp(PathInfo);
						}
						else
						{
							ErrorText = FString::Printf(TEXT("Object property: %s is unavailable remotely on object: %s"), *PropertyName, *Object->GetPathName());
							bSuccess = false;
						}
					}
					else
					{
						ErrorText = FString::Printf(TEXT("Object property: %s could not be resolved on object: %s"), *PropertyName, *Object->GetPathName());
						bSuccess = false;
					}
				}
				else
				{
					OutObjectRef.Object = Object;
					OutObjectRef.Access = AccessType;
					OutObjectRef.ContainerAdress = static_cast<void*>(Object);
					OutObjectRef.ContainerType = Object->GetClass();
				}
			}
			else
			{
				ErrorText = FString::Printf(TEXT("Invalid object to resolve property '%s'"), *PropertyName);
				bSuccess = false;
			}
		}
		else
		{
			ErrorText = FString::Printf(TEXT("Can't resolve object '%s' properties '%s' : %s while saving or garbage collecting."), *Object->GetPathName(), *PropertyName);
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
			Policies.MapSerialization = EStructSerializerMapPolicies::Array;
			if (ObjectAccess.Property.IsValid())
			{
				if (ObjectAccess.PropertyPathInfo.IsResolved())
				{
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
					FStructSerializer::SerializeElement(ObjectAccess.ContainerAdress, ObjectAccess.PropertyPathInfo.GetResolvedData().Field, ObjectAccess.PropertyPathInfo.GetFieldSegment(ObjectAccess.PropertyPathInfo.GetSegmentCount()-1).ArrayIndex, Backend, Policies);
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

	virtual bool SetObjectProperties(const FRCObjectReference& ObjectAccess, IStructDeserializerBackend& Backend) override
	{
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
				Object->Modify();

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
				bSuccess = FStructDeserializer::DeserializeElement(ObjectAccess.ContainerAdress, *ObjectAccess.PropertyPathInfo.GetResolvedData().Struct, ObjectAccess.PropertyPathInfo.GetFieldSegment(ObjectAccess.PropertyPathInfo.GetSegmentCount() - 1).ArrayIndex, Backend, Policies);
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

	virtual bool ResetObjectProperties(const FRCObjectReference& ObjectAccess) override
	{
		if (ObjectAccess.IsValid() && (ObjectAccess.Access == ERCAccess::WRITE_ACCESS || ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS))
		{
			UObject* Object = ObjectAccess.Object.Get();
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();

#if WITH_EDITOR
			bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
			FScopedTransaction Transaction(LOCTEXT("RemoteResetPropertyTransaction", "Remote Reset Object Property"), bGenerateTransaction);
			if (bGenerateTransaction)
			{
				Object->Modify();
				Object->PreEditChange(ObjectAccess.Property.Get());
			}
#endif
					
			ObjectAccess.Property->InitializeValue(ObjectAccess.Property->template ContainerPtrToValuePtr<void>(ObjectAccess.ContainerAdress));

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
			ExposedFunction = Preset->ResolveExposedFunction(FName(*Args.FieldLabel));
		}

		return ExposedFunction;
	}

	virtual TOptional<FExposedProperty> ResolvePresetProperty(const FResolvePresetFieldArgs& Args) const override
	{
		TOptional<FExposedProperty> ExposedProperty;

		if (URemoteControlPreset* Preset = ResolvePreset(FName(*Args.PresetName)))
		{
			ExposedProperty = Preset->ResolveExposedProperty(FName(*Args.FieldLabel));
		}

		return ExposedProperty;
	}

	virtual URemoteControlPreset* ResolvePreset(FName PresetName) const override
	{
		if (const TSoftObjectPtr<URemoteControlPreset>* Preset = RegisteredPresetMap.Find(PresetName))
		{
			return Preset->LoadSynchronous();
		}

		return nullptr;
	}

	virtual void GetPresets(TArray<TSoftObjectPtr<URemoteControlPreset>>& OutPresets) override
	{
		if (!bHasDoneInitialPresetSearch)
		{
			bHasDoneInitialPresetSearch = true;
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
			FARFilter Filter;
			Filter.bIncludeOnlyOnDiskAssets = true;
			Filter.ClassNames = { URemoteControlPreset::StaticClass()->GetFName() };
			Filter.bRecursivePaths = true;
			Filter.PackagePaths = { TEXT("/Game") };
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssets(Filter, Assets);

			for (const FAssetData& AssetData : Assets)
			{
				RegisteredPresetMap.Emplace(AssetData.AssetName, AssetData.ToSoftObjectPath());
			}
		}

		return RegisteredPresetMap.GenerateValueArray(OutPresets);
	}

private:
	
	/** Map of all preset assets */
	TMap<FName, TSoftObjectPtr<URemoteControlPreset>> RegisteredPresetMap;

	/** Delegate for preset registration */
	FOnPresetRegistered OnPresetRegisteredDelegate;

	/** Delegate for preset unregistration */
	FOnPresetUnregistered OnPresetUnregisteredDelegate;

	/** Whether the module has already scanned for existing preset assets. */
	bool bHasDoneInitialPresetSearch = false;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);

