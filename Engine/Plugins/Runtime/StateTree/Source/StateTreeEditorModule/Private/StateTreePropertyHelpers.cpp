// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreePropertyHelpers.h"
#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE { namespace StateTree { namespace PropertyHelpers {

const FSlateBrush* GetTypeIcon(EStateTreeVariableType Type)
{
	switch (Type)
	{
	case EStateTreeVariableType::Bool:
		return FEditorStyle::GetBrush(TEXT("ClassIcon.BlackboardKeyType_Bool"));
	case EStateTreeVariableType::Float:
		return FEditorStyle::GetBrush(TEXT("ClassIcon.BlackboardKeyType_Float"));
	case EStateTreeVariableType::Int:
		return FEditorStyle::GetBrush(TEXT("ClassIcon.BlackboardKeyType_Int"));
	case EStateTreeVariableType::Vector:
		return FEditorStyle::GetBrush(TEXT("ClassIcon.BlackboardKeyType_Vector"));
	case EStateTreeVariableType::Object:
		return FEditorStyle::GetBrush(TEXT("ClassIcon.BlackboardKeyType_Object"));
	default:
		return FEditorStyle::GetBrush(TEXT("NoBrush"));
	}
}

} } } // UE::StateTree::PropertyHelpers

#undef LOCTEXT_NAMESPACE
