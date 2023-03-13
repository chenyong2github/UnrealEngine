// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraph/AnimNode_AnimNextInterfaceGraph.h"
#include "AnimationDataRegistry.h"
#include "AnimationReferencePose.h"
#include "ControlRig.h"
#include "ControlRigComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "GameFramework/Actor.h"
#include "Animation/NodeMappingContainer.h"
#include "AnimationRuntime.h"
#include "AnimNextInterface.h"
#include "AnimationDataRegistry.h"
#include "AnimationGenerationTools.h"
#include "AnimationReferencePose.h"
#include "AnimNextInterface_LODPose.h"
#include "RigUnit_AnimNextAnimSequence.h" // TEST
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequence.h" // TEST
#include "Engine/SkeletalMesh.h"
#include "BoneContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_AnimNextInterfaceGraph)

#if WITH_EDITOR
#include "Editor.h"
#endif

FAnimNode_AnimNextInterfaceGraph::FAnimNode_AnimNextInterfaceGraph()
	: FAnimNode_CustomProperty()
	, AnimNextInterfaceGraph(nullptr)
	, LODThreshold(INDEX_NONE)
{
}

FAnimNode_AnimNextInterfaceGraph::~FAnimNode_AnimNextInterfaceGraph()
{
}

void FAnimNode_AnimNextInterfaceGraph::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::OnInitializeAnimInstance(InProxy, InAnimInstance);

	InitializeProperties(InAnimInstance, GetTargetClass());
}

void FAnimNode_AnimNextInterfaceGraph::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	SourceLink.GatherDebugData(DebugData.BranchFlow(1.f));
}

void FAnimNode_AnimNextInterfaceGraph::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	GraphDeltaTime += Context.GetDeltaTime();

	SourceLink.Update(Context);

	if (IsLODEnabled(Context.AnimInstanceProxy))
	{
		GetEvaluateGraphExposedInputs().Execute(Context);

		PropagateInputProperties(Context.AnimInstanceProxy->GetAnimInstanceObject());
	}

	FAnimNode_CustomProperty::Update_AnyThread(Context);

	//TRACE_ANIM_NODE_VALUE(Context, TEXT("Class"), *GetNameSafe(ControlRigClass.Get()));
}

void FAnimNode_AnimNextInterfaceGraph::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	SourceLink.Initialize(Context);

	FAnimNode_CustomProperty::Initialize_AnyThread(Context);
}

void FAnimNode_AnimNextInterfaceGraph::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FAnimNode_CustomProperty::CacheBones_AnyThread(Context);

	SourceLink.CacheBones(Context);
}

void FAnimNode_AnimNextInterfaceGraph::Evaluate_AnyThread(FPoseContext & Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	using namespace UE::AnimNext;

	FPoseContext SourcePose(Output);

	if (SourceLink.GetLinkNode())
	{
		SourceLink.Evaluate(SourcePose);
	}
	else
	{
		SourcePose.ResetToRefPose();
	}

	USkeletalMeshComponent* SkeletalMeshComponent = Output.AnimInstanceProxy->GetSkelMeshComponent();
	check(SkeletalMeshComponent != nullptr);

	FAnimationDataHandle RefPoseHandle = FAnimationDataRegistry::Get()->GetOrGenerateReferencePose(SkeletalMeshComponent);
	const FAnimationReferencePose& RefPose = RefPoseHandle.GetRef<FAnimationReferencePose>();

	const int32 LODLevel = Output.AnimInstanceProxy->GetLODLevel();

	// TODO : In a normal graph, FState/FParamStorage/FContext would come from FAnimationUpdateContext, here I have to create one the fly for the AnimNext graph
	//UE::AnimNext::Interface::FState RootState; // moved to .h as I need state persistence in the AnimNext graph
	UE::AnimNext::FParamStorage ParamStorage(5, 0, 0);	// TODO : see if we can have state and params together and optional. Also, default size
	
	// TODO : Using AnimInstanceProxy->GetDeltaSeconds() makes the preview to advance  multiple times when debug options are activated (i.e. ShowUncompressedAnim)
	// See where to get / how to calculate the correct delta value
	UE::AnimNext::FContext RootContext(GraphDeltaTime, RootState, ParamStorage);
	GraphDeltaTime = 0.f; // Reset for the case that we receive multiple calls to Evaluate (debug options)

	FAnimNextGraphReferencePose GraphReferencePose(&RefPose);
	FAnimNextGraph_AnimSequence GraphTestSequence(TestSequence);  // TEST

	FAnimNextGraphLODPose GraphSourceLODPose(FAnimationLODPose(RefPose, LODLevel, false, Output.ExpectsAdditivePose()));
	FGenerationTools::RemapPose(LODLevel, SourcePose, RefPose, GraphSourceLODPose.LODPose);

	RootContext.AddInputReference(FName(TEXT("GraphReferencePose")), GraphReferencePose);			// TODO : Should this go into the AnimNextVM Context ?
	RootContext.AddInputValue(FName(TEXT("GraphLODLevel")), LODLevel);								// TODO : Should this go into the AnimNextVM Context ?
	RootContext.AddInputValue(FName(TEXT("GraphExpectsAdditive")), Output.ExpectsAdditivePose());	// TODO : Should this go into the AnimNextVM Context ?

	RootContext.AddInputReference(FName(TEXT("SourcePose")), GraphSourceLODPose);		// TODO : Pass this as a external variable maybe ? When we have support for variables in the rigvm graph
	RootContext.AddInputReference(FName(TEXT("TestSequence")), GraphTestSequence);		// TEST anim decompression - Anim Sequence to decompress
		
	TScriptInterface<IAnimNextInterface> ScriptInterface = AnimNextInterfaceGraph;
	FAnimNextGraphLODPose GraphResultLODPose(FAnimationLODPose(RefPose, LODLevel, true, Output.ExpectsAdditivePose()));

	const bool bOk = UE::AnimNext::Interface::GetDataSafe(ScriptInterface, RootContext, GraphResultLODPose);
	if (bOk)
	{
		FGenerationTools::RemapPose(LODLevel, RefPose, GraphResultLODPose.LODPose, Output);
	}
	else
	{
		Output.ResetToRefPose();
	}


	// evaluate 
	FAnimNode_CustomProperty::Evaluate_AnyThread(Output);
}

void FAnimNode_AnimNextInterfaceGraph::PostSerialize(const FArchive& Ar)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// after compile, we have to reinitialize
	// because it needs new execution code
	// since memory has changed
	if (Ar.IsObjectReferenceCollector())
	{
		if (AnimNextInterfaceGraph)
		{
			//AnimNextInterfaceGraph->Initialize();
		}
	}
}

void FAnimNode_AnimNextInterfaceGraph::InitializeProperties(const UObject* InSourceInstance, UClass* InTargetClass)
{
	// Build property lists
	SourceProperties.Reset(SourcePropertyNames.Num());
	DestProperties.Reset(SourcePropertyNames.Num());

	check(SourcePropertyNames.Num() == DestPropertyNames.Num());

	for (int32 Idx = 0; Idx < SourcePropertyNames.Num(); ++Idx)
	{
		FName& SourceName = SourcePropertyNames[Idx];
		UClass* SourceClass = InSourceInstance->GetClass();

		FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, SourceName);
		SourceProperties.Add(SourceProperty);
		DestProperties.Add(nullptr);
	}
}

void FAnimNode_AnimNextInterfaceGraph::PropagateInputProperties(const UObject* InSourceInstance)
{
	if (InSourceInstance)
	{
		// Assign value to the properties exposed as pins
		for (int32 PropIdx = 0; PropIdx < SourceProperties.Num(); ++PropIdx)
		{
		}
	}
}


#if WITH_EDITOR

void FAnimNode_AnimNextInterfaceGraph::HandleObjectsReinstanced_Impl(UObject* InSourceObject, UObject* InTargetObject, const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	FAnimNode_CustomProperty::HandleObjectsReinstanced_Impl(InSourceObject, InTargetObject, OldToNewInstanceMap);
}

#endif
