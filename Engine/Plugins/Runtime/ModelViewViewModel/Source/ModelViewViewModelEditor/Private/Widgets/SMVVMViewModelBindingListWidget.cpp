// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMViewModelBindingListWidget.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMFieldVariant.h"
#include "WidgetBlueprint.h"
#include "Widgets/PropertyViewer/SPropertyViewer.h"

#define LOCTEXT_NAMESPACE "SViewModelBindingListWidget"

namespace UE::MVVM
{

/** */
FFieldIterator_ViewModel::FFieldIterator_ViewModel(const UWidgetBlueprint* InWidgetBlueprint)
	: WidgetBlueprint(InWidgetBlueprint)
{ }


TArray<FFieldVariant> FFieldIterator_ViewModel::GetFields(const UStruct* Struct) const
{
	TArray<FFieldVariant> Result;

	auto AddResult = [&Result, Struct](const TArray<FMVVMAvailableBinding>& AvailableBindingsList)
	{
		Result.Reserve(AvailableBindingsList.Num());
		for (const FMVVMAvailableBinding& Value : AvailableBindingsList)
		{
			FMVVMFieldVariant FieldVariant = BindingHelper::FindFieldByName(Struct, Value.GetBindingName());
			if (FieldVariant.IsFunction())
			{
				Result.Add(FFieldVariant(FieldVariant.GetFunction()));
			}
			else if (FieldVariant.IsProperty())
			{
				Result.Add(FFieldVariant(FieldVariant.GetProperty()));
			}
		}
	};


	if (const UClass* Class = Cast<const UClass>(Struct))
	{
		const UWidgetBlueprint* WidgetBlueprintPtr = WidgetBlueprint.Get();
		TSubclassOf<UObject> AccessorClass = WidgetBlueprintPtr ? WidgetBlueprintPtr->GeneratedClass : nullptr;
		AddResult(GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetAvailableBindings(const_cast<UClass*>(Class), AccessorClass));

	}
	else if (const UScriptStruct* ScriptStruct = Cast<const UScriptStruct>(Struct))
	{
		AddResult(GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetAvailableBindingsForStruct(ScriptStruct));
	}

	Result.Sort([](const FFieldVariant& A, const FFieldVariant& B)
		{
			bool bIsAViewModel = A.Get<FObjectPropertyBase>() && A.Get<FObjectPropertyBase>()->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass());
			bool bIsBViewModel = B.Get<FObjectPropertyBase>() && B.Get<FObjectPropertyBase>()->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass());
			if (A.IsUObject() && B.IsUObject())
			{
				return A.GetFName().LexicalLess(B.GetFName());
			}
			else if (bIsAViewModel && bIsBViewModel)
			{
				return A.GetFName().LexicalLess(B.GetFName());
			}
			else if (bIsAViewModel)
			{
				return true;
			}
			else if (bIsBViewModel)
			{
				return false;
			}
			else if(A.IsUObject())
			{
				return true;
			}
			else if (B.IsUObject())
			{
				return false;
			}
			return A.GetFName().LexicalLess(B.GetFName());
		});


	return Result;
}


/** */
void SViewModelBindingListWidget::Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint)
{
	ViewModelFieldIterator = MakeUnique<FFieldIterator_ViewModel>(WidgetBlueprint);

	PropertyViewer = SNew(UE::PropertyViewer::SPropertyViewer)
		.PropertyVisibility(UE::PropertyViewer::SPropertyViewer::EPropertyVisibility::Hidden)
		.bShowFieldIcon(true)
		.bSanitizeName(true)
		.FieldIterator(ViewModelFieldIterator.Get());

	if (InArgs._ViewModel.Class.Get())
	{
		SetViewModels(MakeArrayView(&InArgs._ViewModel, 1));
	}

	ChildSlot
	[
		PropertyViewer.ToSharedRef()
	];
}


void SViewModelBindingListWidget::SetViewModel(UClass* Class, FName Name, FGuid Guid)
{
	FViewModel ViewModel;
	ViewModel.Class = Class;
	ViewModel.ViewModelId = Guid;
	ViewModel.Name = Name;
	SetViewModels(MakeArrayView(&ViewModel, 1));
}


void SViewModelBindingListWidget::SetViewModels(TArrayView<const FViewModel> InViewModels)
{
	ViewModels = InViewModels;
	if (PropertyViewer)
	{
		PropertyViewer->RemoveAll();
		for (const FViewModel& ViewModel : ViewModels)
		{
			if (ViewModel.Class && ViewModel.Class->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
			{
				PropertyViewer->AddContainer(ViewModel.Class.Get());
			}
		}
	}
}


void SViewModelBindingListWidget::SetRawFilterText(const FText& InFilterText)
{
	if (PropertyViewer)
	{
		PropertyViewer->SetRawFilterText(InFilterText);
	}
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE