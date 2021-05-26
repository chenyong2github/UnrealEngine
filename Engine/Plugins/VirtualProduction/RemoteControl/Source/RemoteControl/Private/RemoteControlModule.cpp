// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRemoteControlModule.h"
#include "IRemoteControlInterceptionFeature.h"
#include "IStructDeserializerBackend.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Backends/CborStructSerializerBackend.h"
#include "Components/StaticMeshComponent.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreMisc.h"
#include "IStructSerializerBackend.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlInterceptionHelpers.h"
#include "RemoteControlInterceptionProcessor.h"
#include "RemoteControlPreset.h"
#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "Misc/ScopeExit.h"
#include "UObject/Class.h"
#include "UObject/FieldPath.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY(LogRemoteControl);
#define LOCTEXT_NAMESPACE "RemoteControl"

struct FRCInterceptionPayload
{
	TArray<uint8> Payload;
	ERCPayloadType Type;
};

namespace RemoteControlUtil
{
	const FName NAME_DisplayName(TEXT("DisplayName"));
	const FName NAME_ScriptName(TEXT("ScriptName"));
	const FName NAME_ScriptNoExport(TEXT("ScriptNoExport"));
	const FName NAME_DeprecatedFunction(TEXT("DeprecatedFunction"));
	const FName NAME_BlueprintGetter(TEXT("BlueprintGetter"));
	const FName NAME_BlueprintSetter(TEXT("BlueprintSetter"));
	const FName NAME_AllowPrivateAccess(TEXT("AllowPrivateAccess"));

	TMap<TWeakFieldPtr<FProperty>, TWeakObjectPtr<UFunction>> CachedSetterFunctions;

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
				InAccessType == ERCAccess::READ_ACCESS || (InProperty->HasAnyPropertyFlags(CPF_Edit) && !InProperty->HasAnyPropertyFlags(CPF_EditConst)));
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

	URemoteControlPreset* FindPresetByName(FName PresetName)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(URemoteControlPreset::StaticClass()->GetFName(), Assets);

		FAssetData* FoundAsset = Assets.FindByPredicate([&PresetName](const FAssetData& InAsset)
		{
			return InAsset.AssetName == PresetName;
		});

		return FoundAsset ? Cast<URemoteControlPreset>(FoundAsset->GetAsset()) : nullptr;
	}

	URemoteControlPreset* GetPresetByName(FName PresetName)
	{
		FARFilter Filter = GetBasePresetFilter();
		Filter.PackageNames = { PresetName };
		URemoteControlPreset* FoundPreset = GetFirstPreset(Filter);
		return FoundPreset ? FoundPreset : FindPresetByName(PresetName);
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

	/** Returns whether the access is a write access regardless of if it generates a transaction. */
	bool IsWriteAccess(ERCAccess Access)
	{
		return Access == ERCAccess::WRITE_ACCESS || Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
	}
}

namespace RemoteControlSetterUtils
{
	static TMap<TWeakFieldPtr<FProperty>, TWeakObjectPtr<UFunction>> CachedSetterFunctions;

	struct FConvertToFunctionCallArgs
	{
		FConvertToFunctionCallArgs(const FRCObjectReference& InObjectReference, IStructDeserializerBackend& InReaderBackend, FRCCall& OutCall)
			: ObjectReference(InObjectReference)
			, ReaderBackend(InReaderBackend)
			, Call(OutCall) 
		{
		}
		
		const FRCObjectReference& ObjectReference;
		IStructDeserializerBackend& ReaderBackend;
		FRCCall& Call;
	};
	
	UFunction* FindSetterFunction(FProperty* Property)
	{
		// UStruct properties cannot have setters.
		if (!ensure(Property) || !Property->GetOwnerClass())
		{
			return nullptr;
		}

		// Check if the property setter is already cached.
		TWeakObjectPtr<UFunction> SetterPtr = CachedSetterFunctions.FindRef(Property);
		if (SetterPtr.IsValid())
		{
			return SetterPtr.Get();
		}
		
		UFunction* SetterFunction = nullptr;
#if WITH_EDITOR
		const FString& SetterName =  Property->GetMetaData(*RemoteControlUtil::NAME_BlueprintSetter.ToString());
		if (!SetterName.IsEmpty())
		{
			SetterFunction = Property->GetOwnerClass()->FindFunctionByName(*SetterName);
		}
#endif

		FString PropertyName = Property->GetName();
		if (Property->IsA<FBoolProperty>()) 
		{
			PropertyName.RemoveFromStart("b", ESearchCase::CaseSensitive);
		}

		static const TArray<FString> SetterPrefixes = {
			FString("Set"),
			FString("K2_Set")
		};

		for (const FString& Prefix : SetterPrefixes)
		{
			FName SetterFunctionName = FName(Prefix + PropertyName);
			SetterFunction = Property->GetOwnerClass()->FindFunctionByName(SetterFunctionName);
			if (SetterFunction)
			{
				break;
			}
		}

		if (SetterFunction)
		{
			CachedSetterFunctions.Add(Property,  SetterFunction);
		}

		return SetterFunction;
	}
	
	FProperty* FindSetterArgument(UFunction* SetterFunction, FProperty* PropertyToModify)
	{
		FProperty* SetterArgument = nullptr;

		if (!ensure(SetterFunction))
		{
			return nullptr;
		}

		// Check if the first parameter for the setter function matches the parameter value.
		for (TFieldIterator<FProperty> PropertyIt(SetterFunction); PropertyIt; ++PropertyIt)
		{
			if (PropertyIt->HasAnyPropertyFlags(CPF_Parm) && !PropertyIt->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				if (PropertyIt->SameType(PropertyToModify))
				{
					SetterArgument = *PropertyIt;
				}
				
				break;
			}
		}

		return SetterArgument;
	}

	void CreateRCCall(FConvertToFunctionCallArgs& InOutArgs, UFunction* InFunction, FStructOnScope&& InFunctionArguments, FRCInterceptionPayload& OutPayload)
	{
		ensure(InFunctionArguments.GetStruct() && InFunctionArguments.GetStruct()->IsA<UFunction>());
		
		// Create the output payload for interception purposes.
		FMemoryWriter Writer{OutPayload.Payload};
		FCborStructSerializerBackend WriterBackend{Writer, EStructSerializerBackendFlags::Default};
		FStructSerializer::Serialize(InFunctionArguments.GetStructMemory(), *const_cast<UStruct*>(InFunctionArguments.GetStruct()), WriterBackend, FStructSerializerPolicies());
		OutPayload.Type = ERCPayloadType::Cbor;

		InOutArgs.Call.bGenerateTransaction = InOutArgs.ObjectReference.Access == ERCAccess::WRITE_TRANSACTION_ACCESS ? true : false;
		InOutArgs.Call.CallRef.Function = InFunction;
		InOutArgs.Call.CallRef.Object = InOutArgs.ObjectReference.Object;
		InOutArgs.Call.ParamStruct = MoveTemp(InFunctionArguments);
	}

	void CreateRCCall(FConvertToFunctionCallArgs& InOutArgs, UFunction* InFunction, FStructOnScope&& InFunctionArguments)
	{
		ensure(InFunctionArguments.GetStruct() && InFunctionArguments.GetStruct()->IsA<UFunction>());
		
		InOutArgs.Call.bGenerateTransaction = InOutArgs.ObjectReference.Access == ERCAccess::WRITE_TRANSACTION_ACCESS ? true : false;
		InOutArgs.Call.CallRef.Function = InFunction;
		InOutArgs.Call.CallRef.Object = InOutArgs.ObjectReference.Object;
		InOutArgs.Call.ParamStruct = MoveTemp(InFunctionArguments);
	}

	/** Create the payload to pass to be passed to a property's setter function. */
	TOptional<FStructOnScope> CreateSetterFunctionPayload(UFunction* InSetterFunction, FConvertToFunctionCallArgs& InOutArgs)
	{
		TOptional<FStructOnScope> OptionalArgsOnScope;

		bool bSuccess = false;
		if (FProperty* SetterArgument = FindSetterArgument(InSetterFunction, InOutArgs.ObjectReference.Property.Get()))
		{
			FStructOnScope ArgsOnScope{InSetterFunction};
			
			// First put the complete property value from the object in the struct on scope
			// in case the user only a part of the incoming structure (ie. Providing only { "x": 2 } in the case of a vector.
			const uint8* ContainerAddress = InOutArgs.ObjectReference.Property->ContainerPtrToValuePtr<uint8>(InOutArgs.ObjectReference.ContainerAdress);
			InOutArgs.ObjectReference.Property->CopyCompleteValue(ArgsOnScope.GetStructMemory(), ContainerAddress);

			// Temporarily rename the setter argument in order to deserialize the incoming property modification on top of it
			// regardless of the argument name.
			FName OldSetterArgumentName = SetterArgument->GetFName();
			SetterArgument->Rename(InOutArgs.ObjectReference.Property->GetFName());
			{
				ON_SCOPE_EXIT
				{
					SetterArgument->Rename(OldSetterArgumentName);
				};

				// Then deserialize the input value on top of it and reset the setter property name.
				bSuccess = FStructDeserializer::Deserialize((void*)ArgsOnScope.GetStructMemory(), *const_cast<UStruct*>(ArgsOnScope.GetStruct()), InOutArgs.ReaderBackend, FStructDeserializerPolicies());
			}
			
			if (bSuccess)
			{
				OptionalArgsOnScope = MoveTemp(ArgsOnScope);
			}
		}
		
		return OptionalArgsOnScope;
	}
	
	bool ConvertModificationToFunctionCall(FConvertToFunctionCallArgs& InOutArgs, UFunction* InSetterFunction, FRCInterceptionPayload& OutPayload)
	{
		if (TOptional<FStructOnScope> ArgsOnScope = CreateSetterFunctionPayload(InSetterFunction, InOutArgs))
		{
			CreateRCCall(InOutArgs, InSetterFunction, MoveTemp(*ArgsOnScope), OutPayload);
			return true;
		}
		
		return false;
	}

	bool ConvertModificationToFunctionCall(FConvertToFunctionCallArgs& InOutArgs, UFunction* InSetterFunction)
	{
		if (TOptional<FStructOnScope> ArgsOnScope = CreateSetterFunctionPayload(InSetterFunction, InOutArgs))
		{
			CreateRCCall(InOutArgs, InSetterFunction, MoveTemp(*ArgsOnScope));
			return true;
		}
		
		return false;
	}

	bool ConvertModificationToFunctionCall(FConvertToFunctionCallArgs& InOutArgs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoteControlSetterUtils::ConvertModificationToFunctionCall);
		if (!InOutArgs.ObjectReference.Object.IsValid() || !InOutArgs.ObjectReference.Property.IsValid())
		{
			return false;
		}
		
		if (UFunction* SetterFunction = FindSetterFunction(InOutArgs.ObjectReference.Property.Get()))
		{
			return ConvertModificationToFunctionCall(InOutArgs, SetterFunction);
		}

		return false;
	}

	bool ConvertModificationToFunctionCall(FConvertToFunctionCallArgs& InArgs, FRCInterceptionPayload& OutPayload)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoteControlSetterUtils::ConvertModificationToFunctionCall);
		if (!InArgs.ObjectReference.Object.IsValid() || !InArgs.ObjectReference.Property.IsValid())
		{
			return false;
		}
		
		if (UFunction* SetterFunction = FindSetterFunction(InArgs.ObjectReference.Property.Get()))
		{
			return ConvertModificationToFunctionCall(InArgs, SetterFunction, OutPayload);
		}

		return false;
	}
}

/**
 * Implementation of the RemoteControl interface
 */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
class FRemoteControlModule : public IRemoteControlModule
{
public:
	virtual void StartupModule() override
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		
		AssetRegistry.OnAssetAdded().AddRaw(this, &FRemoteControlModule::OnAssetAdded);
		AssetRegistry.OnAssetRemoved().AddRaw(this, &FRemoteControlModule::OnAssetRemoved);
		AssetRegistry.OnAssetRenamed().AddRaw(this, &FRemoteControlModule::OnAssetRenamed);
		
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
			AssetRegistry.OnAssetRenamed().RemoveAll(this);
			AssetRegistry.OnAssetAdded().RemoveAll(this);
			AssetRegistry.OnAssetRemoved().RemoveAll(this);
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
	virtual bool RegisterPreset(FName Name, URemoteControlPreset* Preset) override
	{
		return false;
	}

	/** Unregister the preset */
	virtual void UnregisterPreset(FName Name) override
	{
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

	virtual bool InvokeCall(FRCCall& InCall, ERCPayloadType InPayloadType = ERCPayloadType::Json, const TArray<uint8>& InInterceptPayload = TArray<uint8>()) override
	{
		if (InCall.IsValid())
		{
			// Check the replication path before apply property values
			if (InInterceptPayload.Num() != 0)
			{
				FRCIFunctionMetadata FunctionMetadata(InCall.CallRef.Object->GetPathName(), InCall.CallRef.Function->GetPathName(), InCall.bGenerateTransaction, ToExternal(InPayloadType), InInterceptPayload);

				// Initialization
				IModularFeatures& ModularFeatures = IModularFeatures::Get();
				const FName InterceptorFeatureName = IRemoteControlInterceptionFeatureInterceptor::GetName();
				const int32 InterceptorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureInterceptor::GetName());

				// Pass interception command data to all available interceptors
				bool bShouldIntercept = false;
				for (int32 InterceptorIdx = 0; InterceptorIdx < InterceptorsAmount; ++InterceptorIdx)
				{
					IRemoteControlInterceptionFeatureInterceptor* const Interceptor = static_cast<IRemoteControlInterceptionFeatureInterceptor*>(ModularFeatures.GetModularFeatureImplementation(InterceptorFeatureName, InterceptorIdx));
					if (Interceptor)
					{
						// Update response flag
						bShouldIntercept |= (Interceptor->InvokeCall(FunctionMetadata) == ERCIResponse::Intercept);
					}
				}

				// Don't process the RC message if any of interceptors returned ERCIResponse::Intercept
				if (bShouldIntercept)
				{
					return true;
				}
			}
			
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

						// When resolving a property for writing, resolve successfully if it should use a setter since it will end up using it. 
						if ((RemoteControlUtil::IsWriteAccess(AccessType) && PropertyModificationShouldUseSetter(Object, ResolvedProperty))
							|| RemoteControlUtil::IsPropertyAllowed(ResolvedProperty, AccessType, bObjectInGame))
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
		// Check the replication path before applying property values
		if (InPayload.Num() != 0 && ObjectAccess.Object.IsValid())
		{
			// Convert raw property modifications to setter function calls if necessary.
			if (PropertyModificationShouldUseSetter(ObjectAccess.Object.Get(), ObjectAccess.Property.Get()))
			{
				FRCCall	Call;
				FRCInterceptionPayload InterceptionPayload;
				constexpr bool bCreateInterceptionPayload = true;
				RemoteControlSetterUtils::FConvertToFunctionCallArgs Args(ObjectAccess, Backend, Call);
				if (RemoteControlSetterUtils::ConvertModificationToFunctionCall(Args, InterceptionPayload))
				{
					return InvokeCall(Call, InterceptionPayload.Type, InterceptionPayload.Payload);
				}
			}
			
			// If a setter wasn't used, verify if the property should be allowed.
			bool bObjectInGame = !GIsEditor || (ObjectAccess.Object.IsValid() && ObjectAccess.Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor));
			if (!RemoteControlUtil::IsPropertyAllowed(ObjectAccess.Property.Get(), ObjectAccess.Access, bObjectInGame))
			{
				return false;
			}

			// Build interception command
			FString PropertyPathString = TFieldPath<FProperty>(ObjectAccess.Property.Get()).ToString();
			FRCIPropertiesMetadata PropsMetadata(ObjectAccess.Object->GetPathName(), PropertyPathString, ObjectAccess.PropertyPathInfo.ToString(), ToExternal(ObjectAccess.Access), ToExternal(InPayloadType), InPayload);

			// Initialization
			IModularFeatures& ModularFeatures = IModularFeatures::Get();
			const FName InterceptorFeatureName = IRemoteControlInterceptionFeatureInterceptor::GetName();
			const int32 InterceptorsAmount = ModularFeatures.GetModularFeatureImplementationCount(IRemoteControlInterceptionFeatureInterceptor::GetName());

			// Pass interception command data to all available interceptors
			bool bShouldIntercept = false;
			for (int32 InterceptorIdx = 0; InterceptorIdx < InterceptorsAmount; ++InterceptorIdx)
			{
				IRemoteControlInterceptionFeatureInterceptor* const Interceptor = static_cast<IRemoteControlInterceptionFeatureInterceptor*>(ModularFeatures.GetModularFeatureImplementation(InterceptorFeatureName, InterceptorIdx));
				if (Interceptor)
				{
					// Update response flag
					bShouldIntercept |= (Interceptor->SetObjectProperties(PropsMetadata) == ERCIResponse::Intercept);
				}
			}

			// Don't process the RC message if any of interceptors returned ERCIResponse::Intercept
			if (bShouldIntercept)
			{
				return true;
			}
		}

		// Convert raw property modifications to setter function calls if necessary.
		if (PropertyModificationShouldUseSetter(ObjectAccess.Object.Get(), ObjectAccess.Property.Get()))
		{
			FRCCall	Call;
			RemoteControlSetterUtils::FConvertToFunctionCallArgs Args(ObjectAccess, Backend, Call);
			if (RemoteControlSetterUtils::ConvertModificationToFunctionCall(Args))
			{
				return InvokeCall(Call);
			}
		}

		//Setting object properties require a property and can't be done at the object level. Property must be valid to move forward
		if (ObjectAccess.IsValid()
			&& (RemoteControlUtil::IsWriteAccess(ObjectAccess.Access))
			&& ObjectAccess.Property.IsValid()
			&& ObjectAccess.PropertyPathInfo.IsResolved())
		{
			UObject* Object = ObjectAccess.Object.Get();
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();

#if WITH_EDITOR
			const bool bGenerateTransaction = ObjectAccess.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
			if (GEditor && bGenerateTransaction)
			{
				GEditor->BeginTransaction(LOCTEXT("RemoteSetPropertyTransaction", "Remote Set Object Property"));
			}

			FEditPropertyChain PreEditChain;
			ObjectAccess.PropertyPathInfo.ToEditPropertyChain(PreEditChain);
			Object->PreEditChange(PreEditChain);
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

			// Generate post edit property event independently from a transaction
#if WITH_EDITOR
			if (GEditor && bGenerateTransaction)
			{
				GEditor->EndTransaction();
			}
			
			FPropertyChangedEvent PropertyEvent = ObjectAccess.PropertyPathInfo.ToPropertyChangedEvent();
			Object->PostEditChangeProperty(PropertyEvent);
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
			bool bShouldIntercept = false;
			for (int32 InterceptorIdx = 0; InterceptorIdx < InterceptorsAmount; ++InterceptorIdx)
			{
				IRemoteControlInterceptionFeatureInterceptor* const Interceptor = static_cast<IRemoteControlInterceptionFeatureInterceptor*>(ModularFeatures.GetModularFeatureImplementation(InterceptorFeatureName, InterceptorIdx));
				if (Interceptor)
				{
					// Update response flag
					bShouldIntercept |= (Interceptor->ResetObjectProperties(ObjectMetadata) == ERCIResponse::Intercept);
				}
			}

			// Don't process the RC message if any of interceptors returned ERCIResponse::Intercept
			if (bShouldIntercept)
			{
				return true;
			}
		}

		if (ObjectAccess.IsValid() && RemoteControlUtil::IsWriteAccess(ObjectAccess.Access))
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
		if (const TArray<FAssetData>* Assets = CachedPresetsByName.Find(PresetName))
		{
			for (const FAssetData& Asset : *Assets)
			{
				if (Asset.AssetName == PresetName)
				{
					return Cast<URemoteControlPreset>(Asset.GetAsset());
				}
			}
		}

		if (URemoteControlPreset* FoundPreset = RemoteControlUtil::GetPresetByName(PresetName))
		{
			CachedPresetsByName.FindOrAdd(PresetName).AddUnique(FoundPreset);
			return FoundPreset;
		}
		
		return nullptr;
	}
	
	virtual URemoteControlPreset* ResolvePreset(const FGuid& PresetId) const override
	{
		if (const FName* AssetName = CachedPresetNamesById.Find(PresetId))
		{
			if (const TArray<FAssetData>* Assets = CachedPresetsByName.Find(*AssetName))
			{
				for (const FAssetData& Asset : *Assets)
				{
					if (RemoteControlUtil::GetPresetId(Asset) == PresetId)
					{
						return Cast<URemoteControlPreset>(Asset.GetAsset());
					}
				}
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
		for (const TPair<FName, TArray<FAssetData>>& Entry : CachedPresetsByName)
		{
			Algo::Transform(Entry.Value, OutPresets, [this](const FAssetData& AssetData){ return Cast<URemoteControlPreset>( AssetData.GetAsset()); });
		}
	}

	virtual void GetPresetAssets(TArray<FAssetData>& OutPresetAssets) const override
	{
		OutPresetAssets.Reserve(CachedPresetsByName.Num());
		for (const TPair<FName, TArray<FAssetData>>& Entry : CachedPresetsByName)
		{
			OutPresetAssets.Append(Entry.Value);
		}
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
			CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);
			
			const FGuid PresetId = RemoteControlUtil::GetPresetId(AssetData);
			if (PresetId.IsValid())
			{
				CachedPresetNamesById.Add(PresetId, AssetData.AssetName);
			}
		}
	}
	
	void OnAssetAdded(const FAssetData& AssetData)
	{
		if (AssetData.AssetClass != URemoteControlPreset::StaticClass()->GetFName())
		{
			return;
		}
	
		const FGuid PresetId = RemoteControlUtil::GetPresetId(AssetData);
		CachedPresetNamesById.Add(PresetId, AssetData.AssetName);
		CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);
	}
	
	void OnAssetRemoved(const FAssetData& AssetData)
	{
		if (AssetData.AssetClass != URemoteControlPreset::StaticClass()->GetFName())
		{
			return;
		}
	
		const FGuid PresetId = RemoteControlUtil::GetPresetId(AssetData);
		if (FName* PresetName = CachedPresetNamesById.Find(PresetId))
		{
			if (TArray<FAssetData>* Assets = CachedPresetsByName.Find(*PresetName))
			{
				for (auto It = Assets->CreateIterator(); It; ++It)
				{
					if (RemoteControlUtil::GetPresetId(*It) == PresetId)
					{
						It.RemoveCurrent();
						break;
					}
				}
			}
		}
	}
	
	void OnAssetRenamed(const FAssetData& AssetData, const FString&)
	{
		if (AssetData.AssetClass != URemoteControlPreset::StaticClass()->GetFName())
		{
			return;
		}

		const FGuid PresetId = RemoteControlUtil::GetPresetId(AssetData);
		if (FName* OldPresetName = CachedPresetNamesById.Find(PresetId))
		{
			if (TArray<FAssetData>* Assets = CachedPresetsByName.Find(*OldPresetName))
			{
				for (auto It = Assets->CreateIterator(); It; ++It)
				{
					if (RemoteControlUtil::GetPresetId(*It) == PresetId)
					{
						It.RemoveCurrent();
						break;
					}
				}

				if (Assets->Num() == 0)
				{
					CachedPresetsByName.Remove(*OldPresetName);
				}
			}
		}
		
		CachedPresetNamesById.Remove(PresetId);
		CachedPresetNamesById.Add(PresetId, AssetData.AssetName);
		
		CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);
	}
	
	bool PropertyModificationShouldUseSetter(UObject* Object, FProperty* Property)
	{
		if (!Property || !Object)
		{
			return false;
		}
		
		const bool bObjectInGamePackage = !GIsEditor || Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
		if (!RemoteControlUtil::IsPropertyAllowed(Property, ERCAccess::WRITE_ACCESS, bObjectInGamePackage))
		{
			return !!RemoteControlSetterUtils::FindSetterFunction(Property);
		}

		return false;
	}

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
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);

