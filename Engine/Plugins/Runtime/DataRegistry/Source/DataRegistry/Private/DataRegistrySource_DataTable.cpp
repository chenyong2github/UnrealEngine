// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataRegistrySource_DataTable.h"
#include "DataRegistryTypesPrivate.h"
#include "DataRegistrySettings.h"
#include "Interfaces/ITargetPlatform.h"
#include "Engine/AssetManager.h"

void UDataRegistrySource_DataTable::SetSourceTable(const TSoftObjectPtr<UDataTable>& InSourceTable, const FDataRegistrySource_DataTableRules& InTableRules)
{
	if (ensure(IsTransientSource() || GIsEditor))
	{
		SourceTable = InSourceTable;
		TableRules = InTableRules;
		SetCachedTable(false);
	}
}

void UDataRegistrySource_DataTable::SetCachedTable(bool bForceLoad /*= false*/)
{
#if WITH_EDITOR
	if (CachedTable)
	{
		CachedTable->OnDataTableChanged().RemoveAll(this);
	}
#endif

	CachedTable = nullptr;
	UDataTable* FoundTable = SourceTable.Get();

	if (!FoundTable && (bForceLoad || TableRules.bUseHardReference))
	{
		if (IsTransientSource() && TableRules.bUseHardReference && !bForceLoad)
		{
			// Possibly bad sync load, should we warn?
		}

		FoundTable = SourceTable.LoadSynchronous();
	}

	if (FoundTable)
	{
		const UScriptStruct* ItemStruct = GetItemStruct();
		const UScriptStruct* RowStruct = FoundTable->GetRowStruct();

		if (FoundTable->HasAnyFlags(RF_NeedLoad))
		{
			UE_LOG(LogDataRegistry, Error, TEXT("Cannot initialize DataRegistry source %s, Preload table was not set, resave in editor!"), *GetPathName());
		}
		else if(!ItemStruct || !RowStruct)
		{
			UE_LOG(LogDataRegistry, Error, TEXT("Cannot initialize DataRegistry source %s, Table %s or registry is invalid!"), *GetPathName(), *FoundTable->GetPathName());
		}
		else if (!RowStruct->IsChildOf(ItemStruct))
		{
			UE_LOG(LogDataRegistry, Error, TEXT("Cannot initialize DataRegistry source %s, Table %s type does not match %s"), *GetPathName(), *FoundTable->GetPathName(), *RowStruct->GetName(), *ItemStruct->GetName());
		}
		else
		{
			CachedTable = FoundTable;

#if WITH_EDITOR
			// Listen for changes like row 
			CachedTable->OnDataTableChanged().AddUObject(this, &UDataRegistrySource_DataTable::EditorRefreshSource);
#endif
		}
	}

	if (PreloadTable != CachedTable && TableRules.bUseHardReference)
	{
		ensureMsgf(GIsEditor || !PreloadTable, TEXT("Switching a valid PreloadTable to a new table should only happen in the editor!"));
		PreloadTable = CachedTable;
	}

	LastAccessTime = GetRegistry()->GetCurrentTime();
}

void UDataRegistrySource_DataTable::ClearCachedTable()
{
	// For soft refs this will null, for hard refs it will set to preload one
	CachedTable = PreloadTable;
}

void UDataRegistrySource_DataTable::PostLoad()
{
	Super::PostLoad();

	SetCachedTable(false);
}

EDataRegistryAvailability UDataRegistrySource_DataTable::GetSourceAvailability() const
{
	if (TableRules.bUseHardReference)
	{
		return EDataRegistryAvailability::InMemory;
	}
	else
	{
		return EDataRegistryAvailability::LocalAsset;
	}
}

EDataRegistryAvailability UDataRegistrySource_DataTable::GetItemAvailability(const FName& ResolvedName, const uint8** InMemoryDataPtr) const
{
	LastAccessTime = GetRegistry()->GetCurrentTime();

	if (CachedTable)
	{
		uint8* FoundRow = CachedTable->FindRowUnchecked(ResolvedName);

		if (FoundRow)
		{
			if (TableRules.bUseHardReference)
			{
				// Return struct if found
				if (InMemoryDataPtr)
				{
					*InMemoryDataPtr = FoundRow;
				}

				return EDataRegistryAvailability::InMemory;
			}
			else
			{
				return EDataRegistryAvailability::LocalAsset;
			}
		}
		else
		{
			return EDataRegistryAvailability::DoesNotExist;
		}
	}
	else
	{
		return EDataRegistryAvailability::Unknown;
	}
}

void UDataRegistrySource_DataTable::GetResolvedNames(TArray<FName>& Names) const
{
	LastAccessTime = GetRegistry()->GetCurrentTime();

	if (!CachedTable && GIsEditor)
	{
		// Force load in editor
		const_cast<UDataRegistrySource_DataTable*>(this)->SetCachedTable(true);
	}

	if (CachedTable)
	{
		Names = CachedTable->GetRowNames();
	}
}

void UDataRegistrySource_DataTable::ResetRuntimeState()
{
	ClearCachedTable();

	if (LoadingTableHandle.IsValid())
	{
		LoadingTableHandle->CancelHandle();
		LoadingTableHandle.Reset();
	}

	Super::ResetRuntimeState();
}

bool UDataRegistrySource_DataTable::AcquireItem(FDataRegistrySourceAcquireRequest&& Request)
{
	LastAccessTime = GetRegistry()->GetCurrentTime();

	PendingAcquires.Add(Request);

	if (CachedTable)
	{
		// Tell it to go next frame
		FStreamableHandle::ExecuteDelegate(FStreamableDelegate::CreateUObject(this, &UDataRegistrySource_DataTable::HandlePendingAcquires));
	}
	else if (!LoadingTableHandle.IsValid() || !LoadingTableHandle->IsActive())
	{
		// If already in progress, don't request again
		LoadingTableHandle = UAssetManager::Get().LoadAssetList({ SourceTable.ToSoftObjectPath() }, FStreamableDelegate::CreateUObject(this, &UDataRegistrySource_DataTable::OnTableLoaded));
	}

	return true;
}

void UDataRegistrySource_DataTable::TimerUpdate(float CurrentTime, float TimerUpdateFrequency)
{
	Super::TimerUpdate(CurrentTime, TimerUpdateFrequency);

	// If we have a valid keep seconds, see if it has expired and release cache if needed
	if (TableRules.CachedTableKeepSeconds >= 0 && !TableRules.bUseHardReference && CachedTable)
	{
		if (CurrentTime - LastAccessTime > TableRules.CachedTableKeepSeconds)
		{
			ClearCachedTable();
		}
	}
}

FString UDataRegistrySource_DataTable::GetDebugString() const
{
	const UDataRegistry* Registry = GetRegistry();
	if (!SourceTable.IsNull() && Registry)
	{
		return FString::Printf(TEXT("%s(%d)"), *SourceTable.GetAssetName(), Registry->GetSourceIndex(this));
	}
	return Super::GetDebugString();
}

bool UDataRegistrySource_DataTable::Initialize()
{
	if (Super::Initialize())
	{
		// Add custom logic

		return true;
	}

	return false;
}

void UDataRegistrySource_DataTable::HandlePendingAcquires()
{
	LastAccessTime = GetRegistry()->GetCurrentTime(); 

	// Iterate manually to deal with recursive adds
	int32 NumRequests = PendingAcquires.Num();
	for (int32 i = 0; i < NumRequests; i++)
	{
		// Make a copy in case array changes
		FDataRegistrySourceAcquireRequest Request = PendingAcquires[i];

		uint8 Sourceindex = 255;
		FName ResolvedName;
			
		if (Request.Lookup.GetEntry(Sourceindex, ResolvedName, Request.LookupIndex))
		{
			if (CachedTable)
			{
				const UScriptStruct* ItemStruct = GetItemStruct();
				if (ensure(ItemStruct && ItemStruct->GetStructureSize()))
				{
					uint8* FoundRow = CachedTable->FindRowUnchecked(ResolvedName);

					if (FoundRow)
					{
						// Allocate new copy of struct, will be handed off to cache
						uint8* ItemStructMemory = FCachedDataRegistryItem::AllocateItemMemory(ItemStruct);

						ItemStruct->CopyScriptStruct(ItemStructMemory, FoundRow);

						HandleAcquireResult(Request, EDataRegistryAcquireStatus::InitialAcquireFinished, ItemStructMemory);
						continue;
					}
				}
			}
		}
		else
		{
			// Invalid request
		}
		
		// Acquire failed for some reason, report failure for each one
		HandleAcquireResult(Request, EDataRegistryAcquireStatus::AcquireError, nullptr);
		
	}
		
	PendingAcquires.RemoveAt(0, NumRequests);
}

void UDataRegistrySource_DataTable::OnTableLoaded()
{
	// Set cache pointer than handle any pending requests
	LoadingTableHandle.Reset();

	SetCachedTable(false);

	HandlePendingAcquires();
}

#if WITH_EDITOR

void UDataRegistrySource_DataTable::EditorRefreshSource()
{
	SetCachedTable(false);
}

void UDataRegistrySource_DataTable::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	// Force load it to validate type on save
	SetCachedTable(true);
}

#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UMetaDataRegistrySource_DataTable::UMetaDataRegistrySource_DataTable()
{
	CreatedSource = UDataRegistrySource_DataTable::StaticClass();
	SearchRules.AssetBaseClass = UDataTable::StaticClass();
}

TSubclassOf<UDataRegistrySource> UMetaDataRegistrySource_DataTable::GetChildSourceClass() const
{
	return CreatedSource;
}

bool UMetaDataRegistrySource_DataTable::SetDataForChild(FName SourceId, UDataRegistrySource* ChildSource)
{
	UDataRegistrySource_DataTable* ChildDataTable = Cast<UDataRegistrySource_DataTable>(ChildSource);
	if (ensure(ChildDataTable))
	{
		TSoftObjectPtr<UDataTable> NewTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(SourceId));
		ChildDataTable->SetSourceTable(NewTable, TableRules);
		return true;
	}
	return false;
}

bool UMetaDataRegistrySource_DataTable::DoesAssetPassFilter(const FAssetData& AssetData, bool bRegisteredAsset)
{
	// Call into parent to check search rules if needed	
	if (bRegisteredAsset)
	{
		bool bPassesFilter = UAssetManager::Get().DoesAssetMatchSearchRules(AssetData, SearchRules);
		if (!bPassesFilter)
		{
#if !WITH_EDITORONLY_DATA
			const UDataRegistrySettings* Settings = GetDefault<UDataRegistrySettings>();
			if (Settings->bIgnoreMissingCookedAssetRegistryData)
			{
				// Drop the class and tag check, only do basic path validation
				FAssetManagerSearchRules ModifiedRules = SearchRules;
				ModifiedRules.AssetBaseClass = nullptr;

				bPassesFilter = UAssetManager::Get().DoesAssetMatchSearchRules(AssetData, ModifiedRules);
				if (bPassesFilter)
				{
					return true;
				}
			}
#endif

			return false;
		}
	}

	const UScriptStruct* ItemStruct = GetItemStruct();
	static const FName RowStructureTagName("RowStructure");
	FString RowStructureString;
	if (AssetData.GetTagValue(RowStructureTagName, RowStructureString))
	{
		if (RowStructureString == ItemStruct->GetName())
		{
			return true;
		}
		else
		{
			// TODO no 100% way to check for inherited row structs, but BP types can't inherit anyway
			UScriptStruct* RowStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *RowStructureString, true);

			if (RowStruct && RowStruct->IsChildOf(ItemStruct))
			{
				return true;
			}
		}
	}

	return false;
}

