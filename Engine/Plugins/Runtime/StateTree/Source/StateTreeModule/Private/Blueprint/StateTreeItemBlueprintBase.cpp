// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeItemBlueprintBase.h"
#include "CoreMinimal.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"

UWorld* UStateTreeItemBlueprintBase::GetWorld() const
{
	// The items are duplicated as the StateTreeExecution context as outer, so this should be essentially the same as GetWorld() on StateTree context.
	// The CDO is used by the BP editor to check for certain functionality, make it return nullptr so that the GetWorld() passes as overridden. 
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		if (UObject* Outer = GetOuter())
		{
			return Outer->GetWorld();
		}
	}
	
	return nullptr;
}

AActor* UStateTreeItemBlueprintBase::GetOwnerActor(const FStateTreeExecutionContext& Context) const
{
	if (const AAIController* Controller = Cast<AAIController>(Context.GetOwner()))
	{
		return Controller->GetPawn();
	}
	
	return Cast<AActor>(Context.GetOwner());
}

#if WITH_EDITOR
void UStateTreeItemBlueprintBase::PostCDOCompiled()
{
	// Save some property data for runtime use.
	PropertyInfos.Reset();

	static const FName CategoryName(TEXT("Category"));

	for (TFieldIterator<FProperty> It(GetClass()); It; ++It)
	{
		const FString Category = It->GetMetaData(CategoryName);
		if (Category == TEXT("Input"))
		{
			PropertyInfos.Emplace(It->GetFName(), EStateTreeBlueprintPropertyCategory::Input);
		}
		else if (Category == TEXT("Output"))
		{
			PropertyInfos.Emplace(It->GetFName(), EStateTreeBlueprintPropertyCategory::Output);
		}
		else if (Category == TEXT("Parameter"))
		{
			PropertyInfos.Emplace(It->GetFName(), EStateTreeBlueprintPropertyCategory::Parameter);
		}
		else if (Category == TEXT("ExternalData"))
		{
			PropertyInfos.Emplace(It->GetFName(), EStateTreeBlueprintPropertyCategory::ExternalData);
		}
	}
}
#endif

void UStateTreeItemBlueprintBase::LinkExternalData(FStateTreeLinker& Linker, TArray<FStateTreeBlueprintExternalDataHandle>& OutExternalDataHandles) const
{
	const UClass* Class = GetClass();

	// Find properties which are external data requirements.
	for (const FStateTreeBlueprintPropertyInfo& PropertyInfo : PropertyInfos)
	{
		if (PropertyInfo.Category == EStateTreeBlueprintPropertyCategory::ExternalData)
		{
			const FProperty* Property = Class->FindPropertyByName(PropertyInfo.PropertyName);
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				FStateTreeBlueprintExternalDataHandle& ExtData = OutExternalDataHandles.AddDefaulted_GetRef();
				ExtData.Property = Property;
				Linker.LinkExternalData(ExtData.Handle, ObjectProperty->PropertyClass, EStateTreeExternalDataRequirement::Required);
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				FStateTreeBlueprintExternalDataHandle& ExtData = OutExternalDataHandles.AddDefaulted_GetRef();
				ExtData.Property = Property;
				Linker.LinkExternalData(ExtData.Handle, StructProperty->Struct, EStateTreeExternalDataRequirement::Required);
			}
		}
	}
}

void UStateTreeItemBlueprintBase::CopyExternalData(FStateTreeExecutionContext& Context, TConstArrayView<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles)
{
	for (const FStateTreeBlueprintExternalDataHandle& ExtData : ExternalDataHandles)
	{
		FStateTreeDataView View = Context.GetExternalDataView(ExtData.Handle);
		if (View.IsValid())
		{
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(ExtData.Property))
			{
				check(View.GetStruct()->IsChildOf(ObjectProperty->PropertyClass));
				// Copy pointer
				uint8* Address = ObjectProperty->ContainerPtrToValuePtr<uint8>(this);
				ObjectProperty->SetObjectPropertyValue(Address, View.GetMutablePtr<UObject>());
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(ExtData.Property))
			{
				check(View.GetStruct()->IsChildOf(StructProperty->Struct));
				// Copy value
				uint8* Address = StructProperty->ContainerPtrToValuePtr<uint8>(this);
				StructProperty->CopyCompleteValue(Address, View.GetMemory());
			}
		}
	}
}