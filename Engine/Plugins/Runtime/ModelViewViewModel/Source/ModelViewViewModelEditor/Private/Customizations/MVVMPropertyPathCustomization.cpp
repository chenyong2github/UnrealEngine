// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMPropertyPathCustomization.h"

#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "BlueprintEditor.h"
#include "Components/Widget.h"
#include "DetailWidgetRow.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SSimpleButton.h"
#include "Templates/UnrealTemplate.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Input/SComboBox.h" 
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SMVVMFieldSelector.h"
#include "Widgets/SMVVMSourceSelector.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MVVMPropertyPathCustomization"

namespace UE::MVVM
{
	namespace Private
	{
		// figure out what the selection is - if any is different in the case of a multiselection, then return null
		// this is required for getting the selection of the other pair
		TArray<TSharedPtr<IFieldPathHelper>> GetPathHelpersForProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle, UWidgetBlueprint* WidgetBlueprint, bool bIsWidget)
		{
			TArray<TSharedPtr<IFieldPathHelper>> Result;

			TArray<void*> RawData;
			PropertyHandle->AccessRawData(RawData);

			for (void* Datum : RawData)
			{
				const FProperty* ThisSelected = nullptr;
				
				if (bIsWidget)
				{
					Result.Add(MakeShared<FWidgetFieldPathHelper>(reinterpret_cast<FMVVMBlueprintPropertyPath*>(Datum), WidgetBlueprint));
				}
				else
				{
					Result.Add(MakeShared<FViewModelFieldPathHelper>(reinterpret_cast<FMVVMBlueprintPropertyPath*>(Datum), WidgetBlueprint));
				}
			}
			return Result;
		}

		TArray<IFieldPathHelper*> GetRawPathHelperPointers(const TArray<TSharedPtr<IFieldPathHelper>>& SharedPtrs)
		{
			TArray<IFieldPathHelper*> RawPointers;
			RawPointers.Reserve(SharedPtrs.Num());

			Algo::Transform(SharedPtrs, RawPointers, [](const TSharedPtr<IFieldPathHelper>& X) { return X.Get(); });

			return RawPointers;
		}
	}

	FPropertyPathCustomization::FPropertyPathCustomization(UWidgetBlueprint* InWidgetBlueprint) :
		WidgetBlueprint(InWidgetBlueprint)
	{
		check(WidgetBlueprint != nullptr);

		OnBlueprintChangedHandle = WidgetBlueprint->OnChanged().AddRaw(this, &FPropertyPathCustomization::HandleBlueprintChanged);
	}

	FPropertyPathCustomization::~FPropertyPathCustomization()
	{
		WidgetBlueprint->OnChanged().Remove(OnBlueprintChangedHandle);
	}

	void FPropertyPathCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		PropertyHandle = InPropertyHandle;

		bool bIsWidget = false;

		const FName PropertyName = PropertyHandle->GetProperty()->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, WidgetPath))
		{
			bIsWidget = true;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, ViewModelPath))
		{
			bIsWidget = false;
		}

		PathHelpers = Private::GetPathHelpersForProperty(PropertyHandle, WidgetBlueprint, bIsWidget);

		TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
		TSharedPtr<IPropertyHandle> OtherHandle;

		if (bIsWidget)
		{
			OtherHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, ViewModelPath));
		}
		else
		{
			OtherHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, WidgetPath));
		}

		if (OtherHandle.IsValid() && OtherHandle->IsValidHandle())
		{
			OtherHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPropertyPathCustomization::OnOtherPropertyChanged));
			OtherHelpers = Private::GetPathHelpersForProperty(OtherHandle, WidgetBlueprint, !bIsWidget);
		}
		else
		{
			ensureMsgf(false, TEXT("MVVMPropertyPathCustomization used in unknown context."));
		}

		TSharedPtr<IPropertyHandle> BindingTypeHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, BindingType));
		if (BindingTypeHandle.IsValid() && BindingTypeHandle->IsValidHandle())
		{
			BindingTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPropertyPathCustomization::OnOtherPropertyChanged));
		}
		else
		{
			ensureMsgf(false, TEXT("MVVMPropertyPathCustomization used in unknown context."));
		}

		uint8 Value;
		BindingTypeHandle->GetValue(Value);

		HeaderRow
			.NameWidget
			.VAlign(VAlign_Center)
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueWidget
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SAssignNew(SourceSelector, SMVVMSourceSelector)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.PathHelpers(this, &FPropertyPathCustomization::GetRawPathHelpers)
					.OnSelectionChanged(this, &FPropertyPathCustomization::OnSourceSelectionChanged)
				]
			+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(FieldSelector, SMVVMFieldSelector)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.PathHelpers(GetRawPathHelpers())
					.CounterpartHelpers(GetRawOtherHelpers())
					.BindingMode(this, &FPropertyPathCustomization::GetCurrentBindingMode)
					.OnSelectionChanged(this, &FPropertyPathCustomization::OnPropertySelectionChanged)
				]				
			];
	}

	TArray<IFieldPathHelper*> FPropertyPathCustomization::GetRawPathHelpers() const
	{
		return Private::GetRawPathHelperPointers(PathHelpers);
	}

	TArray<IFieldPathHelper*> FPropertyPathCustomization::GetRawOtherHelpers() const
	{
		return Private::GetRawPathHelperPointers(OtherHelpers);
	}

	void FPropertyPathCustomization::OnOtherPropertyChanged()
	{
		FieldSelector->Refresh();
	}

	EMVVMBindingMode FPropertyPathCustomization::GetCurrentBindingMode() const
	{
		TSharedPtr<IPropertyHandle> BindingTypeHandle = PropertyHandle->GetParentHandle()->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, BindingType));
		uint8 BindingMode;
		if (BindingTypeHandle->GetValue(BindingMode) == FPropertyAccess::Success)
		{
			return (EMVVMBindingMode) BindingMode;
		}

		return EMVVMBindingMode::OneWayToDestination;
	}

	void FPropertyPathCustomization::OnSourceSelectionChanged(FBindingSource Selected)
	{
		FScopedTransaction Transaction(LOCTEXT("ChangeSource", "Change binding source."));

		PropertyHandle->NotifyPreChange();

		for (const TSharedPtr<IFieldPathHelper>& Path : PathHelpers)
		{
			Path->SetSelectedSource(Selected);
			Path->ResetBinding(); // Might make sense to keep this around in case we retarget to a compatible widget or switch back.
		}

		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

		FieldSelector->Refresh();
	}

	void FPropertyPathCustomization::OnPropertySelectionChanged(FMVVMConstFieldVariant Selected)
	{
		if (bPropertySelectionChanging)
		{
			return;
		}
		TGuardValue<bool> ReentrantGuard(bPropertySelectionChanging, true);

		FScopedTransaction Transaction(LOCTEXT("ChangeBindingProperty", "Change binding property."));

		PropertyHandle->NotifyPreChange();

		// This is currently handled in SMVVMFieldSelector, but I wonder if it should be here...
		TArray<IFieldPathHelper*> Helpers = GetRawPathHelpers();
		for (const IFieldPathHelper* Helper : Helpers)
		{
			Helper->SetBindingReference(Selected);
		}

		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}

	void FPropertyPathCustomization::HandleBlueprintChanged(UBlueprint* Blueprint)
	{
		SourceSelector->Refresh();
		FieldSelector->Refresh();
	}
}

#undef LOCTEXT_NAMESPACE