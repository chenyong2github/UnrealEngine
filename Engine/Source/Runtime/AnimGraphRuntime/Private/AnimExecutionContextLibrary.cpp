// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimExecutionContextLibrary.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimClassInterface.h"

UAnimInstance* UAnimExecutionContextLibrary::GetAnimInstance(const FAnimExecutionContext& Context)
{
	return CastChecked<UAnimInstance>(Context.GetBaseContext()->GetAnimInstanceObject());
}

FAnimNodeReference UAnimExecutionContextLibrary::GetAnimNodeReference(UAnimInstance* Instance, int32 Index)
{
	IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(Instance->GetClass());
	const TArray<FStructProperty*>& AnimNodeProperties = AnimClassInterface->GetAnimNodeProperties();

	// As the index is patched during compilation, it needs to be reversed here
	int32 ReverseIndex = AnimNodeProperties.Num() - 1 - Index;
	return FAnimNodeReference(Instance, ReverseIndex);
}