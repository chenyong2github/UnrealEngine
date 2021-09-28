// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeCondition.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeVariable.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

FStateTreeCondition::FStateTreeCondition()
	: Left(EStateTreeVariableBindingMode::Any, FName(), EStateTreeVariableType::Void)
	, Right(EStateTreeVariableBindingMode::Typed, FName(), EStateTreeVariableType::Void)
	, Operator(EGenericAICheck::MAX)
{
}

#if WITH_EDITOR
FText FStateTreeCondition::GetDescription() const
{
	if (!Left.IsBound())
	{
		return FText(LOCTEXT("ConditionNotSet", "Condition Not Set"));
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("Left"), Left.GetDescription());

	switch (Operator)
	{
	case EGenericAICheck::Equal:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorEqual", "=="));
		break;
	case EGenericAICheck::NotEqual:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorNotEqual", "!="));
		break;
	case EGenericAICheck::Less:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorLess", "<"));
		break;
	case EGenericAICheck::LessOrEqual:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorLessOrEqual", "<="));
		break;
	case EGenericAICheck::Greater:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorGreater", ">"));
		break;
	case EGenericAICheck::GreaterOrEqual:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorGreaterOrEqual", ">="));
		break;
	default:
		Args.Add(TEXT("Operator"), LOCTEXT("OperatorUnknown", "??"));
		break;
	}

	Args.Add(TEXT("Right"), Right.GetDescription());

	return FText::Format(LOCTEXT("Condition", "{Left} {Operator} {Right}"), Args);
}
#endif

#undef LOCTEXT_NAMESPACE
