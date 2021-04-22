// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeMessages.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSync.h"
#include "ActiveStateMachineScope.generated.h"

struct FAnimationBaseContext;
struct FAnimInstanceProxy;
struct FAnimNode_StateMachine;

USTRUCT()
struct ENGINE_API FEncounteredStateMachineStack
{
	GENERATED_BODY()
	FEncounteredStateMachineStack() {} 
	FEncounteredStateMachineStack(const FEncounteredStateMachineStack& ParentStack, int32 InStateMachineIndex, int32 InStateIndex);
	FEncounteredStateMachineStack(int32 InStateMachineIndex, int32 InStateIndex);
	struct ENGINE_API FStateMachineEntry
	{
		FStateMachineEntry() {}
		FStateMachineEntry(int32 InStateMachineIndex, int32 InStateIndex)
		: StateMachineIndex(InStateMachineIndex)
		, StateIndex(InStateIndex)
		{}
		
		int32 StateMachineIndex = INDEX_NONE;
		int32 StateIndex = INDEX_NONE;;
	};
	TArray<FStateMachineEntry, TInlineAllocator<4>> StateStack;
	static TSharedPtr<const FEncounteredStateMachineStack> InitStack(const FEncounteredStateMachineStack& ParentStack, int32 InStateMachineIndex, int32 InStateIndex);
	static TSharedPtr<const FEncounteredStateMachineStack> InitStack(int32 InStateMachineIndex, int32 InStateIndex);
};

namespace UE { namespace Anim {
	
class ENGINE_API FAnimNotifyStateMachineContext : public UE::Anim::IAnimNotifyEventContextDataInterface
{
	DECLARE_NOTIFY_CONTEXT_INTERFACE(FAnimNotifyStateMachineContext)
	public:
	FAnimNotifyStateMachineContext(const TSharedPtr<const FEncounteredStateMachineStack>& InEncounteredStateMachines);
	bool IsStateMachineInContext(int32 StateMachineIndex) const;
	bool IsStateInStateMachineInContext(int32 StateMachineIndex, int32 StateIndex) const; 
	TSharedPtr<const FEncounteredStateMachineStack> EncounteredStateMachines; 
};

class ENGINE_API FActiveStateMachineScope : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(FActiveStateMachineScope);
	public:
	FActiveStateMachineScope(const FAnimationBaseContext& InContext, FAnimNode_StateMachine* StateMachine, int32 InStateIndex);
	virtual ~FActiveStateMachineScope();
	static int32 GetStateMachineIndex(FAnimNode_StateMachine* StateMachine, const FAnimationBaseContext& Context);
	virtual TSharedPtr<const IAnimNotifyEventContextDataInterface> MakeEventContextData() const override;
	
	TSharedPtr<const FEncounteredStateMachineStack> ActiveStateMachines; 
};

}}	// namespace UE::Anim
