// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMFieldPathHelper.h"

#include "Bindings/MVVMBindingHelper.h"

namespace UE::MVVM::FieldPathHelper
{

namespace Private
{

static const FName NAME_BlueprintGetter = "BlueprintGetter";
static const FName NAME_BlueprintSetter = "BlueprintSetter";

} // namespace


TValueOrError<TArray<FMVVMFieldVariant>, FString> GenerateFieldPathList(TSubclassOf<UObject> InFrom, FStringView InFieldPath, bool bForReading)
{
	if (InFrom.Get() == nullptr)
	{
		return MakeError(TEXT("The source class is invalid."));
	}
	if (InFieldPath.IsEmpty())
	{
		return MakeError(TEXT("The FieldPath is empty."));
	}
	if (InFieldPath[InFieldPath.Len() - 1] == TEXT('.'))
	{
		return MakeError(TEXT("The field path cannot end with a '.' character."));
	}

	auto FindContainer = [](const FProperty* Property) -> TValueOrError<UStruct*, FString>
	{
		const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
		const FStructProperty* StructProperty = CastField<const FStructProperty>(Property);

		if (ObjectProperty)
		{
			return MakeValue(ObjectProperty->PropertyClass);
		}
		else if (StructProperty)
		{
			return MakeValue(StructProperty->Struct);
		}
		return MakeError(FString::Printf(TEXT("Field can only be object properties or struct properties. %s is a %s"), *Property->GetName(), *Property->GetClass()->GetName()));
	};

	auto TransformWithAccessor = [](UStruct* CurrentContainer, FMVVMFieldVariant CurrentField, bool bForReading) -> TValueOrError<FMVVMFieldVariant, FString>
	{
#if WITH_EDITORONLY_DATA
		if (bForReading)
		{
			if (!CurrentField.GetProperty()->HasGetter())
			{
				const FString& BlueprintGetter = CurrentField.GetProperty()->GetMetaData(Private::NAME_BlueprintGetter);
				if (!BlueprintGetter.IsEmpty())
				{
					FMVVMFieldVariant NewField = BindingHelper::FindFieldByName(CurrentContainer, FMVVMBindingName(*BlueprintGetter));
					if (NewField.IsFunction())
					{
						CurrentField = NewField;
					}
					else
					{
						return MakeError(FString::Printf(TEXT("The BlueprintGetter %s could not be found on object %s."), *BlueprintGetter, *CurrentContainer->GetName()));
					}
				}
			}
		}
		else
		{
			if (!CurrentField.GetProperty()->HasSetter())
			{
				const FString& BlueprintSetter = CurrentField.GetProperty()->GetMetaData(Private::NAME_BlueprintSetter);
				if (!BlueprintSetter.IsEmpty())
				{
					FMVVMFieldVariant NewField = BindingHelper::FindFieldByName(CurrentContainer, FMVVMBindingName(*BlueprintSetter));
					if (NewField.IsFunction())
					{
						CurrentField = NewField;
					}
					else
					{
						return MakeError(FString::Printf(TEXT("The BlueprintSetter %s could not be found on object %s."), *BlueprintSetter, *CurrentContainer->GetName()));
					}
				}
			}
		}
#endif
		return MakeValue(CurrentField);
	};

	TArray<FMVVMFieldVariant> Result;
	UStruct* CurrentContainer = InFrom.Get();

	// Split the string into property or function names
	//ie. myvar.myfunction.myvar
	int32 FoundIndex = INDEX_NONE;
	while (InFieldPath.FindChar(TEXT('.'), FoundIndex))
	{
		FMVVMFieldVariant Field = BindingHelper::FindFieldByName(CurrentContainer, FMVVMBindingName(FName(FoundIndex, InFieldPath.GetData())));
		if (Field.IsEmpty())
		{
			return MakeError(FString::Printf(TEXT("The field %s could not be found on container %s."), *FName(FoundIndex, InFieldPath.GetData()).ToString(), CurrentContainer ? *CurrentContainer->GetName() : TEXT("<none>")));
		}
		else if (Field.IsProperty())
		{
			TValueOrError<FMVVMFieldVariant, FString> TransformedField = TransformWithAccessor(CurrentContainer, Field, true);
			if (TransformedField.HasError())
			{
				return MakeError(TransformedField.StealError());
			}
			Field = TransformedField.StealValue();
			check(!Field.IsEmpty());

			if (Field.IsProperty())
			{
				TValueOrError<UStruct*, FString> FoundContainer = FindContainer(Field.GetProperty());
				if (FoundContainer.HasError())
				{
					return MakeError(FoundContainer.StealError());
				}
				CurrentContainer = FoundContainer.GetValue();
			}
		}
		
		if (Field.IsFunction())
		{
			const FProperty* ReturnProperty = BindingHelper::GetReturnProperty(Field.GetFunction());
			TValueOrError<UStruct*, FString> FoundContainer = FindContainer(ReturnProperty);
			if (FoundContainer.HasError())
			{
				return MakeError(FoundContainer.StealError());
			}
			CurrentContainer = FoundContainer.GetValue();
		}

		InFieldPath = InFieldPath.RightChop(FoundIndex + 1);
		Result.Add(Field);
	}

	// The last field can be anything (that is what we are going to bind to)
	if (InFieldPath.Len() > 0)
	{
		FMVVMFieldVariant Field = BindingHelper::FindFieldByName(CurrentContainer, FMVVMBindingName(InFieldPath.GetData()));
		if (Field.IsEmpty())
		{
			return MakeError(FString::Printf(TEXT("The field %s could not be found on container %s."), InFieldPath.GetData(), CurrentContainer ? *CurrentContainer->GetName() : TEXT("<none>")));
		}

		if (Field.IsProperty())
		{
			TValueOrError<FMVVMFieldVariant, FString> TransformedField = TransformWithAccessor(CurrentContainer, Field, bForReading);
			if (TransformedField.HasError())
			{
				return MakeError(TransformedField.StealError());
			}
			Field = TransformedField.StealValue();
		}
		check(!Field.IsEmpty());
		Result.Add(Field);
	}

	return MakeValue(MoveTemp(Result));
}


TValueOrError<TArray<FMVVMFieldVariant>, FString> GenerateConversionFunctionFieldPathList(TSubclassOf<UObject> From, FStringView FieldPath)
{
	return MakeError(TEXT("Not implemented yet"));
}


TValueOrError<UObject*, void> EvaluateObjectProperty(const FFieldContext& InSource)
{
	if (InSource.GetObjectVariant().IsNull())
	{
		return MakeError();
	}

	const bool bIsProperty = InSource.GetFieldVariant().IsProperty();
	const FProperty* GetterType = bIsProperty ? InSource.GetFieldVariant().GetProperty() : BindingHelper::GetReturnProperty(InSource.GetFieldVariant().GetFunction());
	check(GetterType);

	const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(GetterType);
	if (ObjectProperty == nullptr)
	{
		return MakeError();
	}

	if (bIsProperty)
	{
		return MakeValue(ObjectProperty->GetObjectPropertyValue_InContainer(InSource.GetObjectVariant().GetData()));
	}
	else
	{
		check(InSource.GetObjectVariant().IsUObject());
		UFunction* Function = InSource.GetFieldVariant().GetFunction();
		void* DataPtr = FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
		ObjectProperty->InitializeValue(DataPtr);
		InSource.GetObjectVariant().GetUObject()->ProcessEvent(Function, DataPtr);
		UObject* Result = ObjectProperty->GetObjectPropertyValue_InContainer(DataPtr);
		ObjectProperty->DestroyValue(DataPtr);
		return MakeValue(Result);
	}
}

} // namespace
