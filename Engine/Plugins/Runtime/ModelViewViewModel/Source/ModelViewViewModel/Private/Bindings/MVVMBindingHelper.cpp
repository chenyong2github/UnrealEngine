// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMBindingHelper.h"
#include "FieldNotification/FieldNotificationHelpers.h"
#include "UObject/Class.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/UnrealType.h"

#ifndef UE_MVVM_ALLOW_AUTO_INTEGRAL_CONVERSION
#define UE_MVVM_ALLOW_AUTO_INTEGRAL_CONVERSION 1
#endif

namespace UE::MVVM::BindingHelper
{
	namespace Private
	{
		static const FName NAME_BlueprintPrivate = "BlueprintPrivate";
		static const FName NAME_DeprecatedFunction = "DeprecatedFunction";
		static const FName NAME_BlueprintGetter = "BlueprintGetter";
		static const FName NAME_BlueprintSetter = "BlueprintSetter";


		bool IsValidCommon(const UFunction* InFunction)
		{
			bool bResult = InFunction != nullptr
				&& !InFunction->HasAnyFunctionFlags(FUNC_Net | FUNC_Event | FUNC_EditorOnly)
				&& InFunction->HasAllFunctionFlags(FUNC_BlueprintCallable);

#if WITH_EDITOR
			if (bResult)
			{
				if (InFunction->HasMetaData(NAME_DeprecatedFunction))
				{
					bResult = false;
				}
			}
#endif

			return bResult;
		}
	} // namespace

	// CPF_Protected is not used
	bool IsValidForSourceBinding(const FProperty* InProperty)
	{
		return InProperty != nullptr
			&& InProperty->ArrayDim == 1
			&& !InProperty->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditorOnly)
			&& InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintAssignable);
	}


	bool IsValidForDestinationBinding(const FProperty* InProperty)
	{
		return InProperty != nullptr
			&& InProperty->ArrayDim == 1
			&& !InProperty->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditorOnly | CPF_BlueprintReadOnly)
			&& InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintAssignable);
	}


	bool IsValidForSourceBinding(const UFunction* InFunction)
	{
		//UE::FieldNotification::Helpers::IsValidAsField(InFunction)
		return Private::IsValidCommon(InFunction)
			&& InFunction->HasAllFunctionFlags(FUNC_Const)
			&& InFunction->NumParms == 1
			&& GetReturnProperty(InFunction) != nullptr;
	}


	bool IsValidForDestinationBinding(const UFunction* InFunction)
	{
		// A setter can return a value "bool SetValue(int32)" but is not valid for MVVM
		//&& ((InFunction->NumParms == 1 && GetReturnProperty() == nullptr) || (InFunction->NumParms == 2 && GetReturnProperty() != nullptr));

		return Private::IsValidCommon(InFunction)
			&& !InFunction->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure)
			&& InFunction->NumParms == 1
			&& GetFirstArgumentProperty(InFunction) != nullptr;
	}


	bool IsValidForSourceBinding(const FMVVMConstFieldVariant InVariant)
	{
		if (InVariant.IsProperty())
		{
			return IsValidForSourceBinding(InVariant.GetProperty());
		}
		else if (InVariant.IsFunction())
		{
			return IsValidForSourceBinding(InVariant.GetFunction());
		}
		return false;
	}


	bool IsValidForDestinationBinding(const FMVVMConstFieldVariant InVariant)
	{
		if (InVariant.IsProperty())
		{
			return IsValidForDestinationBinding(InVariant.GetProperty());
		}
		else if (InVariant.IsFunction())
		{
			return IsValidForDestinationBinding(InVariant.GetFunction());
		}
		return false;
	}


	bool IsValidForRuntimeConversion(const UFunction* InFunction)
	{
		if (Private::IsValidCommon(InFunction)
			&& (InFunction->HasAllFunctionFlags(FUNC_Static) || InFunction->HasAllFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
			&& InFunction->NumParms == 2)
		{
			const FProperty* ReturnProperty = GetReturnProperty(InFunction);
			const FProperty* FirstArgumentProperty = GetFirstArgumentProperty(InFunction);
			return ReturnProperty && FirstArgumentProperty && ReturnProperty != FirstArgumentProperty;
		}
		return false;
	}


#if WITH_EDITOR
	bool IsAccessibleDirectlyForSourceBinding(const FProperty* InProperty)
	{
		return IsValidForSourceBinding(InProperty) && !InProperty->GetBoolMetaData(Private::NAME_BlueprintPrivate);
	}


	bool IsAccessibleDirectlyForDestinationBinding(const FProperty* InProperty)
	{
		return IsValidForDestinationBinding(InProperty) && !InProperty->GetBoolMetaData(Private::NAME_BlueprintPrivate);
	}


	bool IsAccessibleWithGetterForSourceBinding(const FProperty* InProperty)
	{
		return IsValidForSourceBinding(InProperty) && InProperty->HasMetaData(Private::NAME_BlueprintGetter);
	}


	bool IsAccessibleWithSetterForDestinationBinding(const FProperty* InProperty)
	{
		return IsValidForSourceBinding(InProperty) && InProperty->HasMetaData(Private::NAME_BlueprintSetter);
	}
#endif //WITH_EDITOR


	FMVVMFieldVariant FindFieldByName(const UStruct* Container, FMVVMBindingName BindingName)
	{
		if (Container)
		{
			if (const UClass* Class = Cast<const UClass>(Container))
			{
				if (UFunction* Function = Class->FindFunctionByName(BindingName.ToName()))
				{
					return FMVVMFieldVariant(Function);
				}
			}
			if (FProperty* Property = PropertyAccessUtil::FindPropertyByName(BindingName.ToName(), Container))
			{
				return FMVVMFieldVariant(Property);
			}
		}
		return FMVVMFieldVariant();
	}


	namespace Private
	{
		FString TryGetPropertyTypeCommon(const FProperty* InProperty)
		{
			if (InProperty == nullptr)
			{
				return TEXT("The property is invalid.");
			}
			
			if (InProperty->ArrayDim != 1)
			{
				return FString::Printf(TEXT("The property '%s' is a static array."), *InProperty->GetName());
			}

			if (InProperty->HasAnyPropertyFlags(CPF_Deprecated))
			{
				return FString::Printf(TEXT("The property '%s' is depreated."), *InProperty->GetName());
			}

			if (InProperty->HasAnyPropertyFlags(CPF_EditorOnly))
			{
				return FString::Printf(TEXT("The property '%s' is only available in the editor."), *InProperty->GetName());
			}
			
			if (!InProperty->HasAnyPropertyFlags(CPF_BlueprintVisible))
			{
				return FString::Printf(TEXT("The property '%s' is not visible to the Blueprint script."), *InProperty->GetName());
			}

			return FString();
		}

		FString TryGetPropertyTypeCommon(const UFunction* InFunction)
		{
			if (InFunction == nullptr)
			{
				return (TEXT("The function is invalid."));
			}

			if (!InFunction->HasAllFunctionFlags(FUNC_BlueprintCallable))
			{
				return (FString::Printf(TEXT("The function '%s' is not BlueprintCallable."), *InFunction->GetName()));
			}

			if (InFunction->HasAllFunctionFlags(FUNC_Net))
			{
				return (FString::Printf(TEXT("The function '%s' is networked."), *InFunction->GetName()));
			}

			if (InFunction->HasAllFunctionFlags(FUNC_Event))
			{

				return (FString::Printf(TEXT("The function '%s' is an event."), *InFunction->GetName()));
			}

			if (InFunction->HasAllFunctionFlags(FUNC_EditorOnly))
			{
				return (FString::Printf(TEXT("The function '%s' is editor only"), *InFunction->GetName()));
			}

#if WITH_EDITOR
			{
				if (InFunction->HasMetaData(NAME_DeprecatedFunction))
				{
					return (FString::Printf(TEXT("The function '%s' is deprecated"), *InFunction->GetName()));
				}
			}
#endif

			return FString();
		}
	} // namespace

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const FProperty* Property)
	{
		FString Result = Private::TryGetPropertyTypeCommon(Property);
		if (!Result.IsEmpty())
		{
			return MakeError(MoveTemp(Result));
		}

		return MakeValue(Property);
	}

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const UFunction* Function)
	{
		FString Result = Private::TryGetPropertyTypeCommon(Function);
		if (!Result.IsEmpty())
		{
			return MakeError(MoveTemp(Result));
		}

		if (!Function->HasAllFunctionFlags(FUNC_Const))
		{
			return MakeError(FString::Printf(TEXT("The function '%s' is not const."), *Function->GetName()));
		}

		//if (!InFunction->HasAllFunctionFlags(FUNC_BlueprintPure))
		//{
		//	return MakeError(FString::Printf(TEXT("The function '%s' is not pure."), *InFunction->GetName()));
		//}

		if (Function->NumParms != 1)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' doesn't have a return value."), *Function->GetName()));
		}

		const FProperty* ReturnProperty = GetReturnProperty(Function);
		if (ReturnProperty == nullptr)
		{
			return MakeError(FString::Printf(TEXT("The return value for function '%s' is invalid."), *Function->GetName()));
		}

		return MakeValue(ReturnProperty);
	}

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const FMVVMConstFieldVariant& InField)
	{
		if (InField.IsEmpty())
		{
			return MakeError(TEXT("No source was provided for the binding."));
		}

		if (InField.IsProperty())
		{
			return TryGetPropertyTypeForSourceBinding(InField.GetProperty());
		}
		else
		{
			check(InField.IsFunction());
			return TryGetPropertyTypeForSourceBinding(InField.GetFunction());
		}
	}

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const FProperty* Property)
	{
		FString Result = Private::TryGetPropertyTypeCommon(Property);
		if (!Result.IsEmpty())
		{
			return MakeError(MoveTemp(Result));
		}

		if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
		{
			return MakeError(FString::Printf(TEXT("The property '%s' is read only."), *Property->GetName()));
		}

		return MakeValue(Property);
	}

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const UFunction* Function)
	{
		FString Result = Private::TryGetPropertyTypeCommon(Function);
		if (!Result.IsEmpty())
		{
			return MakeError(MoveTemp(Result));
		}

		if (Function->NumParms != 1)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' has more than one argument."), *Function->GetName()));
		}

		if (Function->HasAllFunctionFlags(FUNC_Const))
		{
			return MakeError(FString::Printf(TEXT("The function '%s' is const."), *Function->GetName()));
		}

		if (Function->HasAllFunctionFlags(FUNC_BlueprintPure))
		{
			return MakeError(FString::Printf(TEXT("The function '%s' is pure."), *Function->GetName()));
		}

		const FProperty* ReturnProperty = GetReturnProperty(Function);
		if (ReturnProperty != nullptr)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' has a return value."), *Function->GetName()));
		}

		const FProperty* FirstArgumentProperty = GetFirstArgumentProperty(Function);
		if (FirstArgumentProperty == nullptr)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' doesn't not have a valid (single) argument."), *Function->GetName()));
		}

		return MakeValue(FirstArgumentProperty);
	}

	TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const FMVVMConstFieldVariant& InField)
	{
		if (InField.IsEmpty())
		{
			return MakeError(TEXT("No destination was provided for the binding."));
		}

		if (InField.IsProperty())
		{
			return TryGetPropertyTypeForDestinationBinding(InField.GetProperty());
		}
		else
		{
			check(InField.IsFunction());

			return TryGetPropertyTypeForDestinationBinding(InField.GetFunction());
		}
	}
	TValueOrError<const FProperty*, FString> TryGetReturnTypeForConversionFunction(const UFunction* InFunction)
	{
		FString CommonResult = Private::TryGetPropertyTypeCommon(InFunction);
		if (!CommonResult.IsEmpty())
		{
			return MakeError(MoveTemp(CommonResult));
		}

		if (!InFunction->HasAllFunctionFlags(FUNC_Static) && !InFunction->HasAllFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
		{
			return MakeError(FString::Printf(TEXT("The function '%s' is not static or is not const and pure."), *InFunction->GetName()));
		}

		if (InFunction->NumParms < 2)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' does not have the correct number of arguments."), *InFunction->GetName()));
		}

		const FProperty* ReturnProperty = GetReturnProperty(InFunction);
		if (ReturnProperty == nullptr)
		{
			return MakeError(FString::Printf(TEXT("The return value for function '%s' is invalid."), *InFunction->GetName()));
		}

		return MakeValue(ReturnProperty);
	}

	TValueOrError<TArray<const FProperty*>, FString> TryGetArgumentsForConversionFunction(const UFunction* InFunction)
	{
		FString CommonResult = Private::TryGetPropertyTypeCommon(InFunction);
		if (!CommonResult.IsEmpty())
		{
			return MakeError(MoveTemp(CommonResult));
		}

		if (!InFunction->HasAllFunctionFlags(FUNC_Static) && !InFunction->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure))
		{
			return MakeError(FString::Printf(TEXT("The function '%s' is not static or is not const and pure."), *InFunction->GetName()));
		}

		if (InFunction->NumParms < 2)
		{
			return MakeError(FString::Printf(TEXT("The function '%s' does not have the correct number of arguments."), *InFunction->GetName()));
		}

		const FProperty* ReturnProperty = GetReturnProperty(InFunction);
		if (ReturnProperty == nullptr)
		{
			return MakeError(FString::Printf(TEXT("The return value for function '%s' is invalid."), *InFunction->GetName()));
		}

		TArray<const FProperty*> Arguments;
		for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			const FProperty* Property = *It;
			if (IsValidArgumentProperty(Property))
			{
				Arguments.Add(Property);
			}
		}

		return MakeValue(Arguments);
	}

	namespace Private
	{
		bool IsObjectPropertyCompatible(const FProperty* Source, const FProperty* Destination)
		{
			const FObjectPropertyBase* SourceObjectProperty = CastField<const FObjectPropertyBase>(Source);
			const FObjectPropertyBase* DestinationObjectProperty = CastField<const FObjectPropertyBase>(Destination);
			return SourceObjectProperty
				&& DestinationObjectProperty
				&& SourceObjectProperty->PropertyClass
				&& DestinationObjectProperty->PropertyClass
				&& SourceObjectProperty->PropertyClass->IsChildOf(DestinationObjectProperty->PropertyClass);
		}


		bool IsNumericConversionRequired(const FProperty* Source, const FProperty* Destination)
		{
			const FNumericProperty* SourceNumericProperty = CastField<const FNumericProperty>(Source);
			const FNumericProperty* DestinationNumericProperty = CastField<const FNumericProperty>(Destination);
			if (SourceNumericProperty && DestinationNumericProperty)
			{
				const bool bSameType = Destination->SameType(Source);
				const bool bBothFloatingPoint = SourceNumericProperty->IsFloatingPoint() && DestinationNumericProperty->IsFloatingPoint();
#if UE_MVVM_ALLOW_AUTO_INTEGRAL_CONVERSION
				const bool bBothIntegral = SourceNumericProperty->IsInteger() && DestinationNumericProperty->IsInteger();
				const bool bOneIsEnum = SourceNumericProperty->IsEnum() || DestinationNumericProperty->IsEnum();
				return !bSameType && (bBothFloatingPoint || (bBothIntegral && !bOneIsEnum));
#else
				return !bSameType && bBothFloatingPoint;
#endif
			}
			return false;
		}


		void ConvertNumeric(const FProperty* Source, const FProperty* Destination, void* Data)
		{
			check(Source);
			check(Destination);
			check(Data);

			const FNumericProperty* SourceNumericProperty = CastField<const FNumericProperty>(Source);
			const FNumericProperty* DestinationNumericProperty = CastField<const FNumericProperty>(Destination);
			if (SourceNumericProperty && DestinationNumericProperty)
			{
				if (SourceNumericProperty->IsFloatingPoint() && DestinationNumericProperty->IsFloatingPoint())
				{
					//floating to floating
					const void* SrcElemValue = static_cast<const uint8*>(Data);
					void* DestElemValue = static_cast<uint8*>(Data);

					const double Value = SourceNumericProperty->GetFloatingPointPropertyValue(SrcElemValue);
					DestinationNumericProperty->SetFloatingPointPropertyValue(DestElemValue, Value);
				}
#if UE_MVVM_ALLOW_AUTO_INTEGRAL_CONVERSION
				else if (SourceNumericProperty->IsInteger() && DestinationNumericProperty->IsInteger())
				{
					//integral to integral
					const void* SrcElemValue = static_cast<const uint8*>(Data);
					void* DestElemValue = static_cast<uint8*>(Data);

					const int64 Value = SourceNumericProperty->GetSignedIntPropertyValue(SrcElemValue);
					DestinationNumericProperty->SetIntPropertyValue(DestElemValue, Value);
				}
#endif
			}
		}
	} // namespace

	bool ArePropertiesCompatible(const FProperty* Source, const FProperty* Destination)
	{
		if (Source == nullptr || Destination == nullptr)
		{
			return false;
		}

		return Destination->SameType(Source)
				|| Private::IsNumericConversionRequired(Source, Destination)
				|| Private::IsObjectPropertyCompatible(Source, Destination);
	}


	const FProperty* GetReturnProperty(const UFunction* InFunction)
	{
		check(InFunction);
		FProperty* Result = InFunction->GetReturnProperty();

		if (Result == nullptr && InFunction->HasAllFunctionFlags(FUNC_HasOutParms))
		{
			for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				if (It->HasAllPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditorOnly | CPF_ConstParm | CPF_ReferenceParm))
				{
					Result = *It;
					break;
				}
			}
		}

		return Result;
	}

	bool IsValidArgumentProperty(const FProperty* Property)
	{
		if (Property->HasAllPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_EditorOnly | CPF_ReturnParm))
		{
			if (Property->HasAllPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm | CPF_ReferenceParm))
			{
				return false;
			}

			return true;
		}

		return false;
	}

	// We only accept copy argument or const ref
	const FProperty* GetFirstArgumentProperty(const UFunction* InFunction)
	{
		check(InFunction);
		for (TFieldIterator<FProperty> It(InFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (IsValidArgumentProperty(*It))
			{
				return *It;
			}
		}

		return nullptr;
	}

	void ExecuteBinding_NoCheck(const FFieldContext& Source, const FFieldContext& Destination)
	{
		check(!Source.GetObjectVariant().IsNull() && !Destination.GetObjectVariant().IsNull());
		check(!Source.GetFieldVariant().IsEmpty() && !Destination.GetFieldVariant().IsEmpty());

		const bool bIsSourceBindingIsProperty = Source.GetFieldVariant().IsProperty();
		const FProperty* GetterType = bIsSourceBindingIsProperty ? Source.GetFieldVariant().GetProperty() : GetReturnProperty(Source.GetFieldVariant().GetFunction());
		check(GetterType);

		const bool bIsDestinationBindingIsProperty = Destination.GetFieldVariant().IsProperty();
		const FProperty* SetterType = bIsDestinationBindingIsProperty ? Destination.GetFieldVariant().GetProperty() : GetFirstArgumentProperty(Destination.GetFieldVariant().GetFunction());
		check(SetterType);

		check(ArePropertiesCompatible(GetterType, SetterType));

		const int32 AllocationSize = FMath::Max(GetterType->GetSize(), SetterType->GetSize());
		const int32 AllocationMinAlignment = FMath::Max(GetterType->GetMinAlignment(), SetterType->GetMinAlignment());
		void* DataPtr = FMemory_Alloca_Aligned(AllocationSize, AllocationMinAlignment);
		GetterType->InitializeValue(DataPtr);

		if (bIsSourceBindingIsProperty)
		{
			GetterType->GetValue_InContainer(Source.GetObjectVariant().GetData(), DataPtr);
		}
		else
		{
			check(GetterType->GetSize() == Source.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
			check(Source.GetObjectVariant().IsUObject());
			Source.GetObjectVariant().GetUObject()->ProcessEvent(Source.GetFieldVariant().GetFunction(), DataPtr);
		}

		Private::ConvertNumeric(GetterType, SetterType, DataPtr);

		if (bIsDestinationBindingIsProperty)
		{
			SetterType->SetValue_InContainer(Destination.GetObjectVariant().GetData(), DataPtr);
		}
		else
		{
			check(SetterType->GetSize() == Destination.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
			check(Destination.GetObjectVariant().IsUObject());
			Destination.GetObjectVariant().GetUObject()->ProcessEvent(Destination.GetFieldVariant().GetFunction(), DataPtr);
		}

		GetterType->DestroyValue(DataPtr);
	}


	void ExecuteBinding_NoCheck(const FFieldContext& Source, const FFieldContext& Destination, const FFunctionContext& ConversionFunction)
	{
		check(!Source.GetObjectVariant().IsNull() && !Destination.GetObjectVariant().IsNull());
		check(!Source.GetFieldVariant().IsEmpty() && !Destination.GetFieldVariant().IsEmpty());
		check(ConversionFunction.GetFunction() && ConversionFunction.GetObject());

		void* ConversionFunctionDataPtr = FMemory_Alloca_Aligned(ConversionFunction.GetFunction()->ParmsSize, ConversionFunction.GetFunction()->GetMinAlignment());
		for (TFieldIterator<FProperty> It(ConversionFunction.GetFunction()); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(ConversionFunctionDataPtr);
		}

		// Get the value
		{
			const FProperty* ArgumentConversionProperty = GetFirstArgumentProperty(ConversionFunction.GetFunction());
			check(ArgumentConversionProperty);

			const bool bIsSourceBindingIsProperty = Source.GetFieldVariant().IsProperty();
			const FProperty* GetterType = bIsSourceBindingIsProperty ? Source.GetFieldVariant().GetProperty() : GetReturnProperty(Source.GetFieldVariant().GetFunction());
			check(GetterType);
			check(ArePropertiesCompatible(GetterType, ArgumentConversionProperty));

			if (Private::IsNumericConversionRequired(GetterType, ArgumentConversionProperty))
			{
				// we need to do a copy because we may destroy the ReturnConversionProperty (imagine the return type is a TArray)
				const int32 AllocationSize = FMath::Max(GetterType->GetSize(), ArgumentConversionProperty->GetSize());
				const int32 AllocationMinAlignment = FMath::Max(GetterType->GetMinAlignment(), ArgumentConversionProperty->GetMinAlignment());
				void* GetterDataPtr = FMemory_Alloca_Aligned(AllocationSize, AllocationMinAlignment);
				GetterType->InitializeValue(GetterDataPtr); // probably not needed since they are double/float

				if (bIsSourceBindingIsProperty)
				{
					GetterType->GetValue_InContainer(Source.GetObjectVariant().GetData(), GetterDataPtr);
				}
				else
				{
					check(GetterType->GetSize() == Source.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
					check(Source.GetObjectVariant().IsUObject());
					Source.GetObjectVariant().GetUObject()->ProcessEvent(Source.GetFieldVariant().GetFunction(), GetterDataPtr);
				}

				Private::ConvertNumeric(GetterType, ArgumentConversionProperty, GetterDataPtr);
				ArgumentConversionProperty->CopyCompleteValue(ArgumentConversionProperty->ContainerPtrToValuePtr<void>(ConversionFunctionDataPtr), GetterDataPtr);

				GetterType->DestroyValue(GetterDataPtr);

			}
			else
			{
				// Re use the same buffer, no need to create a new copy
				void* SourceDataPtr = ArgumentConversionProperty->ContainerPtrToValuePtr<void>(ConversionFunctionDataPtr);
				if (bIsSourceBindingIsProperty)
				{
					GetterType->GetValue_InContainer(Source.GetObjectVariant().GetData(), SourceDataPtr);
				}
				else
				{
					check(GetterType->GetSize() == Source.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
					check(Source.GetObjectVariant().IsUObject());
					Source.GetObjectVariant().GetUObject()->ProcessEvent(Source.GetFieldVariant().GetFunction(), SourceDataPtr);
				}
			}
		}

		ConversionFunction.GetObject()->ProcessEvent(ConversionFunction.GetFunction(), ConversionFunctionDataPtr);

		{
			const FProperty* ReturnConversionProperty = GetReturnProperty(ConversionFunction.GetFunction());
			check(ReturnConversionProperty);

			const bool bIsDestinationBindingIsProperty = Destination.GetFieldVariant().IsProperty();
			const FProperty* SetterType = bIsDestinationBindingIsProperty ? Destination.GetFieldVariant().GetProperty() : GetFirstArgumentProperty(Destination.GetFieldVariant().GetFunction());
			check(SetterType);
			check(ArePropertiesCompatible(ReturnConversionProperty, SetterType));

			if (Private::IsNumericConversionRequired(ReturnConversionProperty, SetterType))
			{
				// we need to do a copy because we may destroy the ReturnConversionProperty (imagine the return type is a TArray)
				const int32 AllocationSize = FMath::Max(SetterType->GetSize(), ReturnConversionProperty->GetSize());
				const int32 AllocationMinAlignment = FMath::Max(SetterType->GetMinAlignment(), ReturnConversionProperty->GetMinAlignment());
				void* SetterDataPtr = FMemory_Alloca_Aligned(AllocationSize, AllocationMinAlignment);
				SetterType->InitializeValue(SetterDataPtr); // probably not needed since they are double/float

				ReturnConversionProperty->CopyCompleteValue(SetterDataPtr, ReturnConversionProperty->ContainerPtrToValuePtr<void>(ConversionFunctionDataPtr));
				Private::ConvertNumeric(ReturnConversionProperty, SetterType, SetterDataPtr);

				if (bIsDestinationBindingIsProperty)
				{
					SetterType->SetValue_InContainer(Destination.GetObjectVariant().GetData(), SetterDataPtr);
				}
				else
				{
					check(SetterType->GetSize() == Destination.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
					check(Destination.GetObjectVariant().IsUObject());
					Destination.GetObjectVariant().GetUObject()->ProcessEvent(Destination.GetFieldVariant().GetFunction(), SetterDataPtr);
				}

				SetterType->DestroyValue(SetterDataPtr);
			}
			else
			{
				// Re use the same buffer, no need to create a new copy
				void* DestinationDataPtr = ReturnConversionProperty->ContainerPtrToValuePtr<void>(ConversionFunctionDataPtr);
				if (bIsDestinationBindingIsProperty)
				{
					SetterType->SetValue_InContainer(Destination.GetObjectVariant().GetData(), DestinationDataPtr);
				}
				else
				{
					check(SetterType->GetSize() == Destination.GetFieldVariant().GetFunction()->ParmsSize); // only 1 param
					check(Destination.GetObjectVariant().IsUObject());
					Destination.GetObjectVariant().GetUObject()->ProcessEvent(Destination.GetFieldVariant().GetFunction(), DestinationDataPtr);
				}
			}
		}

		for (TFieldIterator<FProperty> It(ConversionFunction.GetFunction()); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(ConversionFunctionDataPtr);
		}
	}
}
