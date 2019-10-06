// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshDescriptionBase.h"
#include "StaticMeshAttributes.h"
#include "UObject/UObjectGlobals.h"
#include "StaticMeshDescription.generated.h"

class UMaterial;

/**
* A wrapper for MeshDescription, customized for static meshes
*/
UCLASS(BlueprintType)
class STATICMESHDESCRIPTION_API UStaticMeshDescription : public UMeshDescriptionBase
{
public:
	GENERATED_BODY()

	/** Register attributes required by static mesh description */
	virtual void RegisterAttributes() override;

	virtual FStaticMeshAttributes& GetRequiredAttributes() override
	{ 
		return static_cast<FStaticMeshAttributes&>(*RequiredAttributes);
	}

	virtual const FStaticMeshAttributes& GetRequiredAttributes() const override
	{
		return static_cast<const FStaticMeshAttributes&>(*RequiredAttributes);
	}

	UFUNCTION(BlueprintPure, Category="MeshDescription")
	FVector2D GetVertexInstanceUV(FVertexInstanceID VertexInstanceID, int32 UVIndex = 0) const;

	UFUNCTION(BlueprintCallable, Category="MeshDescription")
	void SetVertexInstanceUV(FVertexInstanceID VertexInstanceID, FVector2D UV, int32 UVIndex = 0);

	UFUNCTION(BlueprintCallable, Category="MeshDescription")
	void CreateCube(FVector Center, FVector HalfExtents, FPolygonGroupID PolygonGroup,
					FPolygonID& PolygonID_PlusX,
					FPolygonID& PolygonID_MinusX,
					FPolygonID& PolygonID_PlusY,
					FPolygonID& PolygonID_MinusY,
					FPolygonID& PolygonID_PlusZ,
					FPolygonID& PolygonID_MinusZ);

	UFUNCTION(BlueprintCallable, Category="MeshDescription")
	void SetPolygonGroupMaterialSlotName(FPolygonGroupID PolygonGroupID, const FName& SlotName);

public:

	TVertexAttributesRef<float> GetVertexCornerSharpnesses() { return GetRequiredAttributes().GetVertexCornerSharpnesses(); }
	TVertexAttributesConstRef<float> GetVertexCornerSharpnesses() const { return GetRequiredAttributes().GetVertexCornerSharpnesses(); }

	TVertexInstanceAttributesRef<FVector2D> GetVertexInstanceUVs() { return GetRequiredAttributes().GetVertexInstanceUVs(); }
	TVertexInstanceAttributesConstRef<FVector2D> GetVertexInstanceUVs() const { return GetRequiredAttributes().GetVertexInstanceUVs(); }

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceNormals() { return GetRequiredAttributes().GetVertexInstanceNormals(); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceNormals() const { return GetRequiredAttributes().GetVertexInstanceNormals(); }

	TVertexInstanceAttributesRef<FVector> GetVertexInstanceTangents() { return GetRequiredAttributes().GetVertexInstanceTangents(); }
	TVertexInstanceAttributesConstRef<FVector> GetVertexInstanceTangents() const { return GetRequiredAttributes().GetVertexInstanceTangents(); }

	TVertexInstanceAttributesRef<float> GetVertexInstanceBinormalSigns() { return GetRequiredAttributes().GetVertexInstanceBinormalSigns(); }
	TVertexInstanceAttributesConstRef<float> GetVertexInstanceBinormalSigns() const { return GetRequiredAttributes().GetVertexInstanceBinormalSigns(); }

	TVertexInstanceAttributesRef<FVector4> GetVertexInstanceColors() { return GetRequiredAttributes().GetVertexInstanceColors(); }
	TVertexInstanceAttributesConstRef<FVector4> GetVertexInstanceColors() const { return GetRequiredAttributes().GetVertexInstanceColors(); }

	TEdgeAttributesRef<bool> GetEdgeHardnesses() { return GetRequiredAttributes().GetEdgeHardnesses(); }
	TEdgeAttributesConstRef<bool> GetEdgeHardnesses() const { return GetRequiredAttributes().GetEdgeHardnesses(); }

	TEdgeAttributesRef<float> GetEdgeCreaseSharpnesses() { return GetRequiredAttributes().GetEdgeCreaseSharpnesses(); }
	TEdgeAttributesConstRef<float> GetEdgeCreaseSharpnesses() const { return GetRequiredAttributes().GetEdgeCreaseSharpnesses(); }

	TPolygonGroupAttributesRef<FName> GetPolygonGroupMaterialSlotNames() { return GetRequiredAttributes().GetPolygonGroupMaterialSlotNames(); }
	TPolygonGroupAttributesConstRef<FName> GetPolygonGroupMaterialSlotNames() const { return GetRequiredAttributes().GetPolygonGroupMaterialSlotNames(); }
};
