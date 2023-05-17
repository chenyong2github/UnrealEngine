// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "BoneWeights.h"
#include "TransferSkinWeightsNode.generated.h"

class USkeletalMesh;

UENUM(BlueprintType)
enum class EChaosClothAssetTransferSkinWeightsMethod : uint8
{
	// For every vertex on the target mesh, find the closest point on the surface of the source mesh and copy its weights.
	ClosestPointOnSurface,
	
	// For every vertex on the target mesh, find the closest point on the surface of the source mesh. If that point 
	// is within the SearchRadius, and their normals differ by less than the NormalThreshold, then we directly copy  
	// the weights from the source point to the target mesh vertex. For all the vertices we didn't copy the weights 
	// directly, automatically compute the smooth weights.
	InpaintWeights
};


USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetTransferSkinWeightsNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetTransferSkinWeightsNode, "TransferSkinWeights", "Cloth", "Cloth Transfer Skin Weights")
	//DATAFLOW_NODE_RENDER_TYPE(FGeometryCollection::StaticType(), "Collection")  // TODO: Leave out the render type until there is something to render

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (Dataflowinput, DisplayName = "SkeletalMesh"))
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (DisplayName = "SkeletalMeshLOD"))
	int32 SkeletalMeshLOD = 0;

	// The relative transform between the SkeletalMesh and ClothCollection
	UPROPERTY(EditAnywhere, Category = "Source Mesh", Meta = (DisplayName = "Transform"))
	FTransform Transform;

	UPROPERTY(EditAnywhere, Category = "Transfer Method", Meta = (DisplayName = "Algorithm"))
	EChaosClothAssetTransferSkinWeightsMethod TransferMethod = EChaosClothAssetTransferSkinWeightsMethod::ClosestPointOnSurface;

	// Defines the search radius as the RadiusPercentage * BoundingBoxDiagonalLength. All points not within the search
	// radius will be ignored. If negative, all points are considered. Only used in the InpaintWeights algorithm.
	UPROPERTY(EditAnywhere, Category = "Transfer Method", Meta = (UIMin = -1, UIMax = 2, ClampMin = -1, ClampMax = 2, DisplayName = "Radius Percentage", EditCondition="TransferMethod==EChaosClothAssetTransferSkinWeightsMethod::InpaintWeights"))
	double RadiusPercentage = 0.05;

	// Maximum angle (in degress) difference between target and source point normals to be considred a match. 
	// If negative, normals are ignored. Only used in the InpaintWeights algorithm.
	UPROPERTY(EditAnywhere, Category = "Transfer Method", Meta = (UIMin = -1, UIMax = 180, ClampMin = -1, ClampMax = 180, DisplayName = "Normal Threshold", EditCondition="TransferMethod==EChaosClothAssetTransferSkinWeightsMethod::InpaintWeights"))
	double NormalThreshold = 30;

	FChaosClothAssetTransferSkinWeightsNode(const Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
