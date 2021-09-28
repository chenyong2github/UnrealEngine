// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/StateTreeCondition_Common.h"
#include "StateTreePropertyBindings.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE { namespace StateTree { namespace Conditions {

#if WITH_EDITOR
FText GetOperatorText(const EGenericAICheck Operator)
{
	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return FText::FromString(TEXT("=="));
		break;
	case EGenericAICheck::NotEqual:
		return FText::FromString(TEXT("!="));
		break;
	case EGenericAICheck::Less:
		return FText::FromString(TEXT("&lt;"));
		break;
	case EGenericAICheck::LessOrEqual:
		return FText::FromString(TEXT("&lt;="));
		break;
	case EGenericAICheck::Greater:
		return FText::FromString(TEXT("&gt;"));
		break;
	case EGenericAICheck::GreaterOrEqual:
		return FText::FromString(TEXT("&gt;="));
		break;
	default:
		return FText::FromString(TEXT("??"));
		break;
	}
	return FText::GetEmpty();
}
#endif // WITH_EDITOR

template<typename T>
bool CompareNumbers(const T Left, const T Right, const EGenericAICheck Operator)
{
	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return Left == Right;
		break;
	case EGenericAICheck::NotEqual:
		return Left != Right;
		break;
	case EGenericAICheck::Less:
		return Left < Right;
		break;
	case EGenericAICheck::LessOrEqual:
		return Left <= Right;
		break;
	case EGenericAICheck::Greater:
		return Left > Right;
		break;
	case EGenericAICheck::GreaterOrEqual:
		return Left >= Right;
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
		return false;
		break;
	}
	return false;
}

}}} // UE::StateTree::Conditions

#if WITH_EDITOR
FText FStateTreeCondition_CompareInt::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareInt, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareInt, Right));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = FText::AsNumber(Left);
	}

	FText OperatorText = UE::StateTree::Conditions::GetOperatorText(Operator);

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = FText::AsNumber(Right);
	}

	return FText::Format(LOCTEXT("CompareIntDesc", "{0} <Details.Bold>{1}</> {2} <Details.Bold>{3}</>"), InvertText, LeftText, OperatorText, RightText);
}
#endif

bool FStateTreeCondition_CompareInt::TestCondition() const
{
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<int32>(Left, Right, Operator);
	return bResult ^ bInvert;
}


#if WITH_EDITOR
FText FStateTreeCondition_CompareFloat::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareFloat, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareFloat, Right));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = FText::AsNumber(Left);
	}

	FText OperatorText = UE::StateTree::Conditions::GetOperatorText(Operator);

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = FText::AsNumber(Right);
	}

	return FText::Format(LOCTEXT("CompareFloatDesc", "{0} <Details.Bold>{1}</> {2} <Details.Bold>{3}</>"), InvertText, LeftText, OperatorText, RightText);
}
#endif

bool FStateTreeCondition_CompareFloat::TestCondition() const
{
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<float>(Left, Right, Operator);
	return bResult ^ bInvert;
}


#if WITH_EDITOR
FText FStateTreeCondition_CompareBool::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareBool, bLeft));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareBool, bRight));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		LeftText = FText::FromString(LexToSanitizedString(bLeft));
	}

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		RightText = FText::FromString(LexToSanitizedString(bRight));
	}

	return FText::Format(LOCTEXT("CompareBoolDesc", "{0} <Details.Bold>{1}</> is <Details.Bold>{2}</>"), InvertText, LeftText, RightText);
}
#endif

bool FStateTreeCondition_CompareBool::TestCondition() const
{
	return (bLeft == bRight) ^ bInvert;
}


#if WITH_EDITOR
FText FStateTreeCondition_CompareEnum::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath LeftPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareEnum, Left));
	const FStateTreeEditorPropertyPath RightPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareEnum, Right));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText LeftText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(LeftPath))
	{
		LeftText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		if (Left.Enum)
		{
			LeftText = Left.Enum->GetDisplayNameTextByValue(int64(Left.Value));
		}
		else
		{
			LeftText = LOCTEXT("Invalid", "Invalid");
		}
	}

	FText RightText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(RightPath))
	{
		RightText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		if (Right.Enum)
		{
			RightText = Right.Enum->GetDisplayNameTextByValue(int64(Right.Value));
		}
		else
		{
			RightText = LOCTEXT("Invalid", "Invalid");
		}
	}

	return FText::Format(LOCTEXT("EnumEqualsDesc", "{0} <Details.Bold>{1}</> is <Details.Bold>{2}</>"), InvertText, LeftText, RightText);
}

void FStateTreeCondition_CompareEnum::OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup)
{
	if (!TargetPath.IsValid())
	{
		return;
	}

	// Left has changed, update enums from the leaf property.
	if (TargetPath.Path.Last() == GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareEnum, Left))
	{
		if (const FProperty* LeafProperty = BindingLookup.GetPropertyPathLeafProperty(SourcePath))
		{
			// Handle both old stype namespace enums and new class enum properties.
			UEnum* NewEnum = nullptr;
			if (const FByteProperty* ByteProperty = CastField<FByteProperty>(LeafProperty))
			{
				NewEnum = ByteProperty->GetIntPropertyEnum();
			}
			else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(LeafProperty))
			{
				NewEnum = EnumProperty->GetEnum();
			}

			if (Left.Enum != NewEnum)
			{
				Left.Initialize(NewEnum);
			}
		}
		else
		{
			Left.Initialize(nullptr);
		}

		if (Right.Enum != Left.Enum)
		{
			Right.Initialize(Left.Enum);
		}
	}
}

#endif
bool FStateTreeCondition_CompareEnum::TestCondition() const
{
	return (Left == Right) ^ bInvert;
}


#if WITH_EDITOR
FText FStateTreeCondition_CompareDistance::GetDescription(const IStateTreeBindingLookup& BindingLookup) const
{
	const FStateTreeEditorPropertyPath SourcePath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareDistance, Source));
	const FStateTreeEditorPropertyPath TargetPath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareDistance, Target));
	const FStateTreeEditorPropertyPath DistancePath(ID, GET_MEMBER_NAME_STRING_CHECKED(FStateTreeCondition_CompareDistance, Distance));

	FText InvertText;
	if (bInvert)
	{
		InvertText = LOCTEXT("Not", "Not");
	}

	FText SourceText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(SourcePath))
	{
		SourceText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		SourceText = Source.ToCompactText();
	}

	FText TargetText;
	if (const FStateTreeEditorPropertyPath* Binding = BindingLookup.GetPropertyBindingSource(TargetPath))
	{
		TargetText = BindingLookup.GetPropertyPathDisplayName(*Binding);
	}
	else
	{
		TargetText = Target.ToCompactText();
	}

	FText OperatorText = UE::StateTree::Conditions::GetOperatorText(Operator);

	FText DistanceText;
	if (const FStateTreeEditorPropertyPath* DistanceBinding = BindingLookup.GetPropertyBindingSource(DistancePath))
	{
		DistanceText = BindingLookup.GetPropertyPathDisplayName(*DistanceBinding);
	}
	else
	{
		DistanceText = FText::AsNumber(Distance);
	}

	return FText::Format(LOCTEXT("CompareDistanceDesc", "{0} Distance <Details.Subdued>from</> <Details.Bold>{1}</> <Details.Subdued>to</> <Details.Bold>{2}</> {3} <Details.Bold>{4}</>"), InvertText, SourceText, TargetText, OperatorText, DistanceText);
}
#endif

bool FStateTreeCondition_CompareDistance::TestCondition() const
{
	const float Left = FVector::DistSquared(Source, Target);
	const float Right = FMath::Square(Distance);
	const bool bResult = UE::StateTree::Conditions::CompareNumbers<float>(Left, Right, Operator);
	return bResult ^ bInvert;
}

#undef LOCTEXT_NAMESPACE
