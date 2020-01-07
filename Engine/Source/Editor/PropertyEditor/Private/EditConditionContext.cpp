// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditConditionContext.h"
#include "EditConditionParser.h"

#include "PropertyNode.h"
#include "PropertyEditorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditCondition, Log, All);

FEditConditionContext::FEditConditionContext(FPropertyNode& InPropertyNode)
{
	PropertyNode = InPropertyNode.AsShared();

	FComplexPropertyNode* ComplexParentNode = InPropertyNode.FindComplexParent();
	check(ComplexParentNode);

	const FProperty* Property = InPropertyNode.GetProperty();
	check(Property);
}

const FBoolProperty* FEditConditionContext::GetSingleBoolProperty(const TSharedPtr<FEditConditionExpression>& Expression) const
{
	if (!PropertyNode.IsValid())
	{
		return nullptr;
	}

	const FProperty* Property = PropertyNode.Pin()->GetProperty();

	const FBoolProperty* BoolProperty = nullptr;
	for (const FCompiledToken& Token : Expression->Tokens)
	{
		if (const EditConditionParserTokens::FPropertyToken* PropertyToken = Token.Node.Cast<EditConditionParserTokens::FPropertyToken>())
		{
			if (BoolProperty != nullptr)
			{
				// second property token
				return nullptr;
			}

			const FProperty* Field = FindField<FProperty>(Property->GetOwnerStruct(), *PropertyToken->PropertyName);
			BoolProperty = CastField<FBoolProperty>(Field);
			
			// not a bool
			if (BoolProperty == nullptr)
			{
				return nullptr;
			}
		}
	}

	return BoolProperty;
}

static TSet<const FProperty*> AlreadyLogged;

template<typename T>
T* FindTypedField(const TWeakPtr<FPropertyNode>& PropertyNode, const FString& PropertyName)
{
	if (PropertyNode.IsValid())
	{
		TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
		const FProperty* Property = PinnedNode->GetProperty();

		FProperty* Field = FindField<FProperty>(Property->GetOwnerStruct(), *PropertyName);
		if (Field == nullptr)
		{
			if (!AlreadyLogged.Find(Field))
			{
				AlreadyLogged.Add(Field);
				UE_LOG(LogEditCondition, Error, TEXT("EditCondition parsing failed: Field name \"%s\" was not found in class \"%s\"."), *PropertyName, *Property->GetOwnerStruct()->GetName());
			}

			return nullptr;
		}

		return CastField<T>(Field);
	}

	return nullptr;
}

/** 
 * Get the parent to use as the context when evaluating the edit condition.
 * For normal properties inside a UObject, this is the UObject. 
 * For children of containers, this is the UObject the container is in. 
 * Note: We do not support nested containers.
 * The result can be nullptr in exceptional cases, eg. if the UI is getting rebuilt.
 */
static FPropertyNode* GetEditConditionParentNode(const TSharedPtr<FPropertyNode>& PropertyNode)
{
	FPropertyNode* ParentNode = PropertyNode->GetParentNode();
	FFieldVariant PropertyOuter = PropertyNode->GetProperty()->GetOwnerVariant();

	if (PropertyOuter.Get<FArrayProperty>() != nullptr ||
		PropertyOuter.Get<FSetProperty>() != nullptr ||
		PropertyOuter.Get<FMapProperty>() != nullptr)
	{
		// in a dynamic container, parent is actually one level up
		return ParentNode->GetParentNode();
	}

	return ParentNode;
}

static uint8* GetPropertyValuePtr(const FProperty* Property, const TSharedPtr<FPropertyNode>& PropertyNode, FPropertyNode* ParentNode, FComplexPropertyNode* ComplexParentNode, int32 Index)
{
	uint8* BasePtr = ComplexParentNode->GetMemoryOfInstance(Index);
	if (BasePtr == nullptr)
	{
		return nullptr;
	}

	uint8* ParentPtr = ParentNode->GetValueAddress(BasePtr, PropertyNode->HasNodeFlags(EPropertyNodeFlags::IsSparseClassData) != 0);
	if (ParentPtr == nullptr)
	{
		return nullptr;
	}

	uint8* ValuePtr = ComplexParentNode->GetValuePtrOfInstance(Index, Property, ParentNode);

	// SPARSEDATA_TODO: remove the next three lines once we're sure the pointer math is correct
	uint8* OldValuePtr = Property->ContainerPtrToValuePtr<uint8>(ParentPtr);
	check(OldValuePtr == ValuePtr || PropertyNode->HasNodeFlags(EPropertyNodeFlags::IsSparseClassData));
	ensure(ValuePtr != nullptr);

	return ValuePtr;
}

TOptional<bool> FEditConditionContext::GetBoolValue(const FString& PropertyName) const
{
	const FBoolProperty* BoolProperty = FindTypedField<FBoolProperty>(PropertyNode, PropertyName);
	if (BoolProperty == nullptr)
	{
		return TOptional<bool>();
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
	if (!PinnedNode.IsValid())
	{
		return TOptional<bool>();
	}

	FPropertyNode* ParentNode = GetEditConditionParentNode(PinnedNode);
	if (ParentNode == nullptr)
	{
		return TOptional<bool>();
	}

	FComplexPropertyNode* ComplexParentNode = PinnedNode->FindComplexParent();

	TOptional<bool> Result;
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* ValuePtr = GetPropertyValuePtr(BoolProperty, PinnedNode, ParentNode, ComplexParentNode, Index);
		if (ValuePtr == nullptr)
		{
			return TOptional<bool>();
		}

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
	const FNumericProperty* NumericProperty = FindTypedField<FNumericProperty>(PropertyNode, PropertyName);
	if (NumericProperty == nullptr)
	{
		return TOptional<double>();
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
	if (!PinnedNode.IsValid())
	{
		return TOptional<double>();
	}

	FPropertyNode* ParentNode = GetEditConditionParentNode(PinnedNode);
	if (ParentNode == nullptr)
	{
		return TOptional<double>();
	}

	FComplexPropertyNode* ComplexParentNode = PinnedNode->FindComplexParent();

	TOptional<double> Result;
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* ValuePtr = GetPropertyValuePtr(NumericProperty, PinnedNode, ParentNode, ComplexParentNode, Index);
		if (ValuePtr == nullptr)
		{
			return TOptional<double>();
		}

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
	const FProperty* Property = FindTypedField<FProperty>(PropertyNode, PropertyName);
	if (Property == nullptr)
	{
		return TOptional<FString>();
	}

	const UEnum* EnumType = nullptr;
	const FNumericProperty* NumericProperty = nullptr;
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		NumericProperty = EnumProperty->GetUnderlyingProperty();
		EnumType = EnumProperty->GetEnum();
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		NumericProperty = ByteProperty;
		EnumType = ByteProperty->GetIntPropertyEnum();
	}
	else
	{
		return TOptional<FString>();
	}

	if (!NumericProperty->IsInteger())
	{
		return TOptional<FString>();
	}

	TSharedPtr<FPropertyNode> PinnedNode = PropertyNode.Pin();
	if (!PinnedNode.IsValid())
	{
		return TOptional<FString>();
	}

	FPropertyNode* ParentNode = GetEditConditionParentNode(PinnedNode);
	if (ParentNode == nullptr)
	{
		return TOptional<FString>();
	}

	FComplexPropertyNode* ComplexParentNode = PinnedNode->FindComplexParent();

	TOptional<int64> Result;
	for (int32 Index = 0; Index < ComplexParentNode->GetInstancesNum(); ++Index)
	{
		uint8* ValuePtr = GetPropertyValuePtr(Property, PinnedNode, ParentNode, ComplexParentNode, Index);
		if (ValuePtr == nullptr)
		{
			return TOptional<FString>();
		}

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

	if (!Result.IsSet())
	{
		return TOptional<FString>();
	}

	return EnumType->GetNameStringByValue(Result.GetValue());
}

TOptional<FString> FEditConditionContext::GetTypeName(const FString& PropertyName) const
{
	const FProperty* Property = FindTypedField<FProperty>(PropertyNode, PropertyName);
	if (Property == nullptr)
	{
		return TOptional<FString>();
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		return EnumProperty->GetEnum()->GetName();
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		return ByteProperty->GetIntPropertyEnum()->GetName();
	}

	return Property->GetCPPType();
}