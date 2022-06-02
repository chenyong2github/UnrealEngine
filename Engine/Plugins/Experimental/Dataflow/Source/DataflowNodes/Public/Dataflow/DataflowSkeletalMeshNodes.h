// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

#include "Dataflow/DataflowNodesConnectionTypes.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataflowSkeletalMeshNodes, Log, All);

class USkeletalMesh;

namespace Dataflow
{

	class DATAFLOWNODES_API GetSkeletalMesh : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(GetSkeletalMesh)

	public:
		TProperty<FName> SkeletalMeshAttributeName;

		TSharedPtr< class TOutput<USkeletalMeshPtr> > SkeletalMeshOut;

		GetSkeletalMesh(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, SkeletalMeshAttributeName({ FName("SkeletalMeshAttributeName"), FName("SkeletalMesh"), this })
			, SkeletalMeshOut(new TOutput<USkeletalMeshPtr>(TOutputParameters<USkeletalMeshPtr>({ FName("SkeletalMeshOut"), this })))
		{}


		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;
	};
	
	class DATAFLOWNODES_API SkeletalMeshBone : public FNode
	{
		DATAFLOW_NODE_DEFINE_INTERNAL(SkeletalMeshBone)

	public:
		TProperty<FName> BoneName;

		TSharedPtr< class TInput<USkeletalMeshPtr> > SkeletalMeshIn;
		TSharedPtr< class TOutput<int> > BoneIndexOut;

		SkeletalMeshBone(const FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
			: FNode(InParam, InGuid)
			, BoneName({ FName("BoneName"), FName(""), this})
			, SkeletalMeshIn(new TInput<USkeletalMeshPtr>(TInputParameters<USkeletalMeshPtr>({ FName("SkeletalMeshIn"), this })))
			, BoneIndexOut(new TOutput<int>(TOutputParameters<int>({ FName("BoneIndexOut"), this })))
		{}


		virtual void Evaluate(const FContext& Context, FConnection* Out) const override;
	};
	
	void RegisterSkeletalMeshNodes();


}

