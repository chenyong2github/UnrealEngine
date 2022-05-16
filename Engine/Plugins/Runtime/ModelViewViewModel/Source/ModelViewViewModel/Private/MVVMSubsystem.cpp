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

		for (TFieldIterator<const UFunction> FunctionItt(Class, EFieldIteratorFlags::IncludeSuper); FunctionItt; ++FunctionItt)
		{
			const UFunction* Function = *FunctionItt;
			check(Function);
			if (!Function->HasAnyFunctionFlags(FUNC_Private | FUNC_Protected))
			{
				const bool bIsReadable = FieldDescriptors.Contains(Function->GetFName()) && BindingHelper::IsValidForSourceBinding(Function);
				const bool bIsWritable = BindingHelper::IsValidForDestinationBinding(Function);
				if (bIsReadable || bIsWritable)
				{
					Result.Add(FMVVMAvailableBinding(FMVVMBindingName(Function->GetFName()), bIsReadable, bIsWritable));
				}
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

	template<typename TClassType>
	TArray<FMVVMAvailableBinding> GetAvailableBindings(TSubclassOf<TClassType> InSubClass)
	{
		if (InSubClass.Get() == nullptr)
		{
			return TArray<FMVVMAvailableBinding>();
		}
		const UE::FieldNotification::IClassDescriptor& ClassDescriptor = InSubClass.GetDefaultObject()->GetFieldNotificationDescriptor();
		return GetAvailableBindings(InSubClass.Get(), ClassDescriptor);
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
	TValueOrError<const FProperty*, FString> GetterProperty = UE::MVVM::BindingHelper::TryGetPropertyTypeForSourceBinding(Args.SourceBinding);
	if (GetterProperty.HasError())
	{
		return MakeError(GetterProperty.StealError());
	}

	const FProperty* SourceProperty = GetterProperty.StealValue();
	if (SourceProperty == nullptr)
	{
		return MakeError(TEXT("There is no value to bind at the source."));
	}

	// Test Destination
	TValueOrError<const FProperty*, FString> SetterProperty = UE::MVVM::BindingHelper::TryGetPropertyTypeForDestinationBinding(Args.DestinationBinding);
	if (SetterProperty.HasError())
	{
		return MakeError(SetterProperty.StealError());
	}

	const FProperty* DestinationProperty = SetterProperty.StealValue();
	if (DestinationProperty == nullptr)
	{
		return MakeError(TEXT("There is no value to bind at the destination."));
	}

	// Test the conversion function
	if (Args.ConversionFunction)
	{
		TValueOrError<UE::MVVM::BindingHelper::FConversionFunctionArguments, FString> ConversionResult = UE::MVVM::BindingHelper::TryGetPropertyTypeForConversionFunction(Args.ConversionFunction);
		if (ConversionResult.HasError())
		{
			return MakeError(ConversionResult.StealError());
		}

		// The compiled version should look like Setter(Conversion(Getter())).
		const FProperty* ConversionReturnProperty = ConversionResult.GetValue().ReturnProperty;
		if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(ConversionReturnProperty, DestinationProperty))
		{
			return MakeError(FString::Printf(TEXT("The destination property '%s' (%s) does not match the return type of the conversion function (%s)."), *DestinationProperty->GetName(), *DestinationProperty->GetCPPType(), *ConversionReturnProperty->GetCPPType()));
		}

		const FProperty* ConversionArgProperty = ConversionResult.GetValue().ArgumentProperty;
		if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, ConversionArgProperty))
		{
			return MakeError(FString::Printf(TEXT("The source property '%s' (%s) does not match the argument type of the conversion function (%s)."), *SourceProperty->GetName(), *SourceProperty->GetCPPType(), *DestinationProperty->GetCPPType()));
		}
	}
	else if (!UE::MVVM::BindingHelper::ArePropertiesCompatible(SourceProperty, DestinationProperty))
	{
		return MakeError(FString::Printf(TEXT("The source property '%s' (%s) does not match the type of the destination property '%s' (%s). A conversion function is required."), *SourceProperty->GetName(), *SourceProperty->GetCPPType(), *DestinationProperty->GetName(), *DestinationProperty->GetCPPType()));
	}

	return MakeValue(true);
}

