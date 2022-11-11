// Copyright Epic Games, Inc. All Rights Reserved.
#include "EnumColumn.h"
#include "ChooserPropertyAccess.h"

#if WITH_EDITOR
#include "IPropertyAccessEditor.h"
#endif

bool UChooserParameterEnum_ContextProperty::GetValue(const UObject* ContextObject, uint8& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;

	if (UE::Chooser::ResolvePropertyChain(Container, StructType, PropertyBindingChain))
	{
		if (const FEnumProperty* EnumProperty = FindFProperty<FEnumProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *EnumProperty->ContainerPtrToValuePtr<uint8>(Container);
			return true;
		}

		if (const FByteProperty* ByteProperty = FindFProperty<FByteProperty>(StructType, PropertyBindingChain.Last()))
		{
			if (ByteProperty->IsEnum())
			{
				OutResult = *ByteProperty->ContainerPtrToValuePtr<uint8>(Container);
				return true;
			}
		}
	}

	return false;
}

#if WITH_EDITOR

void UChooserParameterEnum_ContextProperty::SetBinding(const TArray<FBindingChainElement>& InBindingChain)
{
	const UEnum* PreviousEnum = Enum;
	Enum = nullptr;

	UE::Chooser::CopyPropertyChain(InBindingChain, PropertyBindingChain);

	const FField* Field = InBindingChain.Last().Field.ToField();
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field))
	{
		Enum = EnumProperty->GetEnum();
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Field))
	{
		Enum = ByteProperty->Enum;
	}

	if (Enum != PreviousEnum)
	{
		// Our enum type has changed! Need to refresh the UI to update enum value pickers.
		EnumChanged.Broadcast();
	}
}

#endif // WITH_EDITOR

UChooserColumnEnum::UChooserColumnEnum(const FObjectInitializer& ObjectInitializer)
{
#if WITH_EDITOR
	InputValue = ObjectInitializer.CreateDefaultSubobject<UChooserParameterEnum_ContextProperty>(this, "InputValue");
	InputValue.GetObject()->SetFlags(RF_Transactional);
	InputChanged();
#endif
}

bool FChooserEnumRowData::Evaluate(const uint8 LeftHandSide) const
{
	switch (Comparison)
	{
	case EChooserEnumComparison::Equal:
		return LeftHandSide == Value;

	case EChooserEnumComparison::NotEqual:
		return LeftHandSide != Value;

	case EChooserEnumComparison::GreaterThan:
		return LeftHandSide > Value;

	case EChooserEnumComparison::GreaterThanEqual:
		return LeftHandSide >= Value;

	case EChooserEnumComparison::LessThan:
		return LeftHandSide < Value;

	case EChooserEnumComparison::LessThanEqual:
		return LeftHandSide <= Value;

	default:
		checkf(false, TEXT("Unsupported comparison type for enum comparison operation!"));
		return false;
	}
}

void UChooserColumnEnum::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut)
{
	uint8 Result = 0;
	if (ContextObject != nullptr &&
		InputValue != nullptr &&
		InputValue->GetValue(ContextObject, Result))
	{
		for (const uint32 Index : IndexListIn)
		{
			if (RowValues.IsValidIndex(Index))
			{
				const FChooserEnumRowData& RowValue = RowValues[Index];
				if (RowValue.Evaluate(Result))
				{
					IndexListOut.Emplace(Index);
				}
			}
		}
	}
	else
	{
		// passthrough fallback (behaves better during live editing)
		IndexListOut = IndexListIn;
	}
}