// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeContext.generated.h"

struct FAnimNode_Base;
struct FAnimationBaseContext;
struct FAnimationInitializeContext;
struct FAnimationUpdateContext;
struct FPoseContext;
struct FComponentSpacePoseContext;



// Context used to expose anim nodes to BP function libraries
USTRUCT(BlueprintType)
struct ENGINE_API FAnimNodeContext
{
	GENERATED_BODY()

public:
	// Internal data, weakly referenced
	struct FData
	{
	public:
		FData(const FAnimationInitializeContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct);

		FData(const FAnimationUpdateContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct);

		FData(FPoseContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct);

		FData(FComponentSpacePoseContext& InContext, FAnimNode_Base& InAnimNode, UScriptStruct* InAnimNodeStruct);

	private:
		friend struct ::FAnimNodeContext;
		
		enum class EContextType
		{
			None,
			Initialize,
			Update,
			Pose,
			ComponentSpacePose,
		};
	
		// The node we wrap
		FAnimNode_Base* AnimNode = nullptr;

		// The struct type of the anim node
		UScriptStruct* AnimNodeStruct = nullptr;

		// The context used when executing this node, e.g. FAnimationUpdateContext, FPoseContext etc.
		FAnimationBaseContext* Context = nullptr;

		// The phase we are in
		EContextType ContextType = EContextType::None;	
	};
	
	FAnimNodeContext() = default;

	FAnimNodeContext(TWeakPtr<FData> InData)
		: Data(InData)
	{}

	// Get the node we wrap. If the node is not of the specified type then this will return nullptr.
	template<typename NodeType>
	NodeType* GetAnimNode() const
	{
		if(TSharedPtr<FData> PinnedData = Data.Pin())
		{
			if(NodeType::StaticStruct()->IsChildOf(PinnedData->AnimNodeStruct))
			{
				return static_cast<NodeType*>(PinnedData->AnimNode);
			}
		}
		return nullptr;
	}

	// Get the context we wrap.
	FAnimationBaseContext* GetContext() const
	{
		if(TSharedPtr<FData> PinnedData = Data.Pin())
		{
			return PinnedData->Context;
		}
		return nullptr;
	}

	// Get the context we wrap. If the context is not an initialize context then this will return nullptr
	FAnimationInitializeContext* GetInitializeContext() const;
	
	// Get the context we wrap. If the context is not an update context then this will return nullptr
	FAnimationUpdateContext* GetUpdateContext() const;

	// Get the context we wrap. If the context is not a pose context then this will return nullptr
	FPoseContext* GetPoseContext() const;

	// Get the context we wrap. If the context is not a component space pose context then this will return nullptr
	FComponentSpacePoseContext* GetComponentSpacePoseContext() const;

private:
	friend struct FAnimNodeFunctionCaller;

	// Internal data
	TWeakPtr<FData> Data; 
};