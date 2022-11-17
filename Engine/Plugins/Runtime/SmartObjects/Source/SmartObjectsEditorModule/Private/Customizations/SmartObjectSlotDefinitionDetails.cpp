// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectSlotDefinitionDetails.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SmartObjectDefinition.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SmartObjectEditor"

TSharedRef<IPropertyTypeCustomization> FSmartObjectSlotDefinitionDetails::MakeInstance()
{
	return MakeShareable(new FSmartObjectSlotDefinitionDetails);
}

void FSmartObjectSlotDefinitionDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	CacheOuterDefinition();
	
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			StructPropertyHandle->CreatePropertyValueWidget()
		]
		.CopyAction(FUIAction(FExecuteAction::CreateSP(this, &FSmartObjectSlotDefinitionDetails::OnCopy)))
		.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FSmartObjectSlotDefinitionDetails::OnPaste)));
}

void FSmartObjectSlotDefinitionDetails::CacheOuterDefinition()
{
	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);
	for (int32 ObjectIdx = 0; ObjectIdx < OuterObjects.Num(); ObjectIdx++)
	{
		if (USmartObjectDefinition* OuterDefinition = OuterObjects[ObjectIdx]->GetTypedOuter<USmartObjectDefinition>())
		{
			Definition = OuterDefinition;
			break;
		}
	}
}

void FSmartObjectSlotDefinitionDetails::OnCopy() const
{
	FString Value;
	if (StructProperty->GetValueAsFormattedString(Value, PPF_Copy) == FPropertyAccess::Success)
	{
		FPlatformApplicationMisc::ClipboardCopy(*Value);
	}
}

void FSmartObjectSlotDefinitionDetails::OnPaste() const
{
	if (Definition == nullptr)
	{
		return;
	}
	
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	FScopedTransaction Transaction(LOCTEXT("PasteNode", "Paste Node"));

	StructProperty->NotifyPreChange();

	if (StructProperty->SetValueFromFormattedString(PastedText, EPropertyValueSetFlags::InstanceObjects) == FPropertyAccess::Success)
	{
		// Reset GUIDs on paste
		TArray<void*> RawNodeData;
		StructProperty->AccessRawData(RawNodeData);
		for (void* Data : RawNodeData)
		{
			if (FSmartObjectSlotDefinition* Slot = static_cast<FSmartObjectSlotDefinition*>(Data))
			{
				Slot->ID = FGuid::NewGuid();
				Slot->SelectionPreconditions.SchemaClass = Definition->GetWorldConditionSchemaClass();
				Slot->SelectionPreconditions.Initialize();
			}
		}
		
		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		StructProperty->NotifyFinishedChangingProperties();

		if (PropUtils)
		{
			PropUtils->ForceRefresh();
		}
	}
	else
	{
		Transaction.Cancel();
	}
}

void FSmartObjectSlotDefinitionDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const FName HiddenName(TEXT("Hidden"));

	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++)
	{
		if (TSharedPtr<IPropertyHandle> ChildPropertyHandle = StructPropertyHandle->GetChildHandle(ChildIndex))
		{
			const bool bIsHidden = ChildPropertyHandle->HasMetaData(HiddenName);
			if (!bIsHidden)
			{
				StructBuilder.AddProperty(ChildPropertyHandle.ToSharedRef());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
