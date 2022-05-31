// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

#include "Dataflow/DataflowNodesConnectionTypes.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowEngineUtil.h"
#include "Dataflow/DataflowProperty.h"
#include "Logging/LogMacros.h"
#include "UObject/UnrealTypePrivate.h"
#include "Animation/Skeleton.h"
#include "Animation/Rig.h"
#include "Engine/SkeletalMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataflowSkeletalMeshNodes, Log, All);

namespace Dataflow
{
	
	class DATAFLOWNODES_API SkeletalMeshBone : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(SkeletalMeshBone)

	public:
		TProperty<FName> SkeletalMeshAttributeName;
		TProperty<FName> BoneName;

		TSharedPtr< class TOutput<int> > BoneIndexOut;
		TSharedPtr< class TOutput<FString> > BoneNameOut;
		TSharedPtr< class TOutput<FVector3f> > BonePositionOut;

		SkeletalMeshBone(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, SkeletalMeshAttributeName({ FName("SkeletalMeshAttributeName"), FName("SkeletalMesh"), this })
			, BoneName({ FName("BoneName"), FName(""), this})
			, BoneIndexOut(new TOutput<int>(TOutputParameters<int>({ FName("BoneIndexOut"), this })))
			, BoneNameOut(new TOutput<FString>(TOutputParameters<FString>({ FName("BoneNameOut"), this })))
			, BonePositionOut(new TOutput<FVector3f>(TOutputParameters<FVector3f>({ FName("BonePositionOut"), this })))
		{}


		virtual void Evaluate(const FContext& Context, FConnection* Out) const override
		{
			BoneIndexOut->SetValue(INDEX_NONE, Context);
			BoneNameOut->SetValue(BoneName.GetValue().ToString(), Context);
			BonePositionOut->SetValue(FVector3f(0, 0, 0), Context);

			if (const FEngineContext* EngineContext = (const FEngineContext*)(&Context))
			{
				if (const USkeletalMesh* SkeletalMesh = Reflection::FindObjectPtrProperty<USkeletalMesh>(
					EngineContext->Owner, SkeletalMeshAttributeName.GetValue()))
				{
					if (const USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
					{
						BoneNameOut->SetValue(BoneName.GetValue().ToString(), Context);
						int32 Index = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName.GetValue());
						BoneIndexOut->SetValue(Index, Context);
						if (Index != INDEX_NONE)
						{
							TArray<FTransform> ComponentPose; 
							Animation::GlobalTransforms(SkeletalMesh->GetRefSkeleton(), ComponentPose);
							if (0 < Index && Index < ComponentPose.Num())
							{
								BonePositionOut->SetValue(FVector3f(ComponentPose[Index].GetTranslation()), Context);
							}
						}
					}
				}
			}
		}
	};
	
	void RegisterSkeletalMeshNodes();


}

