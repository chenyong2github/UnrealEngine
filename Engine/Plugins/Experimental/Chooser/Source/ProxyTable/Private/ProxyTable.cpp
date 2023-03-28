// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProxyTable.h"
#include "ProxyTableFunctionLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Logging/LogMacros.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogProxyTable,Log,All);

#if WITH_EDITOR
void UProxyAsset::PostEditUndo()
{
	UObject::PostEditUndo();

	if (CachedPreviousType != Type)
	{
		OnTypeChanged.Broadcast(Type);
		CachedPreviousType = Type;
	}
	
	if (CachedPreviousContextClass != ContextClass)
	{
		OnContextClassChanged.Broadcast(ContextClass);
		CachedPreviousContextClass = ContextClass;
	}
}

void UProxyAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	
	static FName TypeName = "Type";
	static FName ContextClassName = "ContextClass";
	if (PropertyChangedEvent.Property->GetName() == TypeName)
	{
		if (CachedPreviousType != Type)
		{
			OnTypeChanged.Broadcast(Type);
		}
		CachedPreviousType = Type;
	}
	else if (PropertyChangedEvent.Property->GetName() == ContextClassName)
	{
		if (CachedPreviousContextClass != ContextClass)
		{
			OnContextClassChanged.Broadcast(ContextClass);
		}
		CachedPreviousContextClass = ContextClass;
	}
}

#endif


FLookupProxy::FLookupProxy()
{
}

#if WITH_EDITORONLY_DATA

/////////////////////////////////////////////////////////////////////////////////////////
// Proxy Asset

void UProxyAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	CachedPreviousType = Type;
#endif

	if (!Guid.IsValid())
	{
		// if we load a ProxyAsset that was created before the Guid, assign it a deterministic guid based on the name and path.
		Guid.A = GetTypeHash(GetName());
		Guid.B = GetTypeHash(GetPackage()->GetPathName());
	}
}

void UProxyAsset::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	UObject::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		// create a new guid when duplicating
		Guid = FGuid::NewGuid();
	}
}

//////////////////////////////////////////////////////////////////////////////////////
/// Proxy Entry

// Gets the uint32 key that will be used as the unique identifier for the Proxy Entry
const FGuid FProxyEntry::GetGuid() const
{
	if (Proxy)
	{
		return Proxy->Guid;
	}
	else
	{
		// fallback for old FName key content
		FGuid Guid;
		if (Key != NAME_None)
		{
			Guid.A = GetTypeHash(Key);
		}
		return Guid;
	}
}

// == operator for TArray::Contains
bool FProxyEntry::operator== (const FProxyEntry& Other) const
{
	return GetGuid() == Other.GetGuid();
}

// < operator for Algo::BinarySearch, and TArray::StableSort
bool FProxyEntry::operator< (const FProxyEntry& Other) const
{
	return GetGuid() < Other.GetGuid();
}

//////////////////////////////////////////////////////////////////////////////////////
/// Proxy Table

static void BuildRuntimeDataRecursive(UProxyTable* RootTable, UProxyTable* Table, TArray<FProxyEntry>& OutEntriesArray, TArray<TWeakObjectPtr<UProxyTable>>& OutDependencies)
{
	if(Table == nullptr)
	{
		return;
	}

	if (Table != RootTable)
	{
		OutDependencies.Add(Table);
	}
	
	for (const FProxyEntry& Entry : Table->Entries)
	{
		if (Entry.Proxy)
		{
			Entry.Proxy->ConditionalPostLoad();
		}

		int32 FoundIndex = OutEntriesArray.Find(Entry);
		if (FoundIndex == INDEX_NONE)
		{
			OutEntriesArray.Add(Entry);
		}
		else
		{
			// check for Guid collisions
			if (Entry.Proxy && OutEntriesArray[FoundIndex].Proxy)
			{
				if (Entry.Proxy != OutEntriesArray[FoundIndex].Proxy)
				{
					UE_LOG(LogProxyTable, Error, TEXT("Proxy Assets %s, and %s have the same Guid. They may have been duplicated outside the editor."),
						*Entry.Proxy.GetName(), *OutEntriesArray[FoundIndex].Proxy.GetName());
				}
			}
			else
			{
				// fallback for FName based keys
				if (Entry.Key != OutEntriesArray[FoundIndex].Key)
				{
					UE_LOG(LogProxyTable, Error, TEXT("Proxy Key %s, and %s have the same Hash."),
						*Entry.Key.ToString(), *OutEntriesArray[FoundIndex].Key.ToString());
				}
			}
		}
	}

	for (const TObjectPtr<UProxyTable> ParentTable : Table->InheritEntriesFrom)
	{
		if (!OutDependencies.Contains(ParentTable))
		{
			BuildRuntimeDataRecursive(RootTable, ParentTable, OutEntriesArray, OutDependencies);
		}
	}
}

void UProxyTable::BuildRuntimeData()
{
	// Unregister callbacks on current dependencies
	for (TWeakObjectPtr<UProxyTable> Dependency : TableDependencies)
	{
		if (Dependency.IsValid())
		{
			Dependency->OnProxyTableChanged.RemoveAll(this);
		}
	}
	TableDependencies.Empty();
	ProxyDependencies.Empty();

	TArray<FProxyEntry> RuntimeEntries;
	BuildRuntimeDataRecursive(this, this, RuntimeEntries, TableDependencies);

	// sort by Key
	RuntimeEntries.StableSort();

	// Copy to Key and Value arrays
	int EntryCount = RuntimeEntries.Num();
	
	Keys.Empty(EntryCount);
	Values.Empty();
	
	TArray<FConstStructView> Views;
	Views.Reserve(EntryCount);
	
	for(const FProxyEntry& Entry : RuntimeEntries) 
	{
		Keys.Add(Entry.GetGuid());
   		Views.Add(Entry.ValueStruct);
	}

	Values = Views;
	
	// register callbacks on updated dependencies
	for (TWeakObjectPtr<UProxyTable> Dependency : TableDependencies)
	{
		Dependency->OnProxyTableChanged.AddUObject(this, &UProxyTable::BuildRuntimeData);
	}

	// keep a copy of proxy assets just for debugging purposes (indexes match the Key and Value arrays)
	for(const FProxyEntry& Entry : RuntimeEntries) 
	{
		if (Entry.Proxy)
		{
			ProxyDependencies.Add(Entry.Proxy);
		}
	}
}


void UProxyTable::PostLoad()
{
	Super::PostLoad();
	BuildRuntimeData();
}

void UProxyTable::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	UObject::PostTransacted(TransactionEvent);
	BuildRuntimeData();
	OnProxyTableChanged.Broadcast();
}

#endif

UObject* UProxyTable::FindProxyObject(const FGuid& Key, const UObject* ContextObject) const
{
	const int FoundIndex = Algo::BinarySearch(Keys, Key);
	if (FoundIndex != INDEX_NONE)
	{
		const FObjectChooserBase &EntryValue = Values[FoundIndex].Get<const FObjectChooserBase>();
		return EntryValue.ChooseObject(ContextObject);
	}
	
	return nullptr;
}

static UObject* FindProxyObject(const UProxyAsset* Proxy, const UObject* ContextObject)
{
	if (Proxy && Proxy->ProxyTable.IsValid())
	{
		const UProxyTable* Table;
		if (Proxy->ProxyTable.Get<FChooserParameterProxyTableBase>().GetValue(ContextObject, Table))
		{
			if(Table)
			{
				if (UObject* Value = Table->FindProxyObject(Proxy->Guid, ContextObject))
				{
					return Value;
				}
			}
		}
	}
	
	return nullptr;
}

UObject* FLookupProxy::ChooseObject(const UObject* ContextObject) const
{
	return FindProxyObject(Proxy, ContextObject);
}

UProxyTable::UProxyTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

}

UProxyAsset::UProxyAsset(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	ProxyTable.InitializeAs(FProxyTableContextProperty::StaticStruct());
}

bool FProxyTableContextProperty::GetValue(const UObject* ContextObject, const UProxyTable*& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, Binding.PropertyBindingChain))
	{
		if (const FObjectProperty* Property = FindFProperty<FObjectProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<UProxyTable*>(Container);
			return true;
		}
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////
// Blueprint Library Functions

UObject* UProxyTableFunctionLibrary::EvaluateProxyAsset(const UObject* ContextObject, const UProxyAsset* Proxy, TSubclassOf<UObject> ObjectClass)
{
	UObject* Result = FindProxyObject(Proxy, ContextObject);
	if (ObjectClass && Result && !Result->IsA(ObjectClass))
	{
		return nullptr;
	}
	return Result;
	
}

// fallback for FName based Keys:
UObject* UProxyTableFunctionLibrary::EvaluateProxyTable(const UObject* ContextObject, const UProxyTable* ProxyTable, FName Key)
{
	if (ProxyTable)
	{
		FGuid Guid;
		Guid.A = GetTypeHash(Key);
		if (UObject* Value = ProxyTable->FindProxyObject(Guid, ContextObject))
		{
			return Value;
		}
	}
	
	return nullptr;
}
