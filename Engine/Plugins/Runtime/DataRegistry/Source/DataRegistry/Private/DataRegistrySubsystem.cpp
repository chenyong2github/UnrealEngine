// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataRegistrySubsystem.h"
#include "UObject/UObjectIterator.h"
#include "GameplayTagsManager.h"
#include "Engine/AssetManager.h"
#include "DataRegistrySettings.h"
#include "Stats/StatsMisc.h"
#include "AssetData.h"
#include "ARFilter.h"
#include "UnrealEngine.h"
#include "Misc/WildcardString.h"
#include "Curves/RealCurve.h"
#include "Misc/CoreDelegates.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

UDataRegistrySubsystem* UDataRegistrySubsystem::Get()
{
	return GEngine->GetEngineSubsystem<UDataRegistrySubsystem>();
}

//static bool GetCachedItem(FDataRegistryId ItemId, UPARAM(ref) FTableRowBase& OutItem);
DEFINE_FUNCTION(UDataRegistrySubsystem::execGetCachedItemBP)
{
	P_GET_STRUCT(FDataRegistryId, ItemId);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	void* OutItemDataPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* OutItemProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH;

	UDataRegistrySubsystem* SubSystem = UDataRegistrySubsystem::Get();
	check(SubSystem);

	const uint8* CacheData = nullptr;
	const UScriptStruct* CacheStruct = nullptr;
	FDataRegistryCacheGetResult CacheResult;

	if (OutItemProp && OutItemDataPtr && SubSystem->IsConfigEnabled(true))
	{
		P_NATIVE_BEGIN;
		CacheResult = SubSystem->GetCachedItemRaw(CacheData, CacheStruct, ItemId);

		if (CacheResult && CacheStruct && CacheData)
		{
			UScriptStruct* OutputStruct = OutItemProp->Struct;

			const bool bCompatible = (OutputStruct == CacheStruct) ||
				(OutputStruct->IsChildOf(CacheStruct) && FStructUtils::TheSameLayout(OutputStruct, CacheStruct));
			
			if (bCompatible)
			{
				CacheStruct->CopyScriptStruct(OutItemDataPtr, CacheData);
			}
		}
		P_NATIVE_END;
	}

	*(bool*)RESULT_PARAM = !!CacheResult;
}

// static bool GetCachedItemFromLookup(FDataRegistryId ItemId, const FDataRegistryLookup& ResolvedLookup, UPARAM(ref) FTableRowBase& OutItem) { return false; }
DEFINE_FUNCTION(UDataRegistrySubsystem::execGetCachedItemFromLookupBP)
{
	P_GET_STRUCT(FDataRegistryId, ItemId);
	P_GET_STRUCT_REF(FDataRegistryLookup, ResolvedLookup);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);

	void* OutItemDataPtr = Stack.MostRecentPropertyAddress;
	FStructProperty* OutItemProp = CastField<FStructProperty>(Stack.MostRecentProperty);
	P_FINISH;

	UDataRegistrySubsystem* SubSystem = UDataRegistrySubsystem::Get();
	check(SubSystem);

	const uint8* CacheData = nullptr;
	const UScriptStruct* CacheStruct = nullptr;
	FDataRegistryCacheGetResult CacheResult;

	if (OutItemProp && OutItemDataPtr && SubSystem->IsConfigEnabled(true))
	{
		P_NATIVE_BEGIN;
		CacheResult = SubSystem->GetCachedItemRawFromLookup(CacheData, CacheStruct, ItemId, ResolvedLookup);

		if (CacheResult && CacheStruct && CacheData)
		{
			UScriptStruct* OutputStruct = OutItemProp->Struct;

			const bool bCompatible = (OutputStruct == CacheStruct) ||
				(OutputStruct->IsChildOf(CacheStruct) && FStructUtils::TheSameLayout(OutputStruct, CacheStruct));

			if (bCompatible)
			{
				CacheStruct->CopyScriptStruct(OutItemDataPtr, CacheData);
			}
		}
		P_NATIVE_END;
	}

	*(bool*)RESULT_PARAM = !!CacheResult;
}


UDataRegistry* UDataRegistrySubsystem::GetRegistryForType(FName RegistryType) const
{
	// Weak on purpose to avoid issues with asset deletion in editor
	const TWeakObjectPtr<UDataRegistry>* FoundRegistry = RegistryMap.Find(RegistryType);

	if (FoundRegistry)
	{
		if (FoundRegistry->IsValid())
		{
			return FoundRegistry->Get();
		}
	}

	return nullptr;
}

FText UDataRegistrySubsystem::GetDisplayTextForId(FDataRegistryId ItemId) const
{
	UDataRegistry* DataRegistry = GetRegistryForType(ItemId.RegistryType);
	if (DataRegistry)
	{
		// If tag prefix is redundant, strip it
		const FDataRegistryIdFormat& IdFormat = DataRegistry->GetIdFormat();

		if (IdFormat.BaseGameplayTag.GetTagName() == ItemId.RegistryType.GetName())
		{
			return FText::FromName(ItemId.ItemName);
		}
	}

	return ItemId.ToText();
}

void UDataRegistrySubsystem::GetAllRegistries(TArray<UDataRegistry*>& AllRegistries, bool bSortByType) const
{
	AllRegistries.Reset();

	for (const TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
	{
		UDataRegistry* Registry = RegistryPair.Value.Get();
		if (Registry)
		{
			AllRegistries.Add(Registry);
		}
	}

	if (bSortByType)
	{
		AllRegistries.Sort([](const UDataRegistry& LHS, const UDataRegistry& RHS) { return LHS.GetRegistryType().LexicalLess(RHS.GetRegistryType()); });
	}
}

void UDataRegistrySubsystem::RefreshRegistryMap()
{
	const UDataRegistrySettings* Settings = GetDefault<UDataRegistrySettings>();

	RegistryMap.Reset();

	for (TObjectIterator<UDataRegistry> RegistryIterator; RegistryIterator; ++RegistryIterator)
	{
		UDataRegistry* Registry = *RegistryIterator;

		if (!Settings->bInitializeAllLoadedRegistries)
		{
			// Check it's one of the scanned directories
			FString ObjectPath = Registry->GetPathName();
			bool bFoundPath = false;
			for (const FString& ScanPath : AssetScanPaths)
			{
				if (ObjectPath.StartsWith(ScanPath))
				{
					bFoundPath = true;
					break;
				}
			}

			if (!bFoundPath)
			{
				continue;
			}
		}

		RegistryMap.Add(Registry->GetRegistryType(), Registry);
		
		// Apply pending map before we initialize
		if (!Registry->IsInitialized())
		{
			ApplyPreregisterMap(Registry);
		}
	}
}

void UDataRegistrySubsystem::LoadAllRegistries()
{
	SCOPED_BOOT_TIMING("UDataRegistrySubsystem::LoadAllRegistries");

	if (!IsConfigEnabled())
	{
		// Don't do anything if not enabled
		return;
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	IAssetRegistry& AssetRegistry = AssetManager.GetAssetRegistry();

	const UDataRegistrySettings* Settings = GetDefault<UDataRegistrySettings>();

	AssetScanPaths.Reset();
	for (const FDirectoryPath& PathRef : Settings->DirectoriesToScan)
	{
		if (!PathRef.Path.IsEmpty())
		{
			AssetScanPaths.AddUnique(UAssetManager::GetNormalizedPackagePath(PathRef.Path, false));
		}
	}

	FAssetManagerSearchRules Rules;
	Rules.AssetScanPaths = AssetScanPaths;
	Rules.AssetBaseClass = UDataRegistry::StaticClass();

	bool bExpandedVirtual = AssetManager.ExpandVirtualPaths(Rules.AssetScanPaths);
	Rules.bSkipVirtualPathExpansion = true;

	if (bExpandedVirtual)
	{
		// Handling this properly will require some integration with modular code
		UE_LOG(LogDataRegistry, Error, TEXT("Currently DataRegistries will not automatically refresh if located in virtual asset search roots."));
	}

	TArray<FAssetData> AssetDataList;
	AssetManager.SearchAssetRegistryPaths(AssetDataList, Rules);

	TArray<FSoftObjectPath> PathsToLoad;

	// Now add to map or update as needed
	for (FAssetData& Data : AssetDataList)
	{
		PathsToLoad.Add(AssetManager.GetAssetPathForData(Data));
	}

	if (PathsToLoad.Num() > 0)
	{
		// Do as one async bulk load, faster in cooked builds
		TSharedPtr<FStreamableHandle> LoadHandle = AssetManager.LoadAssetList(PathsToLoad);

		if (LoadHandle.IsValid())
		{
			LoadHandle->WaitUntilComplete();
		}

		if (!GIsEditor)
		{
			// Need to keep these all from GCing in non editor builds
			RegistryLoadHandle = LoadHandle;
		}
	}

	RefreshRegistryMap();
	InitializeAllRegistries();
}

bool UDataRegistrySubsystem::AreRegistriesInitialized() const
{
	return bFullyInitialized;
}

bool UDataRegistrySubsystem::IsConfigEnabled(bool bWarnIfNotEnabled /*= false*/) const
{
	if (bFullyInitialized)
	{
		return true;
	}

	const UDataRegistrySettings* Settings = GetDefault<UDataRegistrySettings>();
	if (Settings->DirectoriesToScan.Num() == 0 && !Settings->bInitializeAllLoadedRegistries)
	{
		if (bWarnIfNotEnabled)
		{
			UE_LOG(LogDataRegistry, Warning, TEXT("DataRegistry functions are not enabled, to fix set scan options in the DataRegistry settings."));
		}
		return false;
	}

	return true;
}

void UDataRegistrySubsystem::InitializeAllRegistries(bool bResetIfInitialized)
{
	for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
	{
		UDataRegistry* Registry = RegistryPair.Value.Get();
		if (Registry && !Registry->IsInitialized())
		{
			Registry->Initialize();
		}
		else if (Registry && bResetIfInitialized)
		{
			Registry->ResetRuntimeState();
		}
	}

	bFullyInitialized = true;
}

void UDataRegistrySubsystem::DeinitializeAllRegistries()
{
	for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
	{
		UDataRegistry* Registry = RegistryPair.Value.Get();
		if (Registry)
		{
			Registry->Deinitialize();
		}
	}

	bFullyInitialized = false;
}

void UDataRegistrySubsystem::ResetRuntimeState()
{
	for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
	{
		UDataRegistry* Registry = RegistryPair.Value.Get();
		if (Registry && Registry->IsInitialized())
		{
			Registry->ResetRuntimeState();
		}
	}
}

void UDataRegistrySubsystem::ReinitializeFromConfig()
{
	if (bReadyForInitialization)
	{
		LoadAllRegistries();
	}
}

bool UDataRegistrySubsystem::RegisterSpecificAsset(FDataRegistryType RegistryType, FAssetData& AssetData, int32 AssetPriority /*= 0*/)
{
	if (!IsConfigEnabled(true))
	{
		return false;
	}

	// Update priority in pending list if found
	TArray<FPreregisterAsset>* FoundPreregister = PreregisterAssetMap.Find(RegistryType);

	if (FoundPreregister)
	{
		FSoftObjectPath AssetPath = AssetData.ToSoftObjectPath();
		for (int32 i = 0; i < FoundPreregister->Num(); i++)
		{
			if (AssetPath == (*FoundPreregister)[i].Key)
			{
				(*FoundPreregister)[i].Value = AssetPriority;
				break;
			}
		}
	}

	if (!RegistryType.IsValid())
	{
		bool bMadeChange = false;
		for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
		{
			UDataRegistry* Registry = RegistryPair.Value.Get();
			if (Registry)
			{
				bMadeChange |= Registry->RegisterSpecificAsset(AssetData, AssetPriority);
			}
		}
		return bMadeChange;
	}

	UDataRegistry* FoundRegistry = GetRegistryForType(RegistryType);
	if (FoundRegistry)
	{
		return FoundRegistry->RegisterSpecificAsset(AssetData, AssetPriority);
	}

	return false;
}

bool UDataRegistrySubsystem::UnregisterSpecificAsset(FDataRegistryType RegistryType, const FSoftObjectPath& AssetPath)
{
	if (!IsConfigEnabled(true))
	{
		return false;
	}

	// First take out of pending list
	TArray<FPreregisterAsset>* FoundPreregister = PreregisterAssetMap.Find(RegistryType);

	if (FoundPreregister)
	{
		for (int32 i = 0; i < FoundPreregister->Num(); i++)
		{
			if (AssetPath == (*FoundPreregister)[i].Key)
			{
				FoundPreregister->RemoveAt(i);
				break;
			}
		}
	}

	if (!RegistryType.IsValid())
	{
		bool bMadeChange = false;
		for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
		{
			UDataRegistry* Registry = RegistryPair.Value.Get();
			if (Registry)
			{
				bMadeChange |= Registry->UnregisterSpecificAsset(AssetPath);
			}
		}
		return bMadeChange;
	}

	UDataRegistry* FoundRegistry = GetRegistryForType(RegistryType);
	if (FoundRegistry)
	{
		return FoundRegistry->UnregisterSpecificAsset(AssetPath);
	}

	return false;
}

void UDataRegistrySubsystem::PreregisterSpecificAssets(const TMap<FDataRegistryType, TArray<FSoftObjectPath>>& AssetMap, int32 AssetPriority)
{
	if (!IsConfigEnabled(true))
	{
		return;
	}

	TSet<FName> ChangedTypeSet;
	for (const TPair<FDataRegistryType, TArray<FSoftObjectPath>>& TypePair : AssetMap)
	{
		// Update map, then apply if already exists
		TArray<FPreregisterAsset>& FoundPreregister = PreregisterAssetMap.FindOrAdd(TypePair.Key);

		for (const FSoftObjectPath& PathToAdd : TypePair.Value)
		{
			bool bFoundAsset = false;
			for (int32 i = 0; i < FoundPreregister.Num(); i++)
			{
				if (PathToAdd == FoundPreregister[i].Key)
				{
					bFoundAsset = true;
					if (AssetPriority != FoundPreregister[i].Value)
					{
						// Update value
						FoundPreregister[i].Value = AssetPriority;
						ChangedTypeSet.Add(TypePair.Key.GetName());
					}
					break;
				}
			}

			if (!bFoundAsset)
			{
				FoundPreregister.Emplace(PathToAdd, AssetPriority);
				ChangedTypeSet.Add(TypePair.Key.GetName());
			}
		}
	}

	// If we've initially loaded, apply to all modified types
	if (bFullyInitialized)
	{
		for (TPair<FName, TWeakObjectPtr<UDataRegistry>>& RegistryPair : RegistryMap)
		{
			if (ChangedTypeSet.Contains(NAME_None) || ChangedTypeSet.Contains(RegistryPair.Key))
			{
				UDataRegistry* Registry = RegistryPair.Value.Get();
				if (Registry)
				{
					ApplyPreregisterMap(Registry);
				}
			}
		}
	}
}

void UDataRegistrySubsystem::ApplyPreregisterMap(UDataRegistry* Registry)
{
	// Apply both the invalid and type-specific lists
	const UDataRegistrySettings* Settings = GetDefault<UDataRegistrySettings>();
	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FPreregisterAsset>* FoundPreregister = PreregisterAssetMap.Find(Registry->GetRegistryType());

	if (FoundPreregister)
	{
		for (int32 i = 0; i < FoundPreregister->Num(); i++)
		{
			const FSoftObjectPath& AssetPath = (*FoundPreregister)[i].Key;
			FAssetData AssetData;
			if (AssetManager.GetAssetDataForPath(AssetPath, AssetData))
			{
				Registry->RegisterSpecificAsset(AssetData, (*FoundPreregister)[i].Value);
			}
#if !WITH_EDITORONLY_DATA
			else if (Settings->bIgnoreMissingCookedAssetRegistryData)
			{
				AssetData = FAssetData(AssetPath.GetLongPackageName(), AssetPath.GetAssetPathString(), NAME_Object);

				Registry->RegisterSpecificAsset(AssetData, (*FoundPreregister)[i].Value);
			}
#endif
		}
	}

	FoundPreregister = PreregisterAssetMap.Find(FDataRegistryType());

	if (FoundPreregister)
	{
		for (int32 i = 0; i < FoundPreregister->Num(); i++)
		{
			const FSoftObjectPath& AssetPath = (*FoundPreregister)[i].Key;
			FAssetData AssetData;
			if (AssetManager.GetAssetDataForPath(AssetPath, AssetData))
			{
				Registry->RegisterSpecificAsset(AssetData, (*FoundPreregister)[i].Value);
			}
#if !WITH_EDITORONLY_DATA
			else if (Settings->bIgnoreMissingCookedAssetRegistryData)
			{
				AssetData = FAssetData(AssetPath.GetLongPackageName(), AssetPath.GetAssetPathString(), NAME_Object);

				Registry->RegisterSpecificAsset(AssetData, (*FoundPreregister)[i].Value);
			}
#endif
		}
	}
}

FDataRegistryCacheGetResult UDataRegistrySubsystem::GetCachedItemRaw(const uint8*& OutItemMemory, const UScriptStruct*& OutItemStruct, const FDataRegistryId& ItemId) const
{
	const UDataRegistry* FoundRegistry = GetRegistryForType(ItemId.RegistryType);
	if (FoundRegistry)
	{
		return FoundRegistry->GetCachedItemRaw(OutItemMemory, OutItemStruct, ItemId);
	}
	OutItemMemory = nullptr;
	OutItemStruct = nullptr;
	return FDataRegistryCacheGetResult();
}

FDataRegistryCacheGetResult UDataRegistrySubsystem::GetCachedItemRawFromLookup(const uint8*& OutItemMemory, const UScriptStruct*& OutItemStruct, const FDataRegistryId& ItemId, const FDataRegistryLookup& Lookup) const
{
	const UDataRegistry* FoundRegistry = GetRegistryForType(ItemId.RegistryType);
	if (FoundRegistry)
	{
		return FoundRegistry->GetCachedItemRawFromLookup(OutItemMemory, OutItemStruct, ItemId, Lookup);
	}
	OutItemMemory = nullptr;
	OutItemStruct = nullptr;
	return FDataRegistryCacheGetResult();
}

bool UDataRegistrySubsystem::AcquireItem(const FDataRegistryId& ItemId, FDataRegistryItemAcquiredCallback DelegateToCall) const
{
	UDataRegistry* FoundRegistry = GetRegistryForType(ItemId.RegistryType);

	if (FoundRegistry)
	{
		return FoundRegistry->AcquireItem(ItemId, DelegateToCall);
	}

	return false;
}

bool UDataRegistrySubsystem::AcquireItemBP(FDataRegistryId ItemId, FDataRegistryItemAcquiredBPCallback AcquireCallback)
{
	UDataRegistrySubsystem* SubSystem = UDataRegistrySubsystem::Get();
	check(SubSystem);

	if (!SubSystem->IsConfigEnabled(true))
	{
		return false;
	}

	// Call the BP delegate, this is safe because it always runs on the game thread on a new frame
	return SubSystem->AcquireItem(ItemId, FDataRegistryItemAcquiredCallback::CreateLambda([AcquireCallback](const FDataRegistryAcquireResult& Result)
		{
			AcquireCallback.ExecuteIfBound(Result.ItemId, Result.ResolvedLookup, Result.Status);
		}));
}

void UDataRegistrySubsystem::EvaluateDataRegistryCurve(FDataRegistryId ItemId, float InputValue, float DefaultValue, EDataRegistrySubsystemGetItemResult& OutResult, float& OutValue)
{
	UDataRegistrySubsystem* SubSystem = UDataRegistrySubsystem::Get();
	check(SubSystem);

	if (!SubSystem->IsConfigEnabled(true))
	{
		return;
	}

	const FRealCurve* FoundCurve = nullptr;
	if (SubSystem->EvaluateCachedCurve(OutValue, FoundCurve, ItemId, InputValue, DefaultValue))
	{
		OutResult = EDataRegistrySubsystemGetItemResult::Found;
	}
	else
	{
		OutResult = EDataRegistrySubsystemGetItemResult::NotFound;
		OutValue = DefaultValue;
	}
}

FDataRegistryCacheGetResult UDataRegistrySubsystem::EvaluateCachedCurve(float& OutValue, const FRealCurve*& OutCurve, FDataRegistryId ItemId, float InputValue, float DefaultValue) const
{
	UDataRegistry* FoundRegistry = GetRegistryForType(ItemId.RegistryType);
	FDataRegistryCacheGetResult CacheResult;

	if (FoundRegistry)
	{
		CacheResult = FoundRegistry->GetCachedCurveRaw(OutCurve, ItemId);

		if (CacheResult && OutCurve)
		{
			OutValue = OutCurve->Eval(InputValue, DefaultValue);
			return CacheResult;
		}
	}

	// Couldn't find a curve, return default
	OutCurve = nullptr;
	OutValue = DefaultValue;
	return CacheResult;
}

bool UDataRegistrySubsystem::IsValidDataRegistryType(FDataRegistryType DataRegistryType)
{
	return DataRegistryType.IsValid();
}

FString UDataRegistrySubsystem::Conv_DataRegistryTypeToString(FDataRegistryType DataRegistryType)
{
	return DataRegistryType.ToString();
}

bool UDataRegistrySubsystem::EqualEqual_DataRegistryType(FDataRegistryType A, FDataRegistryType B)
{
	return A == B;
}

bool UDataRegistrySubsystem::NotEqual_DataRegistryType(FDataRegistryType A, FDataRegistryType B)
{
	return A != B;
}

void UDataRegistrySubsystem::PostEngineInit()
{
	UGameplayTagsManager::Get().CallOrRegister_OnDoneAddingNativeTagsDelegate(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDataRegistrySubsystem::PostGameplayTags));
}

void UDataRegistrySubsystem::PostGameplayTags()
{
	UAssetManager* AssetManager = UAssetManager::GetIfValid();

	if (AssetManager)
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UDataRegistrySubsystem::PostAssetManager));
	}
	else
	{
		UE_LOG(LogDataRegistry, Error, TEXT("Cannot initialize DataRegistrySubsystem because there is no AssetManager! Enable AssetManager or disable DataRegistry plugin"));
	}
}

void UDataRegistrySubsystem::PostAssetManager()
{
	// This will happen fast in game builds, can be many seconds in editor builds due to async load of asset registry
	bReadyForInitialization = true;
	LoadAllRegistries();
}

void UDataRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// This should always happen before PostEngineInit
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UDataRegistrySubsystem::PostEngineInit);

#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::PreBeginPIE.AddUObject(this, &UDataRegistrySubsystem::PreBeginPIE);
		FEditorDelegates::EndPIE.AddUObject(this, &UDataRegistrySubsystem::EndPIE);
	}
	
#endif
}

void UDataRegistrySubsystem::Deinitialize()
{
	if (!GExitPurge)
	{
		DeinitializeAllRegistries();
	}

	bReadyForInitialization = false;
	bFullyInitialized = false;

	Super::Deinitialize();
}

#if WITH_EDITOR

void UDataRegistrySubsystem::PreBeginPIE(bool bStartSimulate)
{
	if (IsConfigEnabled())
	{
		RefreshRegistryMap();
		InitializeAllRegistries(true);
	}
}

void UDataRegistrySubsystem::EndPIE(bool bStartSimulate)
{
	if (IsConfigEnabled())
	{
		ResetRuntimeState();
	}
}
#endif


/*
bool ADataRegistryTestActor::TestSyncRead(FDataRegistryId RegistryId)
{

	UDataRegistrySubsystem* Subsystem = GEngine->GetEngineSubsystem<UDataRegistrySubsystem>();

	if (Subsystem)
	{
		UDataRegistry* Registry = Subsystem->GetRegistryForType(RegistryId.RegistryType);

		if (Registry)
		{
			const uint8* ItemMemory = nullptr;
			const UScriptStruct* ItemStruct = nullptr;

			if (Registry->GetCachedItemRaw(ItemMemory, ItemStruct, RegistryId))
			{
				FString ValueString;
				ItemStruct->ExportText(ValueString, ItemMemory, nullptr, nullptr, 0, nullptr);
				
				UE_LOG(LogDataRegistry, Display, TEXT("DataRegistryTestActor::TestSyncRead succeeded on %s with %s"), *RegistryId.ToString(), *ValueString);
				
				return true;
			}
			else
			{
				UE_LOG(LogDataRegistry, Error, TEXT("DataRegistryTestActor::TestSyncRead can't find item for %s!"), *RegistryId.ToString());
			}
		}
		else
		{
			UE_LOG(LogDataRegistry, Error, TEXT("DataRegistryTestActor::TestSyncRead can't find registry for type %s!"), *RegistryId.RegistryType.ToString());
		}

	}

	UE_LOG(LogDataRegistry, Error, TEXT("DataRegistryTestActor::TestSyncRead failed!"));
	return false;
}

bool ADataRegistryTestActor::TestAsyncRead(FDataRegistryId RegistryId)
{
	UDataRegistrySubsystem* Subsystem = GEngine->GetEngineSubsystem<UDataRegistrySubsystem>();

	if (Subsystem)
	{
		if (Subsystem->AcquireItem(RegistryId, FDataRegistryItemAcquiredCallback::CreateUObject(this, &ADataRegistryTestActor::AsyncReadComplete)))
		{
			return true;
		}
	}

	UE_LOG(LogDataRegistry, Error, TEXT("DataRegistryTestActor::TestAsyncRead failed!"));

	return false;
}

void ADataRegistryTestActor::AsyncReadComplete(const FDataRegistryAcquireResult& Result)
{
	if (Result.Status == EDataRegistryAcquireStatus::AcquireFinished)
	{
		FString ValueString;
		Result.ItemStruct->ExportText(ValueString, Result.ItemMemory, nullptr, nullptr, 0, nullptr);

		UE_LOG(LogDataRegistry, Display, TEXT("DataRegistryTestActor::AsyncReadComplete succeeded on %s with %s"), *Result.ItemId.ToString(), *ValueString);
	}
	else
	{
		UE_LOG(LogDataRegistry, Error, TEXT("DataRegistryTestActor::AsyncReadComplete can't find item for %s!"), *Result.ItemId.ToString());
	}
}
*/