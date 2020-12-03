// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"

#include "VolumeMeshDescriptionToolTarget.generated.h"

/**
 * A tool target backed by AVolume
 */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeMeshDescriptionToolTarget : public UPrimitiveComponentToolTarget,
	public IMeshDescriptionCommitter, public IMeshDescriptionProvider
{
	GENERATED_BODY()

public:
	UVolumeMeshDescriptionToolTarget();

	const UE::Conversion::FVolumeToMeshOptions& GetVolumeToMeshOptions() { return VolumeToMeshOptions; }

	// IMeshDescriptionProvider implementation
	FMeshDescription* GetMeshDescription() override;

	// IMeshDescritpionCommitter implementation
	virtual void CommitMeshDescription(const FCommitter& Committer) override;

	// IPrimitiveComponentBackedTarget implementation
	virtual void GetMaterialSet(FComponentMaterialSet& MaterialSetOut) const override;
	virtual void CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet) override;
	// Rest provided by parent class

protected:
	TSharedPtr<FMeshDescription> ConvertedMeshDescription;
	UE::Conversion::FVolumeToMeshOptions VolumeToMeshOptions;

	friend class UVolumeMeshDescriptionToolTargetFactory;
};

/** Factory for UStaticMeshComponentToolTarget to be used by the target manager. */
UCLASS(Transient)
class MESHMODELINGTOOLSEDITORONLY_API UVolumeMeshDescriptionToolTargetFactory : public UToolTargetFactory
{
	GENERATED_BODY()

public:

	virtual bool CanBuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) const override;

	virtual UToolTarget* BuildTarget(UObject* SourceObject, const FToolTargetTypeRequirements& TargetTypeInfo) override;
};