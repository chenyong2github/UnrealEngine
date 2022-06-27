// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMPropertyPathCustomization.h"

#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "BlueprintEditor.h"
#include "Components/Widget.h"
#include "DetailWidgetRow.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "PropertyHandle.h"
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

		const FName PropertyName = PropertyHandle->GetProperty()->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, WidgetPath))
		{
			bIsWidget = true;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewBinding, ViewModelPath))
		{
			bIsWidget = false;
		}
		else
		{
			ensureMsgf(false, TEXT("MVVMPropertyPathCustomization used in unknown context."));
		}

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
					.AvailableSources(this, &FPropertyPathCustomization::OnGetSources)
					.SelectedSource(this, &FPropertyPathCustomization::OnGetSelectedSource)
					.OnSelectionChanged(this, &FPropertyPathCustomization::OnSourceSelectionChanged)
				]
			+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(FieldSelector, SMVVMFieldSelector)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.BindingMode(this, &FPropertyPathCustomization::GetCurrentBindingMode)
					.SelectedSource(this, &FPropertyPathCustomization::OnGetSelectedSource)
					.OnSelectionChanged(this, &FPropertyPathCustomization::OnPropertySelectionChanged)
					.AvailableFields(this, &FPropertyPathCustomization::OnGetFields)
					.SelectedField(this, &FPropertyPathCustomization::OnGetSelectedField)
				]				
			];
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
		PropertyHandle->NotifyPreChange();

		UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		
		TArray<void*> RawData;
		PropertyHandle->AccessRawData(RawData);

		for (void* RawPtr : RawData)
		{
			if (RawPtr == nullptr)
			{
				continue;
			}

			FMVVMBlueprintPropertyPath* PropertyPath = (FMVVMBlueprintPropertyPath*) RawPtr;
			if (bIsWidget)
			{
				PropertyPath->SetWidgetName(Selected.Name);
			}
			else
			{
				PropertyPath->SetViewModelId(Selected.ViewModelId);
			}
		}

		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

		FieldSelector->Refresh();
	}

	void FPropertyPathCustomization::OnPropertySelectionChanged(FMVVMBlueprintPropertyPath Selected)
	{
		if (bPropertySelectionChanging)
		{
			return;
		}
		TGuardValue<bool> ReentrantGuard(bPropertySelectionChanging, true);

		PropertyHandle->NotifyPreChange();

		TArray<void*> RawData;
		PropertyHandle->AccessRawData(RawData);

		for (void* RawPtr : RawData)
		{
			if (RawPtr == nullptr)
			{
				continue;
			}

			FMVVMBlueprintPropertyPath* PropertyPath = (FMVVMBlueprintPropertyPath*) RawPtr;
			*PropertyPath = Selected;
		}

		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}

	void FPropertyPathCustomization::HandleBlueprintChanged(UBlueprint* Blueprint)
	{
		SourceSelector->Refresh();
		FieldSelector->Refresh();
	}

	TArray<UE::MVVM::FBindingSource> FPropertyPathCustomization::OnGetSources() const 
	{
		UMVVMEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		if (bIsWidget)
		{
			return Subsystem->GetBindableWidgets(WidgetBlueprint);
		}
		else
		{
			return Subsystem->GetAllViewModels(WidgetBlueprint);
		}
	}

	UE::MVVM::FBindingSource FPropertyPathCustomization::OnGetSelectedSource() const
	{
		TArray<void*> RawData;
		PropertyHandle->AccessRawData(RawData);

		FGuid SelectedGuid;
		FName SelectedName;

		bool bFirst = true;

		for (void* RawPtr : RawData)
		{
			if (RawPtr == nullptr)
			{
				continue;
			}

			FMVVMBlueprintPropertyPath* PropertyPath = (FMVVMBlueprintPropertyPath*) RawPtr;
			if (bIsWidget)
			{
				if (bFirst)
				{
					SelectedName = PropertyPath->GetWidgetName();
				}
				else if (SelectedName != PropertyPath->GetWidgetName())
				{
					SelectedName = FName();
					break;
				}
			}
			else
			{
				if (bFirst)
				{
					SelectedGuid = PropertyPath->GetViewModelId();
				}
				else if (SelectedGuid != PropertyPath->GetViewModelId())
				{
					SelectedGuid = FGuid();
					break;
				}
			}

			bFirst = false;
		}

		if (bIsWidget)
		{
			return UE::MVVM::FBindingSource::CreateForWidget(WidgetBlueprint, SelectedName);
		}
		else
		{
			return UE::MVVM::FBindingSource::CreateForViewModel(WidgetBlueprint, SelectedGuid);
		}
	}

	TArray<FMVVMBlueprintPropertyPath> FPropertyPathCustomization::OnGetFields() const
	{
		UE::MVVM::FBindingSource Source = OnGetSelectedSource();

		TArray<FMVVMAvailableBinding> AvailableBindings;

		UClass* Class = Source.Class;
		UMVVMSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMVVMSubsystem>();
		if (bIsWidget)
		{
			AvailableBindings = Subsystem->GetWidgetAvailableBindings(Class);
		}
		else
		{
			AvailableBindings = Subsystem->GetViewModelAvailableBindings(Class);
		}

		TArray<FMVVMBlueprintPropertyPath> AvailablePaths;
		Algo::Transform(AvailableBindings, AvailablePaths, 
			[this, Class, Source](const FMVVMAvailableBinding& Binding)
			{
				FName BindingName = Binding.GetBindingName().ToName();

				UE::MVVM::FMVVMConstFieldVariant Variant;

				if (const UFunction* Function = Class->FindFunctionByName(BindingName))
				{
					Variant = UE::MVVM::FMVVMConstFieldVariant(Function);
				}
				else if (const FProperty* Property = Class->FindPropertyByName(BindingName))
				{
					Variant = UE::MVVM::FMVVMConstFieldVariant(Property);
				}

				FMVVMBlueprintPropertyPath Path;

				if (!Variant.IsEmpty())
				{
					if (bIsWidget)
					{
						Path.SetWidgetName(Source.Name);
					}
					else
					{
						Path.SetViewModelId(Source.ViewModelId);
					}

					Path.SetBasePropertyPath(Variant);
				}

				return Path;
			});

		return AvailablePaths;
	}

	FMVVMBlueprintPropertyPath FPropertyPathCustomization::OnGetSelectedField() const
	{
		TArray<void*> RawData;
		PropertyHandle->AccessRawData(RawData);

		FMVVMBlueprintPropertyPath SelectedField;

		bool bFirst = true;

		for (void* RawPtr : RawData)
		{
			if (RawPtr == nullptr)
			{
				continue;
			}

			FMVVMBlueprintPropertyPath* PropertyPath = (FMVVMBlueprintPropertyPath*) RawPtr;
			if (bFirst)
			{
				SelectedField = *PropertyPath;
			}
			else if (SelectedField != *PropertyPath)
			{
				SelectedField = FMVVMBlueprintPropertyPath();
				break;
			}
			bFirst = false;
		}

		return SelectedField;
	}
}

#undef LOCTEXT_NAMESPACE