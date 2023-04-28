// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProxyTableFunctionLibrary.h"
#include "ProxyTable.h"

/////////////////////////////////////////////////////////////////////////////////////
// Blueprint Library Functions

UObject* UProxyTableFunctionLibrary::EvaluateProxyAsset(const UObject* ContextObject, const UProxyAsset* Proxy, TSubclassOf<UObject> ObjectClass)
{
	UObject* Result = nullptr;
	if (Proxy)
	{
		FChooserEvaluationContext Context;
		Context.ContextData.Add({ContextObject->GetClass(), const_cast<UObject*>(ContextObject)});
		
		Result = Proxy->FindProxyObject(Context);
		if (ObjectClass && Result && !Result->IsA(ObjectClass))
		{
			return nullptr;
		}
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
		FChooserEvaluationContext Context;
		Context.ContextData.Add({ContextObject->GetClass(), const_cast<UObject*>(ContextObject)});
		if (UObject* Value = ProxyTable->FindProxyObject(Guid, Context))
		{
			return Value;
		}
	}
	
	return nullptr;
}
