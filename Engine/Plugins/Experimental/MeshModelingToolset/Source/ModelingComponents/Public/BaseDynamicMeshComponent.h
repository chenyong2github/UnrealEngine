// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/MeshComponent.h"
#include "InteractiveToolObjects.h"
#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "Changes/MeshReplacementChange.h"

#include "DynamicMesh3.h"

#include "BaseDynamicMeshComponent.generated.h"


// predecl
class FMeshVertexChange;
class FMeshChange;




/**
 * Tangent calculation modes
 */
UENUM()
enum class EDynamicMeshTangentCalcType : uint8
{
	/** Tangents are not used/available, proceed accordingly (eg generate arbitrary orthogonal basis) */
	NoTangents,
	/** Tangents should be automatically calculated on demand */
	AutoCalculated,
	/** Tangents are externally calculated (behavior undefined if they are not actually externally calculated!) */
	ExternallyCalculated
};


/**
 * UBaseDynamicMeshComponent is a base interface for a UMeshComponent based on a FDynamicMesh.
 * Currently no functionality lives here, only some interface functions are defined that various subclasses implement.
 */
UCLASS(hidecategories = (LOD, Physics, Collision), editinlinenew, ClassGroup = Rendering)
class MODELINGCOMPONENTS_API UBaseDynamicMeshComponent : public UMeshComponent, public IToolFrameworkComponent, public IMeshVertexCommandChangeTarget, public IMeshCommandChangeTarget, public IMeshReplacementCommandChangeTarget
{
	GENERATED_UCLASS_BODY()

public:

	/**
	 * Call this if you update the mesh via GetMesh()
	 * @todo should provide a function that calls a lambda to modify the mesh, and only return const mesh pointer
	 */
	virtual void NotifyMeshUpdated()
	{
		unimplemented();
	}

	/**
	 * Apply a vertex deformation change to the internal mesh  (implements IMeshVertexCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override
	{
		unimplemented();
	}

	/**
	 * Apply a general mesh change to the internal mesh  (implements IMeshCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override
	{
		unimplemented();
	}

	/**
	* Apply a full mesh replacement change to the internal mesh  (implements IMeshReplacementCommandChangeTarget)
	*/
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) override
	{
		unimplemented();
	}


protected:
	/**
	 * Subclass must implement this to notify allocated proxies of updated materials
	 */
	virtual void NotifyMaterialSetUpdated()
	{
		unimplemented();
	}



public:

	/**
	 * @return true if wireframe rendering pass is enabled (default false)
	 */
	virtual bool EnableWireframeRenderPass() const { return false; }


	//
	// Override rendering material support
	//

	/**
	 * Set an active override render material. This should replace all materials during rendering.
	 */
	virtual void SetOverrideRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear any active override render material
	 */
	virtual void ClearOverrideRenderMaterial();
	
	/**
	 * @return true if an override render material is currently enabled for the given MaterialIndex
	 */
	virtual bool HasOverrideRenderMaterial(int k) const
	{
		return OverrideRenderMaterial != nullptr;
	}

	/**
	 * @return active override render material for the given MaterialIndex
	 */
	virtual UMaterialInterface* GetOverrideRenderMaterial(int MaterialIndex) const
	{
		return OverrideRenderMaterial;
	}




public:

	//
	// secondary buffer support
	//

	/**
	 * Set an active override render material. This should replace all materials during rendering.
	 */
	virtual void SetSecondaryRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear any active override render material
	 */
	virtual void ClearSecondaryRenderMaterial();

	/**
	 * @return true if an override render material is currently enabled for the given MaterialIndex
	 */
	virtual bool HasSecondaryRenderMaterial() const
	{
		return SecondaryRenderMaterial != nullptr;
	}

	/**
	 * @return active override render material for the given MaterialIndex
	 */
	virtual UMaterialInterface* GetSecondaryRenderMaterial() const
	{
		return SecondaryRenderMaterial;
	}



protected:

	TArray<UMaterialInterface*> BaseMaterials;

	UMaterialInterface* OverrideRenderMaterial = nullptr;
	UMaterialInterface* SecondaryRenderMaterial = nullptr;

public:


	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UMeshComponent Interface.

};
