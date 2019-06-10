// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditConditionContext.h"
#include "EditConditionParser.h"

#include "PropertyNode.h"
#include "PropertyEditorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditCondition, Log, All);

FEditConditionContext::FEditConditionContext(FPropertyNode& InPropertyNode)
{
	PropertyNode = InPropertyNode.AsShared();

	FComplexPropertyNode* ComplexParentNode = FindComplexParent();
	check(ComplexParentNode);

	const UProperty* Property = InPropertyNode.GetProperty();
	check(Property);
}

FComplexPropertyNode* FEditConditionContext::FindComplexParent() const
{
	if (!PropertyNode.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();

	FPropertyNode* SearchStart = PinnedNode.Get();
	if (PropertyEditorHelpers::IsStaticArray(*SearchStart))
	{
		//in the case of conditional static arrays, we have to go up one more level to get the proper parent struct.
		SearchStart = SearchStart->GetParentNode();
	}

	return SearchStart->FindComplexParent();
}

const UBoolProperty* FEditConditionContext::GetSingleBoolProperty(const TSharedPtr<FEditConditionExpression>& Expression) const
{
	if (!PropertyNode.IsValid())
	{
		return nullptr;
	}

	const UProperty* Property = PropertyNode.Pin()->GetProperty();

	const UBoolProperty* BoolProperty = nullptr;
	for (const FCompiledToken& Token : Expression->Tokens)
	{
		if (const EditConditionParserTokens::FPropertyToken* PropertyToken = Token.Node.Cast<EditConditionParserTokens::FPropertyToken>())
		{
			if (BoolProperty != nullptr)
			{
				// second property token
				return nullptr;
			}

			const UProperty* Field = FindField<UProperty>(Property->GetOwnerStruct(), *PropertyToken->PropertyName);
			BoolProperty = Cast<UBoolProperty>(Field);
			
			// not a bool
			if (BoolProperty == nullptr)
			{
				return nullptr;
			}
		}
	}

	return BoolProperty;
}

static TSet<const UProperty*> AlreadyLogged;

template<typename T>
T* FindTypedField(const TWeakPtr<FPropertyNode>& PropertyNode, const FString& PropertyName)
{
	if (PropertyNode.IsValid())
	{
		TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
		const UProperty* Property = PinnedNode->GetProperty();

		UProperty* Field = FindField<UProperty>(Property->GetOwnerStruct(), *PropertyName);
		if (Field == nullptr)
		{
			if (!AlreadyLogged.Find(Field))
			{
				AlreadyLogged.Add(Field);
				UE_LOG(LogEditCondition, Error, TEXT("EditCondition parsing failed: Field name \"%s\" was not found in class \"%s\"."), *PropertyName, *Property->GetOwnerStruct()->GetName());
			}

			return nullptr;
		}

		return Cast<T>(Field);
	}

	return nullptr;
}

TOptional<bool> FEditConditionContext::GetBoolValue(const FString& PropertyName) const
{
	const UBoolProperty* BoolProperty = FindTypedField<UBoolProperty>(PropertyNode, PropertyName);
	if (BoolProperty == nullptr)
	{
		return TOptional<bool>();
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
	
	TOptional<bool> Result;

	FComplexPropertyNode* ComplexParentNode = FindComplexParent();
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* BasePtr = ComplexParentNode->GetMemoryOfInstance(Index);
		uint8* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(BasePtr);

		bool bValue = BoolProperty->GetPropertyValue(ValuePtr);
		if (!Result.IsSet())
		{
			Result = bValue;
		}
		else if (Result.GetValue() != bValue)
		{
			// all values aren't the same...
			return TOptional<bool>();
		}
	}

	return Result;
}

TOptional<double> FEditConditionContext::GetNumericValue(const FString& PropertyName) const
{
	const UNumericProperty* NumericProperty = FindTypedField<UNumericProperty>(PropertyNode, PropertyName);
	if (NumericProperty == nullptr)
	{
		return TOptional<double>();
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();

	TOptional<double> Result;

	FComplexPropertyNode* ComplexParentNode = FindComplexParent();
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* BasePtr = ComplexParentNode->GetMemoryOfInstance(Index);
		uint8* ValuePtr = NumericProperty->ContainerPtrToValuePtr<uint8>(BasePtr);

		double Value = 0;

		if (NumericProperty->IsInteger())
		{
			Value = (double) NumericProperty->GetSignedIntPropertyValue(ValuePtr);
		}
		else if (NumericProperty->IsFloatingPoint())
		{
			Value = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
		}

		if (!Result.IsSet())
		{
			Result = Value;
		}
		else if (!FMath::IsNearlyEqual(Result.GetValue(), Value))
		{
			// all values aren't the same...
			return TOptional<double>();
		}
	}

	return Result;
}

TOptional<FString> FEditConditionContext::GetEnumValue(const FString& PropertyName) const
{
	const UProperty* Property = FindTypedField<UProperty>(PropertyNode, PropertyName);
	if (Property == nullptr)
	{
		return TOptional<FString>();
	}

	const UEnum* EnumType = nullptr;
	const UNumericProperty* NumericProperty = nullptr;
	if (const UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
	{
		NumericProperty = EnumProperty->GetUnderlyingProperty();
		EnumType = EnumProperty->GetEnum();
	}
	else if (const UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
	{
		NumericProperty = ByteProperty;
		EnumType = ByteProperty->GetIntPropertyEnum();
	}
	else
	{
		return TOptional<FString>();
	}
	
	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();

	TOptional<int64> Result;

	FComplexPropertyNode* ComplexParentNode = FindComplexParent();
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* BasePtr = ComplexParentNode->GetMemoryOfInstance(Index);
		uint8* ValuePtr = Property->ContainerPtrToValuePtr<uint8>(BasePtr);

		int64 Value = NumericProperty->GetSignedIntPropertyValue(ValuePtr);
		if (!Result.IsSet())
		{
			Result = Value;
		}
		else if (Result.GetValue() != Value)
		{
			// all values aren't the same...
			return TOptional<FString>();
		}
	}

	if (Result.IsSet())
	{
		return EnumType->GetNameStringByValue(Result.GetValue());
	}

	return TOptional<FString>();
}

TOptional<FString> FEditConditionContext::GetTypeName(const FString& PropertyName) const
{
	const UProperty* Property = FindTypedField<UProperty>(PropertyNode, PropertyName);
	if (Property == nullptr)
	{
		return TOptional<FString>();
	}

	if (const UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
	{
		return EnumProperty->GetEnum()->GetName();
	}
	else if (const UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
	{
		return ByteProperty->GetIntPropertyEnum()->GetName();
	}

	return Property->GetCPPType();
}