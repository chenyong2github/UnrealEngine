// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "ToolTargets/VolumeDynamicMeshToolTarget.h"

#include "VolumeMeshDescriptionToolTarget.generated.h"

/**
 * A tool target backed by AVolume
 */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeMeshDescriptionToolTarget : public UVolumeDynamicMeshToolTarget,
	public IMeshDescriptionCommitter, public IMeshDescriptionProvider
{
	GENERATED_BODY()

public:

	// IMeshDescriptionProvider implementation
	FMeshDescription* GetMeshDescription() override;

	// IMeshDescritpionCommitter implementation
	virtual void CommitMeshDescription(const FCommitter& Committer) override;
	using IMeshDescriptionCommitter::CommitMeshDescription; // unhide the other overload

protected:
	// This isn't for caching- we have to take ownership of the mesh description because it is
	// expected for things like a static mesh.
	TSharedPtr<FMeshDescription> ConvertedMeshDescription;

	friend class UVolumeMeshDescriptionToolTargetFactory;
};

/** Factory for UVolumeMeshDescriptionToolTarget to be used by the target manager. */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeMeshDescriptionToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};