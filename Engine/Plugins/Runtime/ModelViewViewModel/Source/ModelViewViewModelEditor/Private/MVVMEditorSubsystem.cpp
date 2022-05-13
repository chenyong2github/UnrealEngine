// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMEditorSubsystem.h"

#include "Bindings/MVVMBindingHelper.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "WidgetBlueprint.h"

UMVVMBlueprintView* UMVVMEditorSubsystem::RequestView(UWidgetBlueprint* WidgetBlueprint) const
{
	return UMVVMWidgetBlueprintExtension_View::RequestExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint)->GetBlueprintView();
}

UMVVMBlueprintView* UMVVMEditorSubsystem::GetView(UWidgetBlueprint* WidgetBlueprint) const
{
	if (UMVVMWidgetBlueprintExtension_View* ExtensionView = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint))
	{
		return ExtensionView->GetBlueprintView();
	}
	return nullptr;
}

TArray<UE::MVVM::FMVVMConstFieldVariant> UMVVMEditorSubsystem::GetChildViewModels(TSubclassOf<UMVVMViewModelBase> Class)
{
	TArray<UE::MVVM::FMVVMConstFieldVariant> Result;

	for (TFieldIterator<const FProperty> PropertyIt(Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (ObjectProperty->PropertyClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
			{
				if (UE::MVVM::BindingHelper::IsValidForSourceBinding(Property))
				{
					Result.Add(UE::MVVM::FMVVMConstFieldVariant(Property));
				}
			}
		}
	}

	return Result;
}

void UMVVMEditorSubsystem::RemoveViewModel(UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewModelContext& ViewModel)
{
	if (UMVVMBlueprintView* View = GetView(WidgetBlueprint))
	{
		View->RemoveViewModel(ViewModel.GetViewModelId());
	}
}

FMVVMBlueprintViewBinding& UMVVMEditorSubsystem::AddBinding(UWidgetBlueprint* WidgetBlueprint)
{
	UMVVMBlueprintView* View = RequestView(WidgetBlueprint);
	return View->AddDefaultBinding();
}

void UMVVMEditorSubsystem::RemoveBinding(UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding)
{
	if (UMVVMBlueprintView* View = GetView(WidgetBlueprint))
	{
		View->RemoveBinding(&Binding);
	}
}

bool UMVVMEditorSubsystem::IsValidConversionFunction(const UFunction* Function, const UE::MVVM::FMVVMConstFieldVariant& Source, const UE::MVVM::FMVVMConstFieldVariant& Destination) const
{
	if (Source.IsEmpty() || Destination.IsEmpty())
	{
		return false;
	}

	TValueOrError<const FProperty*, FString> ReturnResult = UE::MVVM::BindingHelper::TryGetReturnTypeForConversionFunction(Function);
	if (ReturnResult.HasError())
	{
		return false;
	}

	TValueOrError<TArray<const FProperty*>, FString> ArgumentsResult = UE::MVVM::BindingHelper::TryGetArgumentsForConversionFunction(Function);
	if (ArgumentsResult.HasError())
	{
		return false;
	}

	const FProperty* ReturnProperty = ReturnResult.GetValue();
	const FProperty* SourceProperty = Source.IsProperty() ? Source.GetProperty() : UE::MVVM::BindingHelper::GetReturnProperty(Source.GetFunction());
	const FProperty* DestinationProperty = Destination.IsProperty() ? Destination.GetProperty() : UE::MVVM::BindingHelper::GetFirstArgumentProperty(Destination.GetFunction());

	// check that at least one source -> argument binding is compatible
	bool bAnyCompatible = false;

	const TArray<const FProperty*>& ConversionArgProperties = ArgumentsResult.GetValue();
	for (const FProperty* ArgumentProperty : ConversionArgProperties)
	{
		if (ArgumentProperty->IsA<FObjectProperty>())
		{
			// filter out any functions with UObject properties - they aren't valid conversion functions
			return false;
		}

		if (UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, ArgumentProperty))
		{
			bAnyCompatible = true;
		}
	}

	if (!bAnyCompatible)
	{
		return false;
	}

	// check that the return -> dest is valid
	if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(ReturnProperty, DestinationProperty))
	{
		return false;
	}

	return true;
}

TArray<const UFunction*> UMVVMEditorSubsystem::GetAvailableConversionFunctions(const UE::MVVM::FMVVMConstFieldVariant& Source, const UE::MVVM::FMVVMConstFieldVariant& Destination, const UWidgetBlueprint* WidgetBlueprint) const
{
	TArray<const UFunction*> ConversionFunctions;

	if (Source.IsEmpty() || Destination.IsEmpty())
	{
		return ConversionFunctions;
	}

	const UMVVMSubsystem* Subsystem = GEngine->GetEngineSubsystem<UMVVMSubsystem>();

	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
	const FBlueprintActionDatabase::FActionRegistry& ActionRegistry = ActionDatabase.GetAllActions();

	for (auto It = ActionRegistry.CreateConstIterator(); It; ++It)
	{
		if (UObject* ActionObject = It->Key.ResolveObjectPtr())
		{
			for (const UBlueprintNodeSpawner* NodeSpawner : It->Value)
			{
				FBlueprintActionInfo BlueprintAction(ActionObject, NodeSpawner);
				const UFunction* Function = BlueprintAction.GetAssociatedFunction();
				if (Function != nullptr)
				{
					// functions in the widget blueprint can do anything they want, other functions have to be static functions in a BlueprintFunctionLibrary
					const UClass* FunctionClass = Function->GetOuterUClass();
					if ((FunctionClass->ClassGeneratedBy == WidgetBlueprint) ||
						(FunctionClass->IsChildOf<UBlueprintFunctionLibrary>() && Function->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintPure)))
					{
						if (IsValidConversionFunction(Function, Destination, Source))
						{
							ConversionFunctions.Add(Function);
						}
					}
				}
			}
		}
	}

	ConversionFunctions.Sort([](const UFunction& A, const UFunction& B) -> bool
		{
			return A.GetFName().LexicalLess(B.GetFName());
		});
	return ConversionFunctions;
}
