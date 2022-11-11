// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "SkeletalMeshAttributes.h"
#include "MeshBoneWeightFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType, meta = (DisplayName = "Bone Weights"))
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBoneWeight
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = BoneWeights)
	int32 BoneIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = BoneWeights)
	float Weight = 0;
};


USTRUCT(BlueprintType, meta = (DisplayName = "Bone Weights Profile"))
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBoneWeightProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = BoneWeights)
	FName ProfileName = FSkeletalMeshAttributes::DefaultSkinWeightProfileName;

	FName GetProfileName() const { return ProfileName; }
};



UCLASS(meta = (ScriptName = "GeometryScript_BoneWeights"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshBoneWeightFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	 * Check whether the TargetMesh has a per-vertex Bone/Skin Weight Attribute set
	 * @param bHasBoneWeights will be returned true if the requested bone weight profile exists
	 * @param Profile identifier for the bone/skin weight profile
	 */
	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries|BoneWeights", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	MeshHasBoneWeights( 
		UDynamicMesh* TargetMesh,
		bool& bHasBoneWeights,
		FGeometryScriptBoneWeightProfile Profile = FGeometryScriptBoneWeightProfile() );
	
	/**
	 * Determine the largest bone weight index that exists on the Mesh
	 * @param bHasBoneWeights will be returned true if the requested bone weight profile exists
	 * @param MaxBoneIndex maximum bone index will be returned here, or -1 if no bone indices exist
	 * @param Profile identifier for the bone/skin weight profile
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshQueries|BoneWeights", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetMaxBoneWeightIndex( 
		UDynamicMesh* TargetMesh,
		bool& bHasBoneWeights,
		int& MaxBoneIndex,
		FGeometryScriptBoneWeightProfile Profile = FGeometryScriptBoneWeightProfile() );

	/**
	 * Return an array of Bone/Skin Weights at a given vertex of TargetMesh
	 * @param VertexID requested vertex
	 * @param BoneWeights output array of bone index/weight pairs for the given Vertex
	 * @param bHasValidBoneWeights will be returned as true if the vertex has bone weights in the given profile, ie BoneWeights is valid
	 * @param Profile identifier for the bone/skin weight profile
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshQueries|BoneWeights", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetVertexBoneWeights( 
		UDynamicMesh* TargetMesh,
		int VertexID,
		TArray<FGeometryScriptBoneWeight>& BoneWeights,
		bool& bHasValidBoneWeights,
		FGeometryScriptBoneWeightProfile Profile = FGeometryScriptBoneWeightProfile() );

	/**
	 * Return the Bone/Skin Weight with the maximum weight at a given vertex of TargetMesh
	 * @param VertexID requested vertex
	 * @param BoneWeight the bone index and weight that was found to have the maximum weight
	 * @param bHasValidBoneWeights will be returned as true if the vertex has bone weights in the given profile and a largest weight was found
	 * @param Profile identifier for the bone/skin weight profile
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshQueries|BoneWeights", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetLargestVertexBoneWeight( 
		UDynamicMesh* TargetMesh,
		int VertexID,
		FGeometryScriptBoneWeight& BoneWeight,
		bool& bHasValidBoneWeights,
		FGeometryScriptBoneWeightProfile Profile = FGeometryScriptBoneWeightProfile() );

	/**
	 * Set the Bone/Skin Weights at a given vertex of TargetMesh
	 * @param VertexID vertex to update
	 * @param BoneWeights input array of bone index/weight pairs for the Vertex
	 * @param bIsValidVertexID will be returned as true if the vertex ID is valid
	 * @param Profile identifier for the bone/skin weight profile
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshQueries|BoneWeights", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetVertexBoneWeights( 
		UDynamicMesh* TargetMesh,
		int VertexID,
		const TArray<FGeometryScriptBoneWeight>& BoneWeights,
		bool& bIsValidVertexID,
		FGeometryScriptBoneWeightProfile Profile = FGeometryScriptBoneWeightProfile() );


};