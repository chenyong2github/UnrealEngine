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

void FGetSkeletalMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> DataType;
	if (Out->IsA<DataType>(&SkeletalMesh))
	{
		GetOutput(&SkeletalMesh)->SetValue<DataType>(SkeletalMesh, Context); // prime to avoid ensure

		if (SkeletalMesh)
		{
			GetOutput(&SkeletalMesh)->SetValue<DataType>(SkeletalMesh, Context);
		}
		else if (const Dataflow::FEngineContext* EngineContext = Context.AsType<Dataflow::FEngineContext>())
		{
			if (const USkeletalMesh* SkeletalMeshFromOwner = Dataflow::Reflection::FindObjectPtrProperty<USkeletalMesh>(
				EngineContext->Owner, PropertyName))
			{
				GetOutput(&SkeletalMesh)->SetValue<DataType>(DataType(SkeletalMeshFromOwner), Context);
			}
		}
	}
}

void FSkeletalMeshBoneDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> InDataType;
	if (Out->IsA<int>(&BoneIndexOut))
	{
		GetOutput(&BoneIndexOut)->SetValue<int>(INDEX_NONE, Context); // prime to avoid ensure

		if( InDataType InSkeletalMesh = GetInput(&SkeletalMesh)->GetValue<InDataType>(Context, SkeletalMesh) )
		{
			int32 Index = InSkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
			Out->SetValue<int>(Index, Context);
		}

	}
}


