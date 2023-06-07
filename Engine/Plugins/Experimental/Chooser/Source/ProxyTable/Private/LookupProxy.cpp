// Copyright Epic Games, Inc. All Rights Reserved.
#include "LookupProxy.h"
#include "ProxyTableFunctionLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ChooserPropertyAccess.h"
#include "UObject/Package.h"

UObject* FLookupProxy::ChooseObject(FChooserEvaluationContext& Context) const
{
	if (Proxy)
	{
		return Proxy->FindProxyObject(Context);
	}
	return nullptr;
}

UObject* FLookupProxyWithOverrideTable::ChooseObject(FChooserEvaluationContext& Context) const
{
	if (Proxy && OverrideProxyTable)
	{
		return OverrideProxyTable->FindProxyObject(Proxy->Guid, Context);
	}
	return nullptr;
}

bool FProxyTableContextProperty::GetValue(FChooserEvaluationContext& Context, const UProxyTable*& OutResult) const
{
	const UStruct* StructType = nullptr;
	const void* Container = nullptr;
	
	if (UE::Chooser::ResolvePropertyChain(Context, Binding,Container, StructType))
	{
		if (const FObjectProperty* Property = FindFProperty<FObjectProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<UProxyTable*>(Container);
			return true;
		}
	}

	return false;
}
