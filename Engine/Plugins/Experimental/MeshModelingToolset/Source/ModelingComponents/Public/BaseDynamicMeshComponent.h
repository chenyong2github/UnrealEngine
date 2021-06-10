// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "InteractiveToolObjects.h"
#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "Changes/MeshReplacementChange.h"
#include "MeshConversionOptions.h"
#include "DynamicMesh3.h"		// todo replace with predeclaration (lots of fallout)
#include "UDynamicMesh.h"

#include "BaseDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;
class FMeshVertexChange;
class FMeshChange;
using UE::Geometry::FDynamicMesh3;

/**
 * EMeshRenderAttributeFlags is used to identify different mesh rendering attributes, for things
 * like fast-update functions
 */
enum class EMeshRenderAttributeFlags : uint8
{
	None = 0,
	Positions = 1,
	VertexColors = 1<<1,
	VertexNormals = 1<<2,
	VertexUVs = 1<<3,
	SecondaryIndexBuffers = 1<<4,

	AllVertexAttribs = Positions | VertexColors | VertexNormals | VertexUVs
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
 * UBaseDynamicMeshComponent is a base interface for a UMeshComponent based on a UDynamicMesh.
 */
UCLASS(Abstract, hidecategories = (LOD, Physics, Collision), ClassGroup = Rendering)
class MODELINGCOMPONENTS_API UBaseDynamicMeshComponent : 
	public UMeshComponent, 
	public IToolFrameworkComponent, 
	public IMeshVertexCommandChangeTarget, 
	public IMeshCommandChangeTarget, 
	public IMeshReplacementCommandChangeTarget
{
	GENERATED_UCLASS_BODY()


	//===============================================================================================================
	// UBaseDynamicMeshComponent API. Subclasses must implement these functions
	//
public:
	/**
	 * initialize the internal mesh from a MeshDescription
	 * @warning avoid usage of this function, access via GetDynamicMesh() instead
	 */
	virtual void InitializeMesh(const FMeshDescription* MeshDescription)
	{
		unimplemented();
	}

	/**
	 * @return pointer to internal mesh
	 * @warning avoid usage of this function, access via GetDynamicMesh() instead
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
	 * @return the child UDynamicMesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual UDynamicMesh* GetDynamicMesh() 
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

	/**
	 * Apply a transform to the mesh
	 */
	virtual void ApplyTransform(const UE::Geometry::FTransform3d& Transform, bool bInvert)
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




	//===============================================================================================================
	// Built-in Wireframe-on-Shaded Rendering support. The wireframe looks terrible but this is a convenient 
	// way to enable/disable it.
	//
public:
	/**
	 * if true, we always show the wireframe on top of the shaded mesh, even when not in wireframe mode
	 * @todo: this should not be public, access via Set/Get once all usage is cleaned up
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Mesh Component")
	bool bExplicitShowWireframe = false;

	/**
	 * Configure whether wireframe rendering is enabled or not
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void SetEnableWireframeRenderPass(bool bEnable) { bExplicitShowWireframe = bEnable; }

	/**
	 * @return true if wireframe rendering pass is enabled
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual bool GetEnableWireframeRenderPass() const { return bExplicitShowWireframe; }





	//===============================================================================================================
	// Override rendering material support. If an Override material is set, then it
	// will be used during drawing of all mesh buffers except Secondary buffers.
	//
public:
	/**
	 * Set an active override render material. This should replace all materials during rendering.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void SetOverrideRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear any active override render material
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void ClearOverrideRenderMaterial();
	
	/**
	 * @return true if an override render material is currently enabled for the given MaterialIndex
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual bool HasOverrideRenderMaterial(int k) const
	{
		return OverrideRenderMaterial != nullptr;
	}

	/**
	 * @return active override render material for the given MaterialIndex
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual UMaterialInterface* GetOverrideRenderMaterial(int MaterialIndex) const
	{
		return OverrideRenderMaterial;
	}

protected:
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverrideRenderMaterial = nullptr;





	//===============================================================================================================
	// Secondary Render Buffers support. This requires implementation in subclasses. It allows
	// a subset of the mesh triangles to be moved to a separate set of render buffers, which
	// can then have a separate material (eg to highlight faces), or be shown/hidden independently.
	// 
public:
	/**
	 * Set an active secondary render material. 
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void SetSecondaryRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear any active secondary render material
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void ClearSecondaryRenderMaterial();

	/**
	 * @return true if a secondary render material is set
	 */
	virtual bool HasSecondaryRenderMaterial() const
	{
		return SecondaryRenderMaterial != nullptr;
	}

	/**
	 * @return active secondary render material
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual UMaterialInterface* GetSecondaryRenderMaterial() const
	{
		return SecondaryRenderMaterial;
	}

	/**
	 * Show/Hide the secondary triangle buffers. Does not invalidate SceneProxy.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual void SetSecondaryBuffersVisibility(bool bSetVisible);

	/**
	 * @return true if secondary buffers are currently set to be visible
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual bool GetSecondaryBuffersVisibility() const;

protected:
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SecondaryRenderMaterial = nullptr;

	bool bDrawSecondaryBuffers = true;





	//===============================================================================================================
	// Standard Component interfaces
	//
public:

	// UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual FMaterialRelevance GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> BaseMaterials;
};
