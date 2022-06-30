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
	TArray<FMVVMAvailableBinding> GetAvailableBindings(const UStruct* Class, const UClass* AccessorType, const UE::FieldNotification::IClassDescriptor* ClassDescriptor)
	{
		TSet<FName> FieldDescriptors;
		if (ClassDescriptor)
		{
			ClassDescriptor->ForEachField(CastChecked<UClass>(Class), [&FieldDescriptors](UE::FieldNotification::FFieldId FieldId)
			{
				FieldDescriptors.Add(FieldId.GetName());
				return true;
			});
		}

		TArray<FMVVMAvailableBinding> Result;
		Result.Reserve(FieldDescriptors.Num());

		const bool bAccessorCanSeeProtectedMember = AccessorType ? AccessorType->IsChildOf(Class) : false;

		if (Cast<UClass>(Class))
		{
			for (TFieldIterator<const UFunction> FunctionIt(Class, EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
			{
				const UFunction* Function = *FunctionIt;
				check(Function);

				// N.B. A function can be private/protected and can still be use when it's a BlueprintGetter/BlueprintSetter.
				//But we bind to the property not the function.
				if (Function->HasAnyFunctionFlags(FUNC_Private) && Class != AccessorType)
				{
					continue;
				}
				if (Function->HasAnyFunctionFlags(FUNC_Protected) && !bAccessorCanSeeProtectedMember)
				{
					continue;
				}

				const bool bIsReadable = BindingHelper::IsValidForSourceBinding(Function);
				const bool bIsWritable = BindingHelper::IsValidForDestinationBinding(Function);
				const bool bHasNotify = FieldDescriptors.Contains(Function->GetFName()) && bIsReadable;
				if (bIsReadable || bIsWritable || bHasNotify)
				{
					Result.Add(FMVVMAvailableBinding(FMVVMBindingName(Function->GetFName()), bIsReadable, bIsWritable, bHasNotify));
				}
			}
		}

#if WITH_EDITOR
		FName NAME_BlueprintGetter = "BlueprintGetter";
		FName NAME_BlueprintSetter = "BlueprintSetter";
		FName NAME_BlueprintPrivate = "BlueprintPrivate";
#endif

		for (TFieldIterator<const FProperty> PropertyIt(Class, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;

			// N.B. Property can be private/protected in cpp but if they are visible in BP that is all that matter.
			//Only property defined in BP can be BP visible and really private.
#if WITH_EDITOR
			if (Property->GetBoolMetaData(NAME_BlueprintPrivate) && Class != AccessorType)
			{
				continue;
			}
#endif

			const bool bIsReadable = BindingHelper::IsValidForSourceBinding(Property);
			const bool bIsWritable = BindingHelper::IsValidForDestinationBinding(Property);
			const bool bHasNotify = FieldDescriptors.Contains(Property->GetFName()) && bIsReadable;
			if (bIsReadable || bIsWritable || bHasNotify)
			{
#if WITH_EDITOR
				// Remove any BlueprintGetter & BlueprintSetter and use the Property instead.
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
				Result.Add(FMVVMAvailableBinding(FMVVMBindingName(Property->GetFName()), bIsReadable, bIsWritable, bHasNotify));
			}
		}

		return Result;
	}

	TArray<FMVVMAvailableBinding> GetAvailableBindings(TSubclassOf<UObject> InSubClass, TSubclassOf<UObject> InAccessor)
	{
		if (InSubClass.Get() && InSubClass.Get()->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
		{
			TScriptInterface<INotifyFieldValueChanged> DefaultObject = InSubClass.GetDefaultObject();
			const UE::FieldNotification::IClassDescriptor& ClassDescriptor = DefaultObject->GetFieldNotificationDescriptor();
			return GetAvailableBindings(InSubClass.Get(), InAccessor.Get(), &ClassDescriptor);
		}
		else
		{
			return GetAvailableBindings(InSubClass.Get(), InAccessor.Get(), nullptr);
		}
	}
} //namespace


TArray<FMVVMAvailableBinding> UMVVMSubsystem::GetAvailableBindings(TSubclassOf<UObject> Class, TSubclassOf<UObject> Accessor) const
{
	return UE::MVVM::Private::GetAvailableBindings(Class, Accessor);
}


TArray<FMVVMAvailableBinding> UMVVMSubsystem::GetAvailableBindingsForStruct(const UScriptStruct* Struct) const
{
	return UE::MVVM::Private::GetAvailableBindings(Struct, nullptr, nullptr);
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
