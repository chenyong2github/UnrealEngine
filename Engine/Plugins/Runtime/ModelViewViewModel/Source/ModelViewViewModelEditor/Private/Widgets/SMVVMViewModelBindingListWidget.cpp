// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMViewModelBindingListWidget.h"

#include "Algo/Transform.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "FieldNotification/IFieldValueChanged.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMFieldVariant.h"
#include "WidgetBlueprint.h"
#include "Widgets/PropertyViewer/SPropertyViewer.h"

#define LOCTEXT_NAMESPACE "SSourceBindingList"

using UE::PropertyViewer::SPropertyViewer;

namespace UE::MVVM
{

	FFieldIterator_Bindable::FFieldIterator_Bindable(const UWidgetBlueprint* InWidgetBlueprint, EFieldVisibility InVisibilityFlags)
	: WidgetBlueprint(InWidgetBlueprint)
	, FieldVisibilityFlags(InVisibilityFlags)
{ }


TArray<FFieldVariant> FFieldIterator_Bindable::GetFields(const UStruct* Struct) const
{
	TArray<FFieldVariant> Result;

	auto AddResult = [Flags = FieldVisibilityFlags, &Result, Struct](const TArray<FMVVMAvailableBinding>& AvailableBindingsList)
	{
		Result.Reserve(AvailableBindingsList.Num());
		for (const FMVVMAvailableBinding& Value : AvailableBindingsList)
		{
			if (EnumHasAllFlags(Flags, EFieldVisibility::Readable) && !Value.IsReadable())
			{
				continue;
			} 

			if (EnumHasAllFlags(Flags, EFieldVisibility::Writable) && !Value.IsWritable())
			{
				continue;
			}
			
			if (EnumHasAllFlags(Flags, EFieldVisibility::Notify) && !Value.HasNotify())
			{
				continue;
			}

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
			else if (A.IsUObject())
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
void SSourceBindingList::Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint)
{
	FieldIterator = MakeUnique<FFieldIterator_Bindable>(WidgetBlueprint, InArgs._FieldVisibilityFlags);

	OnDoubleClicked = InArgs._OnDoubleClicked;

	ChildSlot
	[
		SAssignNew(PropertyViewer, SPropertyViewer)
		.FieldIterator(FieldIterator.Get())
		.PropertyVisibility(SPropertyViewer::EPropertyVisibility::Hidden)
		.bShowFieldIcon(true)
		.bSanitizeName(true)
		.bShowSearchBox(InArgs._ShowSearchBox)
		.OnSelectionChanged(this, &SSourceBindingList::HandleSelectionChanged)
		.OnDoubleClicked(this, &SSourceBindingList::HandleDoubleClicked)
	];
}

void SSourceBindingList::Clear()
{
	Sources.Reset();
	if (PropertyViewer)
	{
		PropertyViewer->RemoveAll();
	}
}

void SSourceBindingList::AddSource(UClass* Class, FName Name, FGuid Guid)
{
	FBindingSource Source;
	Source.Class = Class;
	Source.Name = Name;
	Source.ViewModelId = Guid;

	AddSources(MakeArrayView(&Source, 1));
} 

void SSourceBindingList::AddWidgetBlueprint(const UWidgetBlueprint* InWidgetBlueprint)
{
	FBindingSource Source;
	Source.Class = InWidgetBlueprint->GeneratedClass;
	Source.Name = InWidgetBlueprint->GetFName();
	Source.DisplayName = FText::FromString(InWidgetBlueprint->GetName());

	AddSources(MakeArrayView(&Source, 1 ));
}

void SSourceBindingList::AddWidgets(TArrayView<const UWidget*> InWidgets)
{
	TArray<FBindingSource, TInlineAllocator<16>> NewSources;
	NewSources.Reserve(InWidgets.Num());

	for (const UWidget* Widget : InWidgets)
	{
		FBindingSource& Source = NewSources.AddDefaulted_GetRef();
		Source.Class = Widget->GetClass();
		Source.Name = Widget->GetFName();
		Source.DisplayName = Widget->GetLabelText();
	}

	AddSources(NewSources);
}

void SSourceBindingList::AddViewModels(TArrayView<const FMVVMBlueprintViewModelContext> InViewModels)
{
	TArray<FBindingSource, TInlineAllocator<16>> NewSources;
	NewSources.Reserve(InViewModels.Num());

	for (const FMVVMBlueprintViewModelContext& ViewModelContext : InViewModels)
	{
		FBindingSource& Source = NewSources.AddDefaulted_GetRef();
		Source.Class = ViewModelContext.GetViewModelClass();
		Source.ViewModelId = ViewModelContext.GetViewModelId();
	}

	AddSources(NewSources);
}

void SSourceBindingList::AddSources(TArrayView<const FBindingSource> InSources)
{
	Algo::Transform(InSources, Sources, [](const FBindingSource& Source)
		{
			return TPair<FBindingSource, SPropertyViewer::FHandle>(Source, SPropertyViewer::FHandle());
		});

	if (PropertyViewer)
	{
		for (TPair<FBindingSource, SPropertyViewer::FHandle>& Source : Sources)
		{
			const UClass* Class = Source.Key.Class;
			if (Class && Class->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
			{
				Source.Value = PropertyViewer->AddContainer(Class);
			}
		}
	}
}

FMVVMBlueprintPropertyPath SSourceBindingList::CreateBlueprintPropertyPath(SPropertyViewer::FHandle Handle, TArrayView<const FFieldVariant> FieldPath) const
{
	const TPair<FBindingSource, SPropertyViewer::FHandle>* Source = Sources.FindByPredicate(
		[Handle](const TPair<FBindingSource, SPropertyViewer::FHandle>& Source)
		{
			return Source.Value == Handle;
		});
		
	FMVVMBlueprintPropertyPath PropertyPath;
	if (Source != nullptr)
	{
		if (Source->Key.ViewModelId.IsValid())
		{
			PropertyPath.SetViewModelId(Source->Key.ViewModelId);
		}
		else
		{
			PropertyPath.SetWidgetName(Source->Key.Name);
		}

		PropertyPath.ResetBasePropertyPath();
	}

	// TODO (sebastiann): FMVVMBlueprintPropertyPath doesn't support long paths yet
	FFieldVariant LastField = FieldPath.Num() > 0 ? FieldPath.Last() : FFieldVariant();
	if (LastField.IsValid())
	{
		PropertyPath.SetBasePropertyPath(FMVVMConstFieldVariant(LastField));
	}

	return PropertyPath;
}

void SSourceBindingList::HandleSelectionChanged(SPropertyViewer::FHandle Handle, TArrayView<const FFieldVariant> FieldPath, ESelectInfo::Type SelectionType)
{
	SelectedPath = CreateBlueprintPropertyPath(Handle, FieldPath);
}

void SSourceBindingList::HandleDoubleClicked(SPropertyViewer::FHandle Handle, TArrayView<const FFieldVariant> FieldPath)
{
	if (OnDoubleClicked.IsBound())
	{
		FMVVMBlueprintPropertyPath ClickedPath = CreateBlueprintPropertyPath(Handle, FieldPath);
		OnDoubleClicked.Execute(ClickedPath);
	}
}

void SSourceBindingList::SetRawFilterText(const FText& InFilterText)
{
	if (PropertyViewer)
	{
		PropertyViewer->SetRawFilterText(InFilterText);
	}
}

FMVVMBlueprintPropertyPath SSourceBindingList::GetSelectedProperty() const
{
	return SelectedPath;
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE