// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMPropertyPathHelpers.h"

#include "Algo/Transform.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "MVVMBlueprintView.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"

namespace UE::MVVM
{
	void FWidgetFieldPathHelper::GetAvailableSources(TSet<FBindingSource>& OutSources) const
	{
		FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		const UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
		if (WidgetTree == nullptr)
		{
			return;
		}

		TArray<UWidget*> AllWidgets;
		WidgetTree->GetAllWidgets(AllWidgets);

		OutSources.Reserve(AllWidgets.Num() + 1);

		// Add current widget as a possible binding source
		if (UClass* BPClass = WidgetBlueprint->GeneratedClass)
		{
			TArray<FMVVMAvailableBinding> Bindings = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetWidgetAvailableBindings(BPClass);

			if (Bindings.Num() > 0)
			{
				// at least one valid property, add it to our list
				FBindingSource Source;
				Source.Name = WidgetBlueprint->GetFName();
				Source.DisplayName = FText::FromName(WidgetBlueprint->GetFName());
				Source.Class = BPClass;
				Source.IsSelected = Source.Name == Path->WidgetName;
				OutSources.Add(Source);
			}
		}

		for (const UWidget* Widget : AllWidgets)
		{
			TArray<FMVVMAvailableBinding> Bindings = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetWidgetAvailableBindings(Widget->GetClass());

			if (Bindings.Num() > 0)
			{
				// at least one valid property, add it to our list
				FBindingSource Source;
				Source.Name = Widget->GetFName();
				Source.DisplayName = Widget->GetLabelText();
				Source.Class = Widget->GetClass();
				Source.IsSelected = Source.Name == Path->WidgetName;
				OutSources.Add(Source);
			}
		}
	}

	FBindingSource FWidgetFieldPathHelper::GetSelectedSource() const
	{
		TSet<FBindingSource> Sources;
		GetAvailableSources(Sources);

		for (const FBindingSource& Source : Sources)
		{
			if (Source.IsSelected)
			{
				return Source;
			}
		}

		return FBindingSource();
	}

	void FWidgetFieldPathHelper::GetAvailableFields(TSet<FMVVMConstFieldVariant>& OutFields) const
	{
		FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		const UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
		if (WidgetTree == nullptr)
		{
			return;
		}

		TSubclassOf<UWidget> WidgetClass = nullptr;
		if (const UWidget* Widget = WidgetTree->FindWidget(Path->WidgetName))
		{
			WidgetClass = Widget->GetClass();
		}
		else if (WidgetBlueprint->GetFName() == Path->WidgetName)
		{
			WidgetClass = WidgetBlueprint->GeneratedClass;
		}

		if (!WidgetClass)
		{
			return;
		}

		TArray<FMVVMAvailableBinding> Bindings = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetWidgetAvailableBindings(WidgetClass);
		Algo::Transform(Bindings, OutFields, [WidgetClass](const FMVVMAvailableBinding& Binding)
			{
				return UE::MVVM::BindingHelper::FindFieldByName(WidgetClass, Binding.GetBindingName());
			});
	}

	FMVVMConstFieldVariant FWidgetFieldPathHelper::GetSelectedField() const
	{
		FMVVMBindingName BindingName = GetBindingName();

		TSet<FMVVMConstFieldVariant> Fields;
		GetAvailableFields(Fields);

		for (const FMVVMConstFieldVariant& Field : Fields)
		{
			if (Field.GetName() == BindingName.ToName())
			{
				return Field;
			}
		}

		return FMVVMConstFieldVariant();
	}

	void FWidgetFieldPathHelper::SetSelectedSource(const FBindingSource& Source) const
	{
		FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		if (Source.IsValid())
		{
			Path->WidgetName = Source.Name;
		}
		else
		{
			Path->WidgetName = FName();
		}
	}

	FMVVMBindingName FWidgetFieldPathHelper::GetBindingName() const
	{
		if (FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr))
		{
			TArray<FName> Paths = Path->GetPaths();
			if (Paths.Num() > 0)
			{
				return FMVVMBindingName(Paths[0]);
			}
		}

		return FMVVMBindingName();
	}

	void FWidgetFieldPathHelper::SetBindingReference(const UE::MVVM::FMVVMFieldVariant& InField) const
	{
		if (FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->SetBasePropertyPath(InField);
		}
	}

	void FWidgetFieldPathHelper::SetBindingReference(const UE::MVVM::FMVVMConstFieldVariant& InField) const
	{
		if (FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->SetBasePropertyPath(InField);
		}
	}

	void FWidgetFieldPathHelper::ResetBinding() const
	{
		if (FMVVMWidgetPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->ResetBasePropertyPath();
		}
	}

	void FViewModelFieldPathHelper::GetAvailableSources(TSet<FBindingSource>& OutSources) const
	{
		FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		const UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
		const UMVVMBlueprintView* MVVMBlueprintView = MVVMExtensionPtr->GetBlueprintView();
		const TArrayView<const FMVVMBlueprintViewModelContext>& ViewModels = MVVMBlueprintView->GetViewModels();

		OutSources.Reserve(ViewModels.Num());

		for (const FMVVMBlueprintViewModelContext& ViewModel : ViewModels)
		{
			FBindingSource Source;
			Source.SourceGuid = ViewModel.GetViewModelId();
			Source.DisplayName = ViewModel.GetDisplayName();
			Source.Class = ViewModel.GetViewModelClass().Get();
			Source.IsSelected = (Source.SourceGuid == Path->ContextId);

			OutSources.Add(Source);
		}
	}

	FBindingSource FViewModelFieldPathHelper::GetSelectedSource() const
	{
		TSet<FBindingSource> Sources;
		GetAvailableSources(Sources);

		for (const FBindingSource& Source : Sources)
		{
			if (Source.IsSelected)
			{
				return Source;
			}
		}

		return FBindingSource();
	}

	void FViewModelFieldPathHelper::GetAvailableFields(TSet<FMVVMConstFieldVariant>& OutFields) const
	{
		FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		const UMVVMWidgetBlueprintExtension_View* MVVMExtensionPtr = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
		const UMVVMBlueprintView* MVVMBlueprintView = MVVMExtensionPtr->GetBlueprintView();

		const FMVVMBlueprintViewModelContext* ViewModelContext = MVVMBlueprintView->FindViewModel(Path->ContextId);
		if (ViewModelContext == nullptr)
		{
			return;
		}

		TArray<FMVVMAvailableBinding> Bindings = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetViewModelAvailableBindings(ViewModelContext->GetViewModelClass());
		Algo::Transform(Bindings, OutFields, [ViewModelContext](const FMVVMAvailableBinding& Binding)
			{
				return UE::MVVM::BindingHelper::FindFieldByName(ViewModelContext->GetViewModelClass(), Binding.GetBindingName());
			});
	}

	FMVVMConstFieldVariant FViewModelFieldPathHelper::GetSelectedField() const
	{
		FMVVMBindingName BindingName = GetBindingName();

		TSet<FMVVMConstFieldVariant> Fields;
		GetAvailableFields(Fields);

		for (const FMVVMConstFieldVariant& Field : Fields)
		{
			if (Field.GetName() == BindingName.ToName())
			{
				return Field;
			}
		}

		return FMVVMConstFieldVariant();
	}

	void FViewModelFieldPathHelper::SetSelectedSource(const FBindingSource& Source) const
	{
		FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr);
		if (Path == nullptr)
		{
			return;
		}

		if (Source.IsValid())
		{
			Path->ContextId = Source.SourceGuid;
		}
		else
		{
			Path->ContextId = FGuid();
		}
	}

	FMVVMBindingName FViewModelFieldPathHelper::GetBindingName() const
	{
		if (FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr))
		{
			TArray<FName> Paths = Path->GetPaths();
			if (Paths.Num() > 0)
			{
				return FMVVMBindingName(Paths[0]);
			}
		}

		return FMVVMBindingName();
	}

	void FViewModelFieldPathHelper::SetBindingReference(const UE::MVVM::FMVVMFieldVariant& InField) const
	{
		if (FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->SetBasePropertyPath(InField);
		}
	}

	void FViewModelFieldPathHelper::SetBindingReference(const UE::MVVM::FMVVMConstFieldVariant& InField) const
	{
		if (FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->SetBasePropertyPath(InField);
		}
	}

	void FViewModelFieldPathHelper::ResetBinding() const
	{
		if (FMVVMViewModelPropertyPath* Path = PathAttr.Get(nullptr))
		{
			Path->ResetBasePropertyPath();
		}
	}
}
