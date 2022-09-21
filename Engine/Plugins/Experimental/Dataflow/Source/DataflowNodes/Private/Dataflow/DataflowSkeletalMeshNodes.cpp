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
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSkeletonDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSkeletalMeshBoneDataflowNode);
	}
}

void FGetSkeletalMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> DataType;
	if (Out->IsA<DataType>(&SkeletalMesh))
	{
		SetValue<DataType>(Context, SkeletalMesh, &SkeletalMesh);

		if (!SkeletalMesh)
		{
			if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
			{
				if (const USkeletalMesh* SkeletalMeshFromOwner = Dataflow::Reflection::FindObjectPtrProperty<USkeletalMesh>(
					EngineContext->Owner, PropertyName))
				{
					SetValue<DataType>(Context, DataType(SkeletalMeshFromOwner), &SkeletalMesh);
				}
			}
		}
	}
}


void FGetSkeletonDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeleton> DataType;
	if (Out->IsA<DataType>(&Skeleton))
	{
		SetValue<DataType>(Context, Skeleton, &Skeleton);

		if (!Skeleton)
		{
			if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
			{
				if (const USkeleton* SkeletonFromOwner = Dataflow::Reflection::FindObjectPtrProperty<USkeleton>(
					EngineContext->Owner, PropertyName))
				{
					SetValue<DataType>(Context, DataType(SkeletonFromOwner), &Skeleton);
				}
			}
		}
	}
}

void FSkeletalMeshBoneDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> InDataType;
	if (Out->IsA<int>(&BoneIndexOut))
	{
		SetValue<int>(Context, INDEX_NONE, &BoneIndexOut);

		if( InDataType InSkeletalMesh = GetValue<InDataType>(Context, &SkeletalMesh) )
		{
			int32 Index = InSkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
			SetValue<int>(Context, Index, &BoneIndexOut);
		}

	}
}


