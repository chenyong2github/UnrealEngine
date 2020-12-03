// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "StaticMeshComponentToolTarget.generated.h"

/**
 * A tool target backed by a static mesh component that can provide and take a mesh
 * description.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UStaticMeshComponentToolTarget : public UPrimitiveComponentToolTarget,
	public IMeshDescriptionCommitter, public IMeshDescriptionProvider
{
	GENERATED_BODY()

public:

	// IMeshDescriptionProvider implementation
	FMeshDescription* GetMeshDescription() override;

	// IMeshDescritpionCommitter implementation
	virtual void CommitMeshDescription(const FCommitter& Committer) override;

	// IPrimitiveComponentBackedTarget implementation
	virtual void CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet) override;
	// Rest provided by parent class

protected:

	friend class UStaticMeshComponentToolTargetFactory;
};

/** Factory for UStaticMeshComponentToolTarget to be used by the target manager. */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UStaticMeshComponentToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};