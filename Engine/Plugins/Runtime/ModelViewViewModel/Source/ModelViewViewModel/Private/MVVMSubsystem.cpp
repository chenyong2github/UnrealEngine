// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMSubsystem.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Blueprint/WidgetTree.h"
#include "View/MVVMView.h"
#include "MVVMViewModelBase.h"
#include "Types/MVVMViewModelCollection.h"


void UMVVMSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	GlobalViewModelCollection = NewObject<UMVVMViewModelCollectionObject>(this);
}


void UMVVMSubsystem::Deinitialize()
{
	GlobalViewModelCollection = nullptr;
	Super::Deinitialize();
}


UMVVMView* UMVVMSubsystem::GetViewFromUserWidget(const UUserWidget* UserWidget) const
{
	return UserWidget ? UserWidget->GetExtension<UMVVMView>() : nullptr;
}


bool UMVVMSubsystem::IsViewModelValueValidForSourceBinding(const UMVVMViewModelBase* ViewModel, FMVVMBindingName ViewModelPropertyOrFunctionName) const
{
	if (ViewModel)
	{
		UE::MVVM::FMVVMFieldVariant Binding = UE::MVVM::BindingHelper::FindFieldByName(ViewModel->GetClass(), ViewModelPropertyOrFunctionName);
		if (Binding.IsFunction())
		{
			return UE::MVVM::BindingHelper::IsValidForSourceBinding(Binding.GetFunction());
		}
		if (Binding.IsProperty())
		{
			return UE::MVVM::BindingHelper::IsValidForSourceBinding(Binding.GetProperty());
		}
	}
	return false;
}


bool UMVVMSubsystem::IsViewModelValueValidForDestinationBinding(const UMVVMViewModelBase* ViewModel, FMVVMBindingName ViewModelPropertyOrFunctionName) const
{
	if (ViewModel)
	{
		UE::MVVM::FMVVMFieldVariant Binding = UE::MVVM::BindingHelper::FindFieldByName(ViewModel->GetClass(), ViewModelPropertyOrFunctionName);
		if (Binding.IsFunction())
		{
			return UE::MVVM::BindingHelper::IsValidForDestinationBinding(Binding.GetFunction());
		}
		if (Binding.IsProperty())
		{
			return UE::MVVM::BindingHelper::IsValidForDestinationBinding(Binding.GetProperty());
		}
	}
	return false;
}


bool UMVVMSubsystem::IsViewValueValidForSourceBinding(const UWidget* View, FMVVMBindingName ViewPropertyOrFunctionName) const
{
	if (View)
	{
		UE::MVVM::FMVVMFieldVariant Binding = UE::MVVM::BindingHelper::FindFieldByName(View->GetClass(), ViewPropertyOrFunctionName);
		if (Binding.IsFunction())
		{
			return UE::MVVM::BindingHelper::IsValidForSourceBinding(Binding.GetFunction());
		}
		if (Binding.IsProperty())
		{
			return UE::MVVM::BindingHelper::IsValidForSourceBinding(Binding.GetProperty());
		}
	}
	return false;
}


bool UMVVMSubsystem::IsViewValueValidForDestinationBinding(const UWidget* View, FMVVMBindingName ViewPropertyOrFunctionName) const
{
	if (View)
	{
		UE::MVVM::FMVVMFieldVariant Binding = UE::MVVM::BindingHelper::FindFieldByName(View->GetClass(), ViewPropertyOrFunctionName);
		if (Binding.IsFunction())
		{
			return UE::MVVM::BindingHelper::IsValidForDestinationBinding(Binding.GetFunction());
		}
		if (Binding.IsProperty())
		{
			return UE::MVVM::BindingHelper::IsValidForDestinationBinding(Binding.GetProperty());
		}
	}
	return false;
}


bool UMVVMSubsystem::DoesWidgetTreeContainedWidget(const UWidgetTree* WidgetTree, const UWidget* ViewWidget) const
{
	// Test if the View's Widget is valid.
	if (WidgetTree != nullptr && ViewWidget != nullptr)
	{
		TArray<UWidget*> Widgets;
		WidgetTree->GetAllWidgets(Widgets);
		return Widgets.Contains(ViewWidget);
	}
	return false;
}


namespace UE::MVVM::Private
{
	TArray<FMVVMAvailableBinding> GetAvailableBindings(const UClass* Class, const UE::FieldNotification::IClassDescriptor& ClassDescriptor)
	{
		TSet<FName> FieldDescriptors;
		ClassDescriptor.ForEachField(Class, [&FieldDescriptors](UE::FieldNotification::FFieldId FieldId)
			{
				FieldDescriptors.Add(FieldId.GetName());
				return true;
			});

		TArray<FMVVMAvailableBinding> Result;
		Result.Reserve(FieldDescriptors.Num());

		for (TFieldIterator<const UFunction> FunctionIt(Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
		{
			const UFunction* Function = *FunctionIt;
			check(Function);
			const bool bIsReadable = FieldDescriptors.Contains(Function->GetFName()) && BindingHelper::IsValidForSourceBinding(Function);
			const bool bIsWritable = BindingHelper::IsValidForDestinationBinding(Function);
			if (bIsReadable || bIsWritable)
			{
				Result.Add(FMVVMAvailableBinding(FMVVMBindingName(Function->GetFName()), bIsReadable, bIsWritable));
			}
		}

#if WITH_EDITOR
		FName NAME_BlueprintGetter = "BlueprintGetter";
		FName NAME_BlueprintSetter = "BlueprintSetter";
#endif

		// Remove BP getters / setters and replace them with their property for bindings
		for (TFieldIterator<const FProperty> PropertyIt(Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const bool bIsReadable = FieldDescriptors.Contains(Property->GetFName()) && BindingHelper::IsValidForSourceBinding(Property);
			const bool bIsWritable = BindingHelper::IsValidForDestinationBinding(Property);
			if (bIsReadable || bIsWritable)
			{
#if WITH_EDITOR
				if (bIsReadable)
				{
					const FString& PropertyGetter = Property->GetMetaData(NAME_BlueprintGetter);
					if (!PropertyGetter.IsEmpty())
					{
						FMVVMBindingName BindingName = FMVVMBindingName(*PropertyGetter);
						int32 BindingIndex = Result.IndexOfByPredicate([BindingName](const FMVVMAvailableBinding& Binding) { return Binding.GetBindingName() == BindingName && Binding.IsReadable() && !Binding.IsWritable(); });
						if (BindingIndex != INDEX_NONE)
						{
							Result.RemoveAtSwap(BindingIndex);
						}
					}
				}
				if (bIsWritable)
				{
					const FString& PropertySetter = Property->GetMetaData(NAME_BlueprintSetter);
					if (!PropertySetter.IsEmpty())
					{
						FMVVMBindingName BindingName = FMVVMBindingName(*PropertySetter);
						int32 BindingIndex = Result.IndexOfByPredicate([BindingName](const FMVVMAvailableBinding& Binding) { return Binding.GetBindingName() == BindingName && Binding.IsWritable() && !Binding.IsReadable(); });
						if (BindingIndex != INDEX_NONE)
						{
							Result.RemoveAtSwap(BindingIndex);
						}
					}
				}
#endif
				Result.Add(FMVVMAvailableBinding(FMVVMBindingName(Property->GetFName()), bIsReadable, bIsWritable));
			}
		}

		return Result;
	}

	TArray<FMVVMAvailableBinding> GetAvailableBindings(TSubclassOf<UObject> InSubClass)
	{
		if (InSubClass.Get() && InSubClass.Get()->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
		{
			TScriptInterface<INotifyFieldValueChanged> DefaultObject = InSubClass.GetDefaultObject();
			const UE::FieldNotification::IClassDescriptor& ClassDescriptor = DefaultObject->GetFieldNotificationDescriptor();
			return GetAvailableBindings(InSubClass.Get(), ClassDescriptor);
		}

		return TArray<FMVVMAvailableBinding>();
	}
} //namespace


TArray<FMVVMAvailableBinding> UMVVMSubsystem::GetViewModelAvailableBindings(TSubclassOf<UMVVMViewModelBase> ViewModelClass) const
{
	return UE::MVVM::Private::GetAvailableBindings(ViewModelClass);
}


TArray<FMVVMAvailableBinding> UMVVMSubsystem::GetWidgetAvailableBindings(TSubclassOf<UWidget> WidgetClass) const
{
	return UE::MVVM::Private::GetAvailableBindings(WidgetClass);
}


TArray<FMVVMAvailableBinding> UMVVMSubsystem::GetAvailableBindings(TSubclassOf<UObject> Class) const
{
	return UE::MVVM::Private::GetAvailableBindings(Class);
}


TValueOrError<bool, FString> UMVVMSubsystem::IsBindingValid(FBindingArgs Args) const
{
	if (UE::MVVM::IsForwardBinding(Args.Mode))
	{
		TValueOrError<bool, FString> ForwardDirection = IsBindingValid(Args.ForwardArgs);
		if (ForwardDirection.HasError())
		{
			return ForwardDirection;
		}
	}

	if (UE::MVVM::IsBackwardBinding(Args.Mode))
	{
		TValueOrError<bool, FString> BackwardDirection = IsBindingValid(Args.BackwardArgs);
		if (BackwardDirection.HasError())
		{
			return BackwardDirection;
		}
	}

	return MakeValue(true);
}

TValueOrError<bool, FString> UMVVMSubsystem::IsBindingValid(FDirectionalBindingArgs Args) const
{
	return IsBindingValid(Args.ToConst());
}

TValueOrError<bool, FString> UMVVMSubsystem::IsBindingValid(FConstDirectionalBindingArgs Args) const
{
	// Test Source
	TValueOrError<const FProperty*, FString> SourceResult = UE::MVVM::BindingHelper::TryGetPropertyTypeForSourceBinding(Args.SourceBinding);
	if (SourceResult.HasError())
	{
		return MakeError(SourceResult.StealError());
	}

	const FProperty* SourceProperty = SourceResult.StealValue();
	if (SourceProperty == nullptr)
	{
		return MakeError(TEXT("There is no value to bind at the source."));
	}

	// Test Destination
	TValueOrError<const FProperty*, FString> DestinationResult = UE::MVVM::BindingHelper::TryGetPropertyTypeForDestinationBinding(Args.DestinationBinding);
	if (DestinationResult.HasError())
	{
		return MakeError(DestinationResult.StealError());
	}

	const FProperty* DestinationProperty = DestinationResult.StealValue();
	if (DestinationProperty == nullptr)
	{
		return MakeError(TEXT("There is no value to bind at the destination."));
	}

	// Test the conversion function
	if (Args.ConversionFunction)
	{
		TValueOrError<const FProperty*, FString> ReturnResult = UE::MVVM::BindingHelper::TryGetReturnTypeForConversionFunction(Args.ConversionFunction);
		if (ReturnResult.HasError())
		{
			return MakeError(ReturnResult.StealError());
		}

		TValueOrError<TArray<const FProperty*>, FString> ArgumentsResult = UE::MVVM::BindingHelper::TryGetArgumentsForConversionFunction(Args.ConversionFunction);
		if (ArgumentsResult.HasError())
		{
			return MakeError(ArgumentsResult.StealError());
		}

		// The compiled version should look like Setter(Conversion(Getter())).
		const FProperty* ReturnProperty = ReturnResult.GetValue();
		if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(ReturnProperty, DestinationProperty))
		{
			return MakeError(FString::Printf(TEXT("The destination property '%s' (%s) does not match the return type of the conversion function (%s)."), *DestinationProperty->GetName(), *DestinationProperty->GetCPPType(), *ReturnProperty->GetCPPType()));
		}

		bool bAnyCompatible = false;

		const TArray<const FProperty*>& ArgumentProperties = ArgumentsResult.GetValue();
		for (const FProperty* ArgumentProperty : ArgumentProperties)
		{
			if (UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, ArgumentProperty))
			{
				bAnyCompatible = true;
				break;
			}
		}

		if (!bAnyCompatible)
		{
			FString ArgumentsString = FString::JoinBy(ArgumentProperties, TEXT(", "), [](const FProperty* Property) { return Property->GetCPPType(); });

			return MakeError(FString::Printf(TEXT("The source property '%s' (%s) does not match any of the argument types of the conversion function (%s)."), *SourceProperty->GetName(), *SourceProperty->GetCPPType(), *ArgumentsString));
		}
	}
	else if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, DestinationProperty))
	{
		return MakeError(FString::Printf(TEXT("The source property '%s' (%s) does not match the type of the destination property '%s' (%s). A conversion function is required."), *SourceProperty->GetName(), *SourceProperty->GetCPPType(), *DestinationProperty->GetName(), *DestinationProperty->GetCPPType()));
	}

	return MakeValue(true);
}
