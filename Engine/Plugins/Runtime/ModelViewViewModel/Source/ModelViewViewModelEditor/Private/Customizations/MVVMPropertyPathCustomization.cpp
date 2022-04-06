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
		TArray<TSharedPtr<IFieldPathHelper>> GetPathHelpersForProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle, UWidgetBlueprint* WidgetBlueprint, EPropertyPathType PathType)
		{
			TArray<TSharedPtr<IFieldPathHelper>> Result;

			TArray<void*> RawData;
			PropertyHandle->AccessRawData(RawData);

			for (void* Datum : RawData)
			{
				const FProperty* ThisSelected = nullptr;
				if (PathType == EPropertyPathType::ViewModel)
				{
					Result.Add(MakeShared<FViewModelFieldPathHelper>(reinterpret_cast<FMVVMViewModelPropertyPath*>(Datum), WidgetBlueprint));
				}
				else
				{
					Result.Add(MakeShared<FWidgetFieldPathHelper>(reinterpret_cast<FMVVMWidgetPropertyPath*>(Datum), WidgetBlueprint));
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

	FPropertyPathCustomizationBase::FPropertyPathCustomizationBase(UWidgetBlueprint* InWidgetBlueprint, EPropertyPathType InPathType) :
		PathType(InPathType),
		WidgetBlueprint(InWidgetBlueprint)
	{
		check(WidgetBlueprint != nullptr);

		OnBlueprintChangedHandle = WidgetBlueprint->OnChanged().AddRaw(this, &FPropertyPathCustomizationBase::HandleBlueprintChanged);
	}

	FPropertyPathCustomizationBase::~FPropertyPathCustomizationBase()
	{
		WidgetBlueprint->OnChanged().Remove(OnBlueprintChangedHandle);
	}

	void FPropertyPathCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		PropertyHandle = InPropertyHandle;
		PathHelpers = Private::GetPathHelpersForProperty(PropertyHandle, WidgetBlueprint, PathType);

		TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
		TSharedPtr<IPropertyHandle> OtherHandle;

		if (PathType == EPropertyPathType::ViewModel)
		{
			OtherHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, WidgetPath));
		}
		else
		{
			OtherHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, ViewModelPath));
		}

		if (OtherHandle.IsValid() && OtherHandle->IsValidHandle())
		{
			OtherHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPropertyPathCustomizationBase::OnOtherPropertyChanged));
			OtherHelpers = Private::GetPathHelpersForProperty(OtherHandle, WidgetBlueprint, PathType == EPropertyPathType::Widget ? EPropertyPathType::ViewModel : EPropertyPathType::Widget);
		}
		else
		{
			ensureMsgf(false, TEXT("MVVMPropertyPathCustomization used in unknown context."));
		}

		TSharedPtr<IPropertyHandle> BindingTypeHandle = ParentHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, BindingType));
		if (BindingTypeHandle.IsValid() && BindingTypeHandle->IsValidHandle())
		{
			BindingTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FPropertyPathCustomizationBase::OnOtherPropertyChanged));
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
					.PathHelpers(this, &FPropertyPathCustomizationBase::GetRawPathHelpers)
					.OnSelectionChanged(this, &FPropertyPathCustomizationBase::OnSourceSelectionChanged)
				]
			+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(FieldSelector, SMVVMFieldSelector)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.PathHelpers(GetRawPathHelpers())
					.CounterpartHelpers(GetRawOtherHelpers())
					.BindingMode(this, &FPropertyPathCustomizationBase::GetCurrentBindingMode)
					.OnSelectionChanged(this, &FPropertyPathCustomizationBase::OnPropertySelectionChanged)
				]				
			];
	}

	TArray<IFieldPathHelper*> FPropertyPathCustomizationBase::GetRawPathHelpers() const
	{
		return Private::GetRawPathHelperPointers(PathHelpers);
	}

	TArray<IFieldPathHelper*> FPropertyPathCustomizationBase::GetRawOtherHelpers() const
	{
		return Private::GetRawPathHelperPointers(OtherHelpers);
	}

	void FPropertyPathCustomizationBase::OnOtherPropertyChanged()
	{
		FieldSelector->Refresh();
	}

	EMVVMBindingMode FPropertyPathCustomizationBase::GetCurrentBindingMode() const
	{
		TSharedPtr<IPropertyHandle> BindingTypeHandle = PropertyHandle->GetParentHandle()->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, BindingType));
		uint8 BindingMode;
		if (BindingTypeHandle->GetValue(BindingMode) == FPropertyAccess::Success)
		{
			return (EMVVMBindingMode) BindingMode;
		}

		return EMVVMBindingMode::OneWayToDestination;
	}

	void FPropertyPathCustomizationBase::OnSourceSelectionChanged(TOptional<FBindingSource> Selected)
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

	void FPropertyPathCustomizationBase::OnPropertySelectionChanged(FMVVMConstFieldVariant Selected)
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

	void FPropertyPathCustomizationBase::HandleBlueprintChanged(UBlueprint* Blueprint)
	{
		SourceSelector->Refresh();
		FieldSelector->Refresh();
	}
}

#undef LOCTEXT_NAMESPACE