// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructVariantCustomization.h"
#include "UObject/StructVariant.h"
#include "UObject/StructOnScope.h"

#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StructVariantCustomization"

FStructVariantCustomization::~FStructVariantCustomization()
{
	if (SyncEditableInstanceFromVariantsTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SyncEditableInstanceFromVariantsTickHandle);
	}
}

void FStructVariantCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();
	StructPropertyHandle = InStructPropertyHandle;

	// Create a struct instance to edit, for the common struct type of the variants being edited
	StructInstanceData.Reset();
	if (const UScriptStruct* CommonStructType = GetSelectedStructType())
	{
		StructInstanceData = MakeShared<FStructOnScope>(CommonStructType);

		// Make sure the struct also has a valid package set, so that properties that rely on this (like FText) work correctly
		{
			TArray<UPackage*> OuterPackages;
			InStructPropertyHandle->GetOuterPackages(OuterPackages);
			if (OuterPackages.Num() > 0)
			{
				StructInstanceData->SetPackage(OuterPackages[0]);
			}
		}
	}

	// If there is a single variant, copy its current data to the instance to edit
	// If there are multiple then we'll just edit the defaults
	// TODO: Better support for "multiple values"?
	SyncEditableInstanceFromVariants();
	SyncEditableInstanceFromVariantsTickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("StructVariantCustomization"), 0.1f, [WeakThis = TWeakPtr<FStructVariantCustomization>(SharedThis(this))](float)
	{
		if (TSharedPtr<FStructVariantCustomization> This = WeakThis.Pin())
		{
			bool bStructMismatch = false;
			This->SyncEditableInstanceFromVariants(&bStructMismatch);

			if (bStructMismatch)
			{
				// If the editable struct no longer has the same struct type as the underlying variant, 
				// then we need to ForceRefresh to update the child property rows for the new type
				This->PropertyUtilities->ForceRefresh();
				return false;
			}

			return true;
		}

		return false;
	});

	// Create the struct picker
	TSharedPtr<SStructPropertyEntryBox> StructPicker;
	{
		const FProperty* MetaDataProperty = StructPropertyHandle->GetMetaDataProperty();

		const UScriptStruct* MetaStruct = nullptr;
		{
			const FString& MetaStructName = MetaDataProperty->GetMetaData(TEXT("MetaStruct"));
			if (!MetaStructName.IsEmpty())
			{
				MetaStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *MetaStructName);
				if (!MetaStruct)
				{
					MetaStruct = LoadObject<UScriptStruct>(nullptr, *MetaStructName);
				}
			}
		}

		const bool bAllowNone = !(MetaDataProperty->PropertyFlags & CPF_NoClear);
		const bool bHideViewOptions = MetaDataProperty->HasMetaData(TEXT("HideViewOptions"));
		const bool bShowDisplayNames = MetaDataProperty->HasMetaData(TEXT("ShowDisplayNames"));
		const bool bShowTreeView = MetaDataProperty->HasMetaData(TEXT("ShowTreeView"));

		SAssignNew(StructPicker, SStructPropertyEntryBox)
			.MetaStruct(MetaStruct)
			.AllowNone(bAllowNone)
			.HideViewOptions(bHideViewOptions)
			.ShowDisplayNames(bShowDisplayNames)
			.ShowTreeView(bShowTreeView)
			.SelectedStruct(this, &FStructVariantCustomization::GetSelectedStructType)
			.OnSetStruct(this, &FStructVariantCustomization::SetSelectedStructType);
	}

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(0.0f)
		.MinDesiredWidth(125.0f)
		[
			StructPicker.ToSharedRef()
		];
}

void FStructVariantCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (StructInstanceData)
	{
		FSimpleDelegate OnStructValuePreChangeDelegate = FSimpleDelegate::CreateSP(this, &FStructVariantCustomization::OnStructValuePreChange);
		FSimpleDelegate OnStructValuePostChangeDelegate = FSimpleDelegate::CreateSP(this, &FStructVariantCustomization::OnStructValuePostChange);

		// Note: We use AddExternalStructureProperty here as it gives the desired result (the struct value properties as direct children of the struct header)
		// Neither AddExternalStructure (which added an extra row) nor AddAllExternalStructureProperties (which didn't indent the properties as children) were suitable
		for (TFieldIterator<const FProperty> PropertyIt(StructInstanceData->GetStruct()); PropertyIt; ++PropertyIt)
		{
			if (IDetailPropertyRow* StructValuePropertyRow = StructBuilder.AddExternalStructureProperty(StructInstanceData.ToSharedRef(), PropertyIt->GetFName()))
			{
				TSharedPtr<IPropertyHandle> StructValuePropertyHandle = StructValuePropertyRow->GetPropertyHandle();
				StructValuePropertyHandle->SetOnPropertyValuePreChange(OnStructValuePreChangeDelegate);
				StructValuePropertyHandle->SetOnChildPropertyValuePreChange(OnStructValuePreChangeDelegate);
				StructValuePropertyHandle->SetOnPropertyValueChanged(OnStructValuePostChangeDelegate);
				StructValuePropertyHandle->SetOnChildPropertyValueChanged(OnStructValuePostChangeDelegate);
			}
		}
	}
}

const UScriptStruct* FStructVariantCustomization::GetSelectedStructType() const
{
	const UScriptStruct* CommonStructType = nullptr;

	ForEachConstStructVariant([&CommonStructType](const FStructVariant* Variant, const int32 VariantIndex, const int32 NumVariants)
	{
		if (Variant)
		{
			const UScriptStruct* StructTypePtr = Variant->GetStructType();
			if (CommonStructType && CommonStructType != StructTypePtr)
			{
				// Multiple struct types on the variants - show nothing set
				CommonStructType = nullptr;
				return false;
			}
			CommonStructType = StructTypePtr;
		}

		return true;
	});

	return CommonStructType;
}

void FStructVariantCustomization::SetSelectedStructType(const UScriptStruct* InStructType)
{
	FScopedTransaction Transaction(LOCTEXT("SetStructType", "Set Struct Type"));

	OnStructValuePreChange();

	ForEachStructVariant([InStructType](FStructVariant* Variant, const int32 VariantIndex, const int32 NumVariants)
	{
		if (Variant)
		{
			Variant->SetStructType(InStructType);
		}
		return true;
	});

	OnStructValuePostChange();
}

void FStructVariantCustomization::SyncEditableInstanceFromVariants(bool* OutStructMismatch)
{
	if (OutStructMismatch)
	{
		*OutStructMismatch = false;
	}

	const UScriptStruct* ExpectedStructType = StructInstanceData ? Cast<UScriptStruct>(StructInstanceData->GetStruct()) : nullptr;
	ForEachStructVariant([this, ExpectedStructType, OutStructMismatch](FStructVariant* Variant, const int32 VariantIndex, const int32 NumVariants)
	{
		if (Variant && NumVariants == 1)
		{
			// Only copy the data if this variant is still using the expected struct type
			const UScriptStruct* StructTypePtr = Variant->GetStructType();
			if (StructTypePtr == ExpectedStructType)
			{
				if (StructTypePtr)
				{
					StructTypePtr->CopyScriptStruct(StructInstanceData->GetStructMemory(), Variant->GetStructInstance());
				}
			}
			else if (OutStructMismatch)
			{
				*OutStructMismatch = true;
			}
		}
		return false;
	});
}

void FStructVariantCustomization::SyncEditableInstanceToVariants(bool* OutStructMismatch)
{
	if (OutStructMismatch)
	{
		*OutStructMismatch = false;
	}

	const UScriptStruct* ExpectedStructType = StructInstanceData ? Cast<UScriptStruct>(StructInstanceData->GetStruct()) : nullptr;
	ForEachStructVariant([this, ExpectedStructType, OutStructMismatch](FStructVariant* Variant, const int32 VariantIndex, const int32 NumVariants)
	{
		if (Variant)
		{
			// Only copy the data if this variant is still using the expected struct type
			const UScriptStruct* StructTypePtr = Variant->GetStructType();
			if (StructTypePtr == ExpectedStructType)
			{
				if (StructTypePtr)
				{
					StructTypePtr->CopyScriptStruct(Variant->GetStructInstance(), StructInstanceData->GetStructMemory());
				}
			}
			else if (OutStructMismatch)
			{
				*OutStructMismatch = true;
			}
		}
		return false;
	});
}

void FStructVariantCustomization::OnStructValuePreChange()
{
	// Forward the change event to the real struct handle
	if (StructPropertyHandle->IsValidHandle())
	{
		StructPropertyHandle->NotifyPreChange();
	}
}

void FStructVariantCustomization::OnStructValuePostChange()
{
	// Copy the modified struct data back to the variant instances
	SyncEditableInstanceToVariants();

	// Forward the change event to the real struct handle
	if (StructPropertyHandle->IsValidHandle())
	{
		StructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}
}

void FStructVariantCustomization::ForEachStructVariant(TFunctionRef<bool(FStructVariant* /*Variant*/, const int32 /*VariantIndex*/, const int32 /*NumVariants*/)> Callback)
{
	if (StructPropertyHandle->IsValidHandle())
	{
		StructPropertyHandle->EnumerateRawData([&Callback](void* RawData, const int32 DataIndex, const int32 NumDatas)
		{
			return Callback(static_cast<FStructVariant*>(RawData), DataIndex, NumDatas);
		});
	}
}

void FStructVariantCustomization::ForEachConstStructVariant(TFunctionRef<bool(const FStructVariant* /*Variant*/, const int32 /*VariantIndex*/, const int32 /*NumVariants*/)> Callback) const
{
	if (StructPropertyHandle->IsValidHandle())
	{
		StructPropertyHandle->EnumerateConstRawData([&Callback](const void* RawData, const int32 DataIndex, const int32 NumDatas)
		{
			return Callback(static_cast<const FStructVariant*>(RawData), DataIndex, NumDatas);
		});
	}
}

#undef LOCTEXT_NAMESPACE
