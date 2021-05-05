// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargets/PrimitiveComponentToolTarget.h"
#include "ComponentSourceInterfaces.h"  // for EStaticMeshEditingLOD

#include "StaticMeshComponentToolTarget.generated.h"

class UStaticMesh;

/**
 * A tool target backed by a static mesh component that can provide and take a mesh
 * description.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UStaticMeshComponentToolTarget : public UPrimitiveComponentToolTarget,
	public IMeshDescriptionCommitter, public IMeshDescriptionProvider, public IMaterialProvider, public IStaticMeshBackedTarget
{
	GENERATED_BODY()

public:
	/**
	 * Configure active LOD to edit. Can only call this after Component is configured in base UPrimitiveComponentToolTarget.
	 * If requested LOD does not exist, fall back to one that does.
	 */
	virtual void SetEditingLOD(EStaticMeshEditingLOD RequestedEditingLOD);

	/** @return current editing LOD */
	virtual EStaticMeshEditingLOD GetEditingLOD() const { return EditingLOD; }

public:
	virtual bool IsValid() const override;


public:
	// IMeshDescriptionProvider implementation
	FMeshDescription* GetMeshDescription() override;

	// IMeshDescritpionCommitter implementation
	virtual void CommitMeshDescription(const FCommitter& Committer) override;

	// IMaterialProvider implementation
	int32 GetNumMaterials() const override;
	UMaterialInterface* GetMaterial(int32 MaterialIndex) const override;
	void GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bPreferAssetMaterials) const override;
	virtual bool CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset) override;	

	// IStaticMeshBackedTarget
	UStaticMesh* GetStaticMesh() const override;

	// Rest provided by parent class

public:

protected:
	EStaticMeshEditingLOD EditingLOD = EStaticMeshEditingLOD::LOD0;

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



public:
	virtual EStaticMeshEditingLOD GetActiveEditingLOD() const { return EditingLOD; }
	virtual void SetActiveEditingLOD(EStaticMeshEditingLOD NewEditingLOD);

protected:
	// LOD to edit, default is to edit LOD0
	EStaticMeshEditingLOD EditingLOD = EStaticMeshEditingLOD::LOD0;
};