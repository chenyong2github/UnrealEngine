// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMEditorSubsystem.h"

#include "Bindings/MVVMBindingHelper.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "Engine/Engine.h"
#include "MVVMSubsystem.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "ScopedTransaction.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "UMVVMEditorSubsystem"

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

TArray<UE::MVVM::FMVVMConstFieldVariant> UMVVMEditorSubsystem::GetChildViewModels(UClass* Class)
{

	auto IsValidObjectProperty = [](const FProperty* Property)
	{
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ObjectProperty->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass());
		}
		return false;
	};


	TArray<UE::MVVM::FMVVMConstFieldVariant> Result;
	for (TFieldIterator<const FProperty> PropertyIt(Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (IsValidObjectProperty(Property) && UE::MVVM::BindingHelper::IsValidForSourceBinding(Property))
		{
			Result.Add(UE::MVVM::FMVVMConstFieldVariant(Property));
		}
	}

	for (TFieldIterator<const UFunction> FunctionItt(Class, EFieldIteratorFlags::IncludeSuper); FunctionItt; ++FunctionItt)
	{
		const UFunction* Function = *FunctionItt;
		check(Function);
		if (!Function->HasAnyFunctionFlags(FUNC_Private | FUNC_Protected))
		{
			if (UE::MVVM::BindingHelper::IsValidForSourceBinding(Function))
			{
				if (const FProperty* ReturnValue = UE::MVVM::BindingHelper::GetReturnProperty(Function))
				{
					if (IsValidObjectProperty(ReturnValue))
					{
						Result.Add(UE::MVVM::FMVVMConstFieldVariant(Function));
					}
				}
			}
		}
	}

	return Result;
}

void UMVVMEditorSubsystem::RemoveViewModel(UWidgetBlueprint* WidgetBlueprint, FName ViewModel)
{
	if (UMVVMBlueprintView* View = GetView(WidgetBlueprint))
	{
		if (const FMVVMBlueprintViewModelContext* ViewModelContext = View->FindViewModel(ViewModel))
		{
			View->RemoveViewModel(ViewModelContext->GetViewModelId());
		}
	}
}

bool UMVVMEditorSubsystem::VerifyViewModelRename(UWidgetBlueprint* WidgetBlueprint, FName ViewModel, FName NewViewModel, FText& OutError)
{
	FKismetNameValidator Validator(WidgetBlueprint);
	EValidatorResult ValidatorResult = Validator.IsValid(NewViewModel);
	if (ValidatorResult != EValidatorResult::Ok)
	{
		if (ViewModel == NewViewModel && (ValidatorResult == EValidatorResult::AlreadyInUse || ValidatorResult == EValidatorResult::ExistingName))
		{
			// Continue successfully
		}
		else
		{
			OutError = INameValidatorInterface::GetErrorText(NewViewModel.ToString(), ValidatorResult);
			return false;
		}
	}
	return true;
}

bool UMVVMEditorSubsystem::RenameViewModel(UWidgetBlueprint* WidgetBlueprint, FName ViewModel, FName NewViewModel, FText& OutError)
{
	if (!VerifyViewModelRename(WidgetBlueprint, ViewModel, NewViewModel, OutError))
	{
		return false;
	}

	UMVVMBlueprintView* View = GetView(WidgetBlueprint);
	if (View == nullptr)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameViewModel", "Rename viewmodel"));
	View->Modify();
	return View->RenameViewModel(ViewModel, NewViewModel);
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

TArray<const UFunction*> UMVVMEditorSubsystem::GetAvailableConversionFunctions(const UE::MVVM::FMVVMConstFieldVariant& Source, const UE::MVVM::FMVVMConstFieldVariant& Destination) const
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
					TValueOrError<UE::MVVM::BindingHelper::FConversionFunctionArguments, FString> FunctionProperties = UE::MVVM::BindingHelper::TryGetPropertyTypeForConversionFunction(Function);
					if (FunctionProperties.HasError())
					{
						continue;
					}

					const FProperty* ArgumentProperty = FunctionProperties.GetValue().ArgumentProperty;
					const FProperty* ReturnProperty = FunctionProperties.GetValue().ReturnProperty;

					const FProperty* SourceProperty = Source.IsProperty() ? Source.GetProperty() : UE::MVVM::BindingHelper::GetReturnProperty(Source.GetFunction());
					const FProperty* DestinationProperty = Destination.IsProperty() ? Destination.GetProperty() : UE::MVVM::BindingHelper::GetFirstArgumentProperty(Destination.GetFunction());

					// check that the source -> argument is valid
					if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, ArgumentProperty))
					{
						continue;
					}
					
					// check that the return -> dest is valid
					if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(ReturnProperty, DestinationProperty))
					{
						continue;
					}

					ConversionFunctions.Add(Function);
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

#undef LOCTEXT_NAMESPACE
