// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProxyTable.h"
#include "ProxyTableFunctionLibrary.h"

FLookupProxy::FLookupProxy()
{
}

static UObject* FindProxyObject(const UProxyTable* Table, const UProxyAsset* Proxy, const UObject* ContextObject)
{
	if (Table)
	{
		for (const FProxyEntry& Entry : Table->Entries)
		{
			if (Entry.Proxy == Proxy && Entry.ValueStruct.IsValid())
			{
				const FObjectChooserBase& EntryValue = Entry.ValueStruct.Get<FObjectChooserBase>();
				return EntryValue.ChooseObject(ContextObject);
			}
		}

		// search parent tables (uncooked data only)
		for (const TObjectPtr<UProxyTable> ParentTable : Table->InheritEntriesFrom)
		{
			if (UObject* Value = FindProxyObject(ParentTable, Proxy, ContextObject))
			{
				return Value;
			}
		}
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
			if (UObject* Value = FindProxyObject(Table, Proxy, ContextObject))
			{
				return Value;
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

static UObject* FindProxyObject(const UProxyTable* Table, FName Key, const UObject* ContextObject)
{
	if (Table)
	{
		for (const FProxyEntry& Entry : Table->Entries)
		{
			if (Entry.Key == Key && Entry.ValueStruct.IsValid())
			{
				const FObjectChooserBase& EntryValue = Entry.ValueStruct.Get<FObjectChooserBase>();
				return EntryValue.ChooseObject(ContextObject);
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
	}

	return nullptr;
}

UObject* UProxyTableFunctionLibrary::EvaluateProxyTable(const UObject* ContextObject, const UProxyTable* ProxyTable, FName Key)
{
	return FindProxyObject(ProxyTable, Key, ContextObject);
}
