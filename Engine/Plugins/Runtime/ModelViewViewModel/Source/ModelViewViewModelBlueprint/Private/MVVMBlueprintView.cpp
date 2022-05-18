// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintView.h"

#include "MVVMWidgetBlueprintExtension_View.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"


FMVVMBlueprintViewModelContext* UMVVMBlueprintView::FindViewModel(FGuid ViewModelId)
{
	return AvailableViewModels.FindByPredicate([ViewModelId](const FMVVMBlueprintViewModelContext& Other)
		{
			return Other.GetViewModelId() == ViewModelId;
		});
}

const FMVVMBlueprintViewModelContext* UMVVMBlueprintView::FindViewModel(FGuid ViewModelId) const
{
	return const_cast<UMVVMBlueprintView*>(this)->FindViewModel(ViewModelId);
}

void UMVVMBlueprintView::AddViewModel(const FMVVMBlueprintViewModelContext& NewContext)
{
	AvailableViewModels.Add(NewContext);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetOuterUMVVMWidgetBlueprintExtension_View()->GetWidgetBlueprint());
	OnViewModelsUpdated.Broadcast();
}


void UMVVMBlueprintView::RemoveViewModel(FGuid ViewModelId)
{
	int32 Count = AvailableViewModels.RemoveAll([ViewModelId](const FMVVMBlueprintViewModelContext& VM)
		{
			return VM.GetViewModelId() == ViewModelId;
		});

	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetOuterUMVVMWidgetBlueprintExtension_View()->GetWidgetBlueprint());
		OnViewModelsUpdated.Broadcast();
	}
}

void UMVVMBlueprintView::RemoveViewModels(const TArrayView<FGuid> ViewModelIds)
{
	int32 Count = 0;
	for (const FGuid& ViewModelId : ViewModelIds)
	{
		Count += AvailableViewModels.RemoveAll([ViewModelId](const FMVVMBlueprintViewModelContext& VM)
			{
				return VM.GetViewModelId() == ViewModelId;
			});
	}

	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetOuterUMVVMWidgetBlueprintExtension_View()->GetWidgetBlueprint());
		OnViewModelsUpdated.Broadcast();
	}
}

void UMVVMBlueprintView::SetViewModels(const TArray<FMVVMBlueprintViewModelContext>& ViewModelContexts)
{
	AvailableViewModels = ViewModelContexts;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetOuterUMVVMWidgetBlueprintExtension_View()->GetWidgetBlueprint());
	OnViewModelsUpdated.Broadcast();
}

const FMVVMBlueprintViewBinding* UMVVMBlueprintView::FindBinding(const UWidget* Widget, const FProperty* Property) const
{
	return const_cast<UMVVMBlueprintView*>(this)->FindBinding(Widget, Property);
}

FMVVMBlueprintViewBinding* UMVVMBlueprintView::FindBinding(const UWidget* Widget, const FProperty* Property)
{
	FName WidgetName = Widget->GetFName();
	return Bindings.FindByPredicate([WidgetName, Property](const FMVVMBlueprintViewBinding& Binding)
		{
			return Binding.WidgetPath.WidgetName == WidgetName &&
				Binding.WidgetPath.BasePropertyPathContains(UE::MVVM::FMVVMConstFieldVariant(Property));
		});
}

void UMVVMBlueprintView::RemoveBindingAt(int32 Index)
{
	Bindings.RemoveAt(Index);
	OnBindingsUpdated.Broadcast();
}

void UMVVMBlueprintView::RemoveBinding(const FMVVMBlueprintViewBinding* Binding)
{
	int32 Index = 0;
	for (; Index < Bindings.Num(); ++Index)
	{
		if (&Bindings[Index] == Binding)
		{
			break;
		}
	}

	Bindings.RemoveAt(Index);
	OnBindingsUpdated.Broadcast();
}

FMVVMBlueprintViewBinding& UMVVMBlueprintView::AddBinding(const UWidget* Widget, const FProperty* Property)
{
	FMVVMBlueprintViewBinding& NewBinding = Bindings.AddDefaulted_GetRef();
	NewBinding.WidgetPath.WidgetName = Widget->GetFName();
	NewBinding.WidgetPath.SetBasePropertyPath(UE::MVVM::FMVVMConstFieldVariant(Property));

	OnBindingsUpdated.Broadcast();
	return NewBinding;
}

FMVVMBlueprintViewBinding& UMVVMBlueprintView::AddDefaultBinding()
{
	FMVVMBlueprintViewBinding& NewBinding = Bindings.AddDefaulted_GetRef();

	OnBindingsUpdated.Broadcast();
	return NewBinding;
}

FMVVMBlueprintViewBinding* UMVVMBlueprintView::GetBindingAt(int32 Index)
{
	if (Bindings.IsValidIndex(Index))
	{
		return &Bindings[Index];
	}
	return nullptr;
}

const FMVVMBlueprintViewBinding* UMVVMBlueprintView::GetBindingAt(int32 Index) const
{
	if (Bindings.IsValidIndex(Index))
	{
		return &Bindings[Index];
	}
	return nullptr;
}

#if WITH_EDITOR
void UMVVMBlueprintView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMVVMBlueprintView, Bindings))
	{
		OnBindingsUpdated.Broadcast();
	}
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMVVMBlueprintView, AvailableViewModels))
	{
		OnViewModelsUpdated.Broadcast();
	}
}

void UMVVMBlueprintView::WidgetRenamed(FName OldObjectName, FName NewObjectName)
{
	bool bRenamed = false;
	for (FMVVMBlueprintViewBinding& Binding : Bindings)
	{
		if (Binding.WidgetPath.WidgetName == OldObjectName)
		{
			Binding.WidgetPath.WidgetName = NewObjectName;
			bRenamed = true;
		}
	}

	if (bRenamed)
	{
		OnBindingsUpdated.Broadcast();
	}
}
#endif
