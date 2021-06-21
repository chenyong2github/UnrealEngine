// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNodeFunctionRef.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimSubsystem_NodeRelevancy.h"
#include "Animation/AnimInstance.h"

void FAnimNodeFunctionRef::Initialize(const UClass* InClass)
{
	if(FunctionName != NAME_None)
	{
		Function = InClass->FindFunctionByName(FunctionName);
	}
}

void FAnimNodeFunctionRef::Call(UObject* InObject, void* InParameters) const
{
	if(IsValid())
	{
		InObject->ProcessEvent(Function, InParameters);
	}
}

namespace UE
{
namespace Anim
{

template<typename ContextType>
static void CallFunctionHelper(const FAnimNodeFunctionRef& InFunction, ContextType InContext, FAnimNode_Base& InNode)
{
	if(InFunction.IsValid())
	{
		UAnimInstance* AnimInstance = CastChecked<UAnimInstance>(InContext.GetAnimInstanceObject());
		
		TSharedRef<FAnimExecutionContext::FData> ContextData = MakeShared<FAnimExecutionContext::FData>(InContext);
			
		struct FAnimNodeFunctionParams
		{
			FAnimExecutionContext ExecutionContext;
			FAnimNodeReference NodeReference;
		};
			
		FAnimNodeFunctionParams Params = { FAnimExecutionContext(ContextData), FAnimNodeReference(AnimInstance, InNode) };
			
		InFunction.Call(AnimInstance, &Params);
	}
}

void FNodeFunctionCaller::Initialize(const FAnimationInitializeContext& InContext, FAnimNode_Base& InNode)
{
	CallFunctionHelper(InNode.GetInitializeFunction(), InContext, InNode);
}
	
void FNodeFunctionCaller::BecomeRelevant(const FAnimationUpdateContext& InContext, FAnimNode_Base& InNode)
{
	const FAnimNodeFunctionRef& Function = InNode.GetBecomeRelevantFunction();
	if(Function.IsValid())
	{
		FAnimSubsystemInstance_NodeRelevancy& RelevancySubsystem = CastChecked<UAnimInstance>(InContext.GetAnimInstanceObject())->GetSubsystem<FAnimSubsystemInstance_NodeRelevancy>();
		FAnimNodeRelevancyStatus Status = RelevancySubsystem.UpdateNodeRelevancy(InContext, InNode);
		if(Status.HasJustBecomeRelevant())
		{
			CallFunctionHelper(Function, InContext, InNode);
		}
	}
}
	
void FNodeFunctionCaller::Update(const FAnimationUpdateContext& InContext, FAnimNode_Base& InNode)
{
	CallFunctionHelper(InNode.GetUpdateFunction(), InContext, InNode);
}

void FNodeFunctionCaller::Evaluate(FPoseContext& InContext, FAnimNode_Base& InNode)
{
	CallFunctionHelper(InNode.GetEvaluateFunction(), InContext, InNode);
}

void FNodeFunctionCaller::EvaluateComponentSpace(FComponentSpacePoseContext& InContext, FAnimNode_Base& InNode)
{
	CallFunctionHelper(InNode.GetEvaluateFunction(), InContext, InNode);
}

}}