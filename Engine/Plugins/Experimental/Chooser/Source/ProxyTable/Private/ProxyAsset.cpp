// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProxyAsset.h"
#include "ProxyTableFunctionLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ChooserPropertyAccess.h"
#include "UObject/Package.h"
#include "LookupProxy.h"

#if WITH_EDITOR
void UProxyAsset::PostEditUndo()
{
	UObject::PostEditUndo();

	if (CachedPreviousType != Type)
	{
		OnTypeChanged.Broadcast(Type);
		CachedPreviousType = Type;
	}
	
	OnContextClassChanged.Broadcast();
}

void UProxyAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	
	static FName TypeName = "Type";
	static FName ContextClassName = "ContextData";
	if (PropertyChangedEvent.Property->GetName() == TypeName)
	{
		if (CachedPreviousType != Type)
		{
			OnTypeChanged.Broadcast(Type);
		}
		CachedPreviousType = Type;
	}
	else
	{
		OnContextClassChanged.Broadcast();
	}
}

#endif

#if WITH_EDITORONLY_DATA

/////////////////////////////////////////////////////////////////////////////////////////
// Proxy Asset

void UProxyAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	CachedPreviousType = Type;
#endif
	
	if (ContextClass_DEPRECATED)
	{
		
		ContextData.SetNum(1);
		ContextData[0].InitializeAs<FContextObjectTypeClass>();
		FContextObjectTypeClass& Context = ContextData[0].GetMutable<FContextObjectTypeClass>();
		Context.Class = ContextClass_DEPRECATED;
		Context.Direction = EContextObjectDirection::ReadWrite;
		ContextClass_DEPRECATED = nullptr;
	}

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

#endif

UProxyAsset::UProxyAsset(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	ProxyTable.InitializeAs(FProxyTableContextProperty::StaticStruct());
}

UObject* UProxyAsset::FindProxyObject(FChooserEvaluationContext& Context) const
{
	if (ProxyTable.IsValid())
	{
		const UProxyTable* Table;
		if (ProxyTable.Get<FChooserParameterProxyTableBase>().GetValue(Context, Table))
		{
			if(Table)
			{
				if (UObject* Value = Table->FindProxyObject(Guid, Context))
				{
					return Value;
				}
			}
		}
	}
	
	return nullptr;
}
