// Copyright Epic Games, Inc. All Rights Reserved.

#include "IRemoteControlInterceptionFeature.h"
#include "IRemoteControlModule.h"
#include "IStructDeserializerBackend.h"
#include "IStructSerializerBackend.h"
#include "Components/ActorComponent.h"
#include "Components/LightComponent.h"
#include "RCPropertyUtilities.h"
#include "RemoteControlFieldPath.h"
#include "RemoteControlInterceptionHelpers.h"
#include "RemoteControlInterceptionProcessor.h"
#include "RemoteControlPreset.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Backends/CborStructSerializerBackend.h"
#include "Features/IModularFeatures.h"
#include "Misc/CoreMisc.h"
#include "Misc/ScopeExit.h"
#include "Misc/TVariant.h"
#include "UObject/Class.h"
#include "UObject/FieldPath.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "HAL/IConsoleManager.h"
#include "ScopedTransaction.h"
#endif

DEFINE_LOG_CATEGORY(LogRemoteControl);
#define LOCTEXT_NAMESPACE "RemoteControl"

struct FRCInterceptionPayload
{
	TArray<uint8> Payload;
	ERCPayloadType Type;
};

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarRemoteControlEnableOngoingChangeOptimization(TEXT("RemoteControl.EnableOngoingChangeOptimization"), 1, TEXT("Enable an optimization that keeps track of the ongoing remote control change in order to improve performance."));
#endif

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
				(bObjectInGamePackage
					? InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible) && (InAccessType == ERCAccess::READ_ACCESS || !InProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
					: InAccessType == ERCAccess::READ_ACCESS || (InProperty->HasAnyPropertyFlags(CPF_Edit) && !InProperty->HasAnyPropertyFlags(CPF_EditConst)));
	};

	FARFilter GetBasePresetFilter()
	{
		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = false;
		Filter.ClassNames = {URemoteControlPreset::StaticClass()->GetFName()};
		Filter.bRecursivePaths = true;
		Filter.PackagePaths = {TEXT("/")};

		return Filter;
	}

	void GetAllPresetAssets(TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.GetAssets(GetBasePresetFilter(), OutAssets);
	}

	URemoteControlPreset* GetFirstPreset(const FARFilter& Filter)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::GetFirstPreset);
		IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		return Assets.Num() ? CastChecked<URemoteControlPreset>(Assets[0].GetAsset()) : nullptr;
	}

	URemoteControlPreset* GetPresetById(const FGuid& Id)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::GetPresetId);
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
		Filter.PackageNames = {PresetName};
		URemoteControlPreset* FoundPreset = GetFirstPreset(Filter);
		return FoundPreset ? FoundPreset : FindPresetByName(PresetName);
	}

	FGuid GetPresetId(const FAssetData& PresetAsset)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::GetPresetId);
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
	struct FConvertToFunctionCallArgs
	{
		FConvertToFunctionCallArgs(const FRCObjectReference& InObjectReference, IStructDeserializerBackend& InReaderBackend, FRCCall& OutCall)
			: ObjectReference(InObjectReference)
			, ReaderBackend(InReaderBackend)
			, Call(OutCall)
		{ }

		const FRCObjectReference& ObjectReference;
		IStructDeserializerBackend& ReaderBackend;
		FRCCall& Call;
	};

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
		if (FProperty* SetterArgument = RemoteControlPropertyUtilities::FindSetterArgument(InSetterFunction, InOutArgs.ObjectReference.Property.Get()))
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

		if (UFunction* SetterFunction = RemoteControlPropertyUtilities::FindSetterFunction(InOutArgs.ObjectReference.Property.Get(), InOutArgs.ObjectReference.Object->GetClass()))
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

		if (UFunction* SetterFunction = RemoteControlPropertyUtilities::FindSetterFunction(InArgs.ObjectReference.Property.Get(), InArgs.ObjectReference.Object->GetClass()))
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

		// Instantiate the RCI processor feature on module start
		RCIProcessor = MakeUnique<FRemoteControlInterceptionProcessor>();
		// Register the interceptor feature
		IModularFeatures::Get().RegisterModularFeature(IRemoteControlInterceptionFeatureProcessor::GetName(), RCIProcessor.Get());

		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddRaw(this, &FRemoteControlModule::CachePresets);
		}

#if WITH_EDITOR
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FRemoteControlModule::HandleEnginePostInit);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		UnregisterEditorDelegates();
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);
#endif
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
	{ }

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
		UE_LOG(LogRemoteControl, VeryVerbose, TEXT("Invoke function"));
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
						UE_LOG(LogRemoteControl, VeryVerbose, TEXT("Invoke function - Intercepted"));
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
			if (CVarRemoteControlEnableOngoingChangeOptimization.GetValueOnAnyThread() == 1)
			{
				// If we have a different ongoing change that hasn't been finalized, do that before handling the next function call.
				if (OngoingModification && GetTypeHash(*OngoingModification) != GetTypeHash(InCall.CallRef))
				{
					constexpr bool bForceFinalizeChange = true;
					TestOrFinalizeOngoingChange(true);
				}
				
				if (!OngoingModification)
				{
					if (InCall.bGenerateTransaction)
					{
						if (GEditor)
						{
							GEditor->BeginTransaction(LOCTEXT("RemoteCallTransaction", "Remote Call Transaction Wrap"));
						}

						if (ensureAlways(InCall.CallRef.Object.IsValid()))
						{
							InCall.CallRef.Object->Modify();
						}
					}
				}
			}
			else if (GEditor && InCall.bGenerateTransaction)
			{
				GEditor->BeginTransaction(LOCTEXT("RemoteCallTransaction", "Remote Call Transaction Wrap"));
			}
			
#endif
			FEditorScriptExecutionGuard ScriptGuard;
			if (ensureAlways(InCall.CallRef.Object.IsValid()))
			{
				InCall.CallRef.Object->ProcessEvent(InCall.CallRef.Function.Get(), InCall.ParamStruct.GetStructMemory());
			}

#if WITH_EDITOR
			if (CVarRemoteControlEnableOngoingChangeOptimization.GetValueOnAnyThread() == 1)
			{
				// If we've called the same function recently, refresh the triggered flag and snapshot the object to the transaction buffer
				if (OngoingModification && GetTypeHash(*OngoingModification) == GetTypeHash(InCall.CallRef))
				{
					OngoingModification->bWasTriggeredSinceLastPass = true;
					if (OngoingModification->bHasStartedTransaction)
					{
						SnapshotTransactionBuffer(InCall.CallRef.Object.Get());

						if (UActorComponent* Component = Cast<UActorComponent>(InCall.CallRef.Object.Get()))
						{
							SnapshotTransactionBuffer(Component->GetOwner());
						}
					}
				}
				else
				{
					OngoingModification = InCall.CallRef;
					OngoingModification->bHasStartedTransaction = InCall.bGenerateTransaction;
				}
			}
			else if (GEditor && InCall.bGenerateTransaction)
			{
				GEditor->EndTransaction();
			}
#endif
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
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::ResolveObjectProperty);
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
							OutObjectRef = FRCObjectReference{AccessType, Object, MoveTemp(PropertyPath)};
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
					OutObjectRef = FRCObjectReference{AccessType, Object};
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
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::SetObjectProperties);
		UE_LOG(LogRemoteControl, VeryVerbose, TEXT("Set Object Properties"));
		// Check the replication path before applying property values
		if (InPayload.Num() != 0 && ObjectAccess.Object.IsValid())
		{
			// Convert raw property modifications to setter function calls if necessary.
			if (PropertyModificationShouldUseSetter(ObjectAccess.Object.Get(), ObjectAccess.Property.Get()))
			{
				FRCCall Call;
				FRCInterceptionPayload InterceptionPayload;
				constexpr bool bCreateInterceptionPayload = true;
				RemoteControlSetterUtils::FConvertToFunctionCallArgs Args(ObjectAccess, Backend, Call);
				if (RemoteControlSetterUtils::ConvertModificationToFunctionCall(Args, InterceptionPayload))
				{
					const bool bResult = InvokeCall(Call, InterceptionPayload.Type, InterceptionPayload.Payload);
					return bResult;
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
					UE_LOG(LogRemoteControl, VeryVerbose, TEXT("Set Object Properties - Intercepted"));
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
			FRCCall Call;
			RemoteControlSetterUtils::FConvertToFunctionCallArgs Args(ObjectAccess, Backend, Call);
			if (RemoteControlSetterUtils::ConvertModificationToFunctionCall(Args))
			{
				const bool bResult = InvokeCall(Call);
				return bResult;
			}
		}

		// Setting object properties require a property and can't be done at the object level. Property must be valid to move forward
		if (ObjectAccess.IsValid()
			&& (RemoteControlUtil::IsWriteAccess(ObjectAccess.Access))
			&& ObjectAccess.Property.IsValid()
			&& ObjectAccess.PropertyPathInfo.IsResolved())
		{
			UStruct* ContainerType = ObjectAccess.ContainerType.Get();
			FRCObjectReference MutableObjectReference = ObjectAccess;

#if WITH_EDITOR
			const bool bGenerateTransaction = MutableObjectReference.Access == ERCAccess::WRITE_TRANSACTION_ACCESS;
			if (CVarRemoteControlEnableOngoingChangeOptimization.GetValueOnAnyThread() == 1)
			{
				FString ObjectPath = MutableObjectReference.Object->GetPathName();

				// If we have a change that hasn't yet generated a post edit change property, do that before handling the next change.
				if (OngoingModification && GetTypeHash(*OngoingModification) != GetTypeHash(MutableObjectReference))
				{
					constexpr bool bForcePostEditChange = true;
					TestOrFinalizeOngoingChange(true);
				}

				// This step is necessary because the object might get recreated by a PostEditChange called in TestOrFinalizeOngoingChange.
				if (!MutableObjectReference.Object.IsValid())
				{
					MutableObjectReference.Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
					if (MutableObjectReference.Object.IsValid() && MutableObjectReference.PropertyPathInfo.IsResolved())
					{
						// Update ContainerAddress as well if the path was resolved.
						if (MutableObjectReference.PropertyPathInfo.Resolve(MutableObjectReference.Object.Get()))
						{
							MutableObjectReference.ContainerAdress = MutableObjectReference.PropertyPathInfo.GetResolvedData().ContainerAddress;
						}
					}
					else
					{
						return false;
					}
				}

				// Only create the transaction if we have no ongoing change.
				if (!OngoingModification)
				{
					if (GEditor && bGenerateTransaction)
					{
						GEditor->BeginTransaction(LOCTEXT("RemoteSetPropertyTransaction", "Remote Set Object Property"));

						// Call modify since it's not called by PreEditChange until the end of the ongoing change.
						MutableObjectReference.Object->Modify();
					}
				}

			}
			else 
			{
				if (GEditor && bGenerateTransaction)
				{
					GEditor->BeginTransaction(LOCTEXT("RemoteSetPropertyTransaction", "Remote Set Object Property"));
				}

				FEditPropertyChain PreEditChain;
				MutableObjectReference.PropertyPathInfo.ToEditPropertyChain(PreEditChain);
				MutableObjectReference.Object->PreEditChange(PreEditChain);
				
			}
#endif

			FStructDeserializerPolicies Policies;
			if (MutableObjectReference.Property.IsValid())
			{
				Policies.PropertyFilter = [&MutableObjectReference](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return CurrentProp == MutableObjectReference.Property || ParentProp != nullptr;
				};
			}
			else
			{
				bool bObjectInGame = !GIsEditor || MutableObjectReference.Object->GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor);
				Policies.PropertyFilter = [&MutableObjectReference, bObjectInGame](const FProperty* CurrentProp, const FProperty* ParentProp)
				{
					return RemoteControlUtil::IsPropertyAllowed(CurrentProp, MutableObjectReference.Access, bObjectInGame) || ParentProp != nullptr;
				};
			}

			//Serialize the element if we're looking for a member or serialize the full object if not
			bool bSuccess = false;
			if (MutableObjectReference.PropertyPathInfo.IsResolved())
			{
				const FRCFieldPathSegment& LastSegment = MutableObjectReference.PropertyPathInfo.GetFieldSegment(MutableObjectReference.PropertyPathInfo.GetSegmentCount() - 1);
				int32 Index = LastSegment.ArrayIndex != INDEX_NONE ? LastSegment.ArrayIndex : LastSegment.ResolvedData.MapIndex;
				bSuccess = FStructDeserializer::DeserializeElement(MutableObjectReference.ContainerAdress, *LastSegment.ResolvedData.Struct, Index, Backend, Policies);
			}
			else
			{
				bSuccess = FStructDeserializer::Deserialize(MutableObjectReference.ContainerAdress, *ContainerType, Backend, Policies);
			}

#if WITH_EDITOR
			if (CVarRemoteControlEnableOngoingChangeOptimization.GetValueOnAnyThread() == 1)
			{
				// If we have modified the same object and property in the last frames,
				// update the triggered flag and snapshot the object to the transaction buffer.
				if (OngoingModification && GetTypeHash(*OngoingModification) == GetTypeHash(MutableObjectReference))
				{
					OngoingModification->bWasTriggeredSinceLastPass = true;
					if (OngoingModification->bHasStartedTransaction)
					{
						SnapshotTransactionBuffer(MutableObjectReference.Object.Get());
					}

					// Update the world lighting if we're modifying a color.
					if (MutableObjectReference.IsValid() && MutableObjectReference.Property->GetOwnerClass()->IsChildOf(ULightComponentBase::StaticClass()))
					{
						UWorld* World = MutableObjectReference.Object->GetWorld();
						if (World && World->Scene)
						{
							if (MutableObjectReference.Object->IsA<ULightComponent>())
							{
								World->Scene->UpdateLightColorAndBrightness(Cast<ULightComponent>(MutableObjectReference.Object.Get()));
							}
						}
					}
				}
				else
				{
					OngoingModification = MutableObjectReference;
					OngoingModification->bHasStartedTransaction = bGenerateTransaction;
				}
			}
			else
			{
				if (GEditor && bGenerateTransaction)
				{
					GEditor->EndTransaction();
				}

				FPropertyChangedEvent PropertyEvent(MutableObjectReference.PropertyPathInfo.ToPropertyChangedEvent());
				MutableObjectReference.Object->PostEditChangeProperty(PropertyEvent);
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
			constexpr bool bForceEndChange = true;
			TestOrFinalizeOngoingChange(true);
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
			if (FieldPathInfo.Resolve(DefaultObject))
			{
				FRCFieldResolvedData DefaultObjectResolvedData = FieldPathInfo.GetResolvedData();
				ObjectAccess.Property->CopyCompleteValue_InContainer(TargetAddress, DefaultObjectResolvedData.ContainerAddress);
			}
			else
			{
				// Object might have been invalidated by the previous TestOrFinalizeOngoingChange invocation.
				if (ObjectAccess.Object.IsValid())
				{
					if (UStruct* Struct = ObjectAccess.Property->GetOwnerStruct())
					{
						FStructOnScope PropertyOwnerStruct{Struct};
						ObjectAccess.Property->CopyCompleteValue_InContainer(TargetAddress, PropertyOwnerStruct.GetStructMemory());
					}
					else
					{
						// Default to reseting using the property's type default value.
						ObjectAccess.Property->InitializeValue_InContainer(TargetAddress);
					}

				}
			}

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
		TRACE_CPUPROFILER_EVENT_SCOPE(FRemoteControlModule::ResolvePreset);

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
			Algo::Transform(Entry.Value, OutPresets, [this](const FAssetData& AssetData) { return Cast<URemoteControlPreset>(AssetData.GetAsset()); });
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

	virtual bool PropertySupportsRawModificationWithoutEditor(FProperty* Property, UClass* OwnerClass = nullptr) const override
	{
		constexpr bool bInGameOrPackage = true;
		return Property && (RemoteControlUtil::IsPropertyAllowed(Property, ERCAccess::WRITE_ACCESS, bInGameOrPackage) || !!RemoteControlPropertyUtilities::FindSetterFunction(Property, OwnerClass));
	}

private:
	void CachePresets()
	{
		TArray<FAssetData> Assets;
		RemoteControlUtil::GetAllPresetAssets(Assets);

		for (const FAssetData& AssetData : Assets)
		{
			CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);

			const FGuid PresetAssetId = RemoteControlUtil::GetPresetId(AssetData);
			if (PresetAssetId.IsValid())
			{
				CachedPresetNamesById.Add(PresetAssetId, AssetData.AssetName);
			}
			else if (URemoteControlPreset* Preset = Cast<URemoteControlPreset>(AssetData.GetAsset()))
			{
				// Handle the case where the preset asset data does not contain the ID yet.
				// This can happen with old assets that haven't been resaved yet.
				const FGuid PresetId = Preset->GetPresetId();
				if (PresetId.IsValid())
				{
					CachedPresetNamesById.Add(PresetId, AssetData.AssetName);
					CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);
				}
			}
		}
	}

	void OnAssetAdded(const FAssetData& AssetData)
	{
		if (AssetData.AssetClass != URemoteControlPreset::StaticClass()->GetFName())
		{
			return;
		}

		const FGuid PresetAssetId = RemoteControlUtil::GetPresetId(AssetData);
		if (PresetAssetId.IsValid())
		{
			CachedPresetNamesById.Add(PresetAssetId, AssetData.AssetName);
			CachedPresetsByName.FindOrAdd(AssetData.AssetName).AddUnique(AssetData);
		}
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

		return !!RemoteControlPropertyUtilities::FindSetterFunction(Property, Object->GetClass());
	}

#if WITH_EDITOR
	void TestOrFinalizeOngoingChange(bool bForceEndChange = false)
	{
		if (OngoingModification)
		{
			if (bForceEndChange || !OngoingModification->bWasTriggeredSinceLastPass)
			{
				// Call PreEditChange for the last call
				if (OngoingModification->Reference.IsType<FRCObjectReference>())
				{
					FRCObjectReference& Reference = OngoingModification->Reference.Get<FRCObjectReference>();
					if (Reference.IsValid())
					{
						FEditPropertyChain PreEditChain;
						Reference.PropertyPathInfo.ToEditPropertyChain(PreEditChain);
						Reference.Object->PreEditChange(PreEditChain);
					}
				}
			
				// If not, trigger the post edit change (and end the transaction if one was started)
				if (GEditor && OngoingModification->bHasStartedTransaction)
				{
					GEditor->EndTransaction();
				}

				if (OngoingModification->Reference.IsType<FRCObjectReference>())
				{
					FPropertyChangedEvent PropertyEvent = OngoingModification->Reference.Get<FRCObjectReference>().PropertyPathInfo.ToPropertyChangedEvent();
					if (UObject* Object = OngoingModification->Reference.Get<FRCObjectReference>().Object.Get())
					{
						Object->PostEditChangeProperty(PropertyEvent);
					}
				}

				OngoingModification.Reset();
			}
			else
			{
				// If the change has occured for this change, effectively reset the counter for it.
				OngoingModification->bWasTriggeredSinceLastPass = false;
			}
		}
	}

	void HandleEnginePostInit()
	{
		CachePresets();
		RegisterEditorDelegates();
	}

	void RegisterEditorDelegates()
	{
		if (GEditor)
		{
			GEditor->GetTimerManager()->SetTimer(OngoingChangeTimer, FTimerDelegate::CreateRaw(this, &FRemoteControlModule::TestOrFinalizeOngoingChange, false), SecondsBetweenOngoingChangeCheck, true);
		}
	}
	
	void UnregisterEditorDelegates()
	{
		if (GEditor)
		{ 
			GEditor->GetTimerManager()->ClearTimer(OngoingChangeTimer);
		}
	}
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
		FOngoingChange(FRCObjectReference InReference)
		{
			Reference.Set<FRCObjectReference>(MoveTemp(InReference));
		}
		
		FOngoingChange(FRCCallReference InReference)
		{
			Reference.Set<FRCCallReference>(MoveTemp(InReference));
		}

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
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRemoteControlModule, RemoteControl);
