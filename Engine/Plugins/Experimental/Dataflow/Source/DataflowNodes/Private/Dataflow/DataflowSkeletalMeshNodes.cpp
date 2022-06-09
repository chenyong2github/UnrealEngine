// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSkeletalMeshNodes.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"
#include "Logging/LogMacros.h"
#include "UObject/UnrealTypePrivate.h"
#include "Animation/Skeleton.h"
#include "Animation/Rig.h"
#include "Engine/SkeletalMesh.h"

namespace Dataflow
{
	void RegisterSkeletalMeshNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSkeletalMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSkeletalMeshBoneDataflowNode);
	}
}
void FGetSkeletalMeshDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	SkeletalMeshOut->SetValue(nullptr, Context);
	if (const Dataflow::FEngineContext* EngineContext = (const Dataflow::FEngineContext*)(&Context))
	{
		if (const USkeletalMesh* SkeletalMesh = Dataflow::Reflection::FindObjectPtrProperty<USkeletalMesh>(
			EngineContext->Owner, SkeletalMeshAttributeName))
		{
			SkeletalMeshOut->SetValue(SkeletalMesh, Context);
		}
	}
}

void FSkeletalMeshBoneDataflowNode::Evaluate(const Dataflow::FContext& Context, Dataflow::FConnection* Out) const
{
	BoneIndexOut->SetValue(INDEX_NONE, Context);

	if (const USkeletalMesh* SkeletalMesh = SkeletalMeshIn->GetValue(Context))
	{
		int32 Index = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
		BoneIndexOut->SetValue(Index, Context);
	}
}


