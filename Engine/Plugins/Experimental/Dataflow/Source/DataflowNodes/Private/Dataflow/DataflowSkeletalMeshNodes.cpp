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
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(GetSkeletalMesh);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(SkeletalMeshBone);
	}

	void GetSkeletalMesh::Evaluate(const FContext& Context, FConnection* Out) const
	{
		SkeletalMeshOut->SetValue(nullptr, Context);
		if (const FEngineContext* EngineContext = (const FEngineContext*)(&Context))
		{
			if (const USkeletalMesh* SkeletalMesh = Reflection::FindObjectPtrProperty<USkeletalMesh>(
				EngineContext->Owner, SkeletalMeshAttributeName.GetValue()))
			{
				SkeletalMeshOut->SetValue(SkeletalMesh, Context);
			}
		}
	}

	void SkeletalMeshBone::Evaluate(const FContext& Context, FConnection* Out) const
	{
		BoneIndexOut->SetValue(INDEX_NONE, Context);

		if (const USkeletalMesh* SkeletalMesh = SkeletalMeshIn->GetValue(Context))
		{
			int32 Index = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName.GetValue());
			BoneIndexOut->SetValue(Index, Context);
		}
	}
}

