// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_RigidBody_Chaos.h"
#include "Kismet2/CompilerResultsLog.h"
#include "BoneControllers/AnimNode_RigidBody.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_RigidBody

#define LOCTEXT_NAMESPACE "RigidBody_Chaos"

UAnimGraphNode_RigidBody_Chaos::UAnimGraphNode_RigidBody_Chaos(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_RigidBody_Chaos::GetControllerDescription() const
{
	return LOCTEXT("AnimGraphNode_RigidBody_Chaos_ControllerDescription", "Chaos rigid body simulation for physics asset");
}

FText UAnimGraphNode_RigidBody_Chaos::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_RigidBody_Chaos_Tooltip", "Use Chaos to simulate parts of the skeletal using the specified Physics Asset");
}

FText UAnimGraphNode_RigidBody_Chaos::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText(LOCTEXT("AnimGraphNode_RigidBody_Chaos_NodeTitle", "Chaos RigidBody"));
}

void UAnimGraphNode_RigidBody_Chaos::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	if(Node.bEnableWorldGeometry && Node.SimulationSpace != ESimulationSpace::WorldSpace)
	{
		MessageLog.Error(*LOCTEXT("AnimGraphNode_CompileError", "@@ - uses world collision without world space simulation. This is not supported").ToString());
	}
	
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

#undef LOCTEXT_NAMESPACE
