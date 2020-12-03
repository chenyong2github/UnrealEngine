// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "ProceduralMeshComponentToolTarget.generated.h"
UCLASS(Transient)
class PROCEDURALMESHCOMPONENT_API UProceduralMeshComponentToolTarget : public UPrimitiveComponentToolTarget,
	public IMeshDescriptionCommitter, public IMeshDescriptionProvider
{
	GENERATED_BODY()

public:
	virtual FMeshDescription* GetMeshDescription() override;
	virtual void CommitMeshDescription(const FCommitter&) override;
private:
	TSharedPtr<FMeshDescription> MeshDescription;

	friend class UProceduralMeshComponentToolTargetFactory;
};

UCLASS(Transient)
class PROCEDURALMESHCOMPONENT_API  UProceduralMeshComponentToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;
	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};
