// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/MeshComponent.h"
#include "InteractiveToolObjects.h"
#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "Changes/MeshReplacementChange.h"
#include "MeshConversionOptions.h"
#include "DynamicMesh3.h"

#include "BaseDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;
class FMeshVertexChange;
class FMeshChange;



/**
 * EMeshRenderAttributeFlags is used to identify different mesh rendering attributes, for things
 * like fast-update functions
 */
enum class EMeshRenderAttributeFlags : uint8
{
	None = 0,
	Positions = 0x1,
	VertexColors = 0x2,
	VertexNormals = 0x4,
	VertexUVs = 0x8,

	All = 0xFF
};
ENUM_CLASS_FLAGS(EMeshRenderAttributeFlags);



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
	 * initialize the internal mesh from a MeshDescription
	 */
	virtual void InitializeMesh(FMeshDescription* MeshDescription)
	{
		unimplemented();
	}

	/**
	 * @return pointer to internal mesh
	 */
	virtual FDynamicMesh3* GetMesh()
	{
		unimplemented();
		return nullptr;
	}

	/**
	 * @return pointer to internal mesh
	 */
	virtual const FDynamicMesh3* GetMesh() const
	{
		unimplemented();
		return nullptr;
	}

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


	//
	// Modification support
	//
public:

	virtual void ApplyTransform(const FTransform3d& Transform, bool bInvert)
	{
		unimplemented();
	}

	/**
	 * Write the internal mesh to a MeshDescription
	 * @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	 * @param ConversionOptions struct of additional options for the conversion
	 */
	virtual void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions)
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
	 * Configure whether wireframe rendering is enabled or not
	 */
	virtual void SetEnableWireframeRenderPass(bool bEnable) { check(false); }

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


	/**
	 * Show/Hide the secondary triangle buffers. Does not invalidate SceneProxy.
	 */
	virtual void SetSecondaryBuffersVisibility(bool bVisible);

	/**
	 * @return true if secondary buffers are currently set to be visible
	 */
	virtual bool GetSecondaryBuffersVisibility() const;


protected:

	TArray<UMaterialInterface*> BaseMaterials;

	UMaterialInterface* OverrideRenderMaterial = nullptr;
	UMaterialInterface* SecondaryRenderMaterial = nullptr;

	bool bDrawSecondaryBuffers = true;

public:


	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UMeshComponent Interface.

};
