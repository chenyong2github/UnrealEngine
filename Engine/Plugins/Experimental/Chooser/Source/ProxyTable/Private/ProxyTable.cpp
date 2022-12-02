// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProxyTable.h"

UObjectChooser_LookupProxy::UObjectChooser_LookupProxy(const FObjectInitializer& ObjectInitializer)
{
	ProxyTable = ObjectInitializer.CreateDefaultSubobject<UChooserParameterProxyTable_ContextProperty>(this, "InputValue");
	ProxyTable.GetObject()->SetFlags(RF_Transactional);
}

static UObject* FindProxyObject(const UProxyTable* Table, FName Key, const UObject* ContextObject)
{
	for (const FProxyEntry& Entry : Table->Entries)
	{
		if (Entry.Key == Key)
		{
			if (const IObjectChooser* EntryValue = Entry.Value.GetInterface())
			{
				return EntryValue->ChooseObject(ContextObject);
			}
		}
	}

	// search parent tables (uncooked data only)
	for (const TObjectPtr<UProxyTable> ParentTable : Table->InheritEntriesFrom)
	{
		if (UObject* Value = FindProxyObject(ParentTable, Key, ContextObject))
		{
			return Value;
		}
	}

	return nullptr;
}

UObject* UObjectChooser_LookupProxy::ChooseObject(const UObject* ContextObject) const
{
	if (ProxyTable)
	{
		const UProxyTable* Table;
		if (ProxyTable->GetValue(ContextObject, Table))
		{
			if (Table)
			{
				if (UObject* Value = FindProxyObject(Table, Key, ContextObject))
				{
					return Value;
				}
			}
		}
				
	}
	
	return nullptr;
}

UProxyTable::UProxyTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

}

bool UChooserParameterProxyTable_ContextProperty::GetValue(const UObject* ContextObject, const UProxyTable*& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, PropertyBindingChain))
	{
		if (const FObjectProperty* Property = FindFProperty<FObjectProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<UProxyTable*>(Container);
			return true;
		}
	}

	return false;
}