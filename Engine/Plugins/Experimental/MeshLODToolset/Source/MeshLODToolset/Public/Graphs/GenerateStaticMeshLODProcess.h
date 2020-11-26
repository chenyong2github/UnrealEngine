// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/StaticMesh.h"

#include "DynamicMesh3.h"
#include "Image/ImageBuilder.h"

#include "Graphs/GenerateMeshLODGraph.h"

class UTexture2D;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;

class FGenerateStaticMeshLODProcess
{
public:

	bool Initialize(UStaticMesh* SourceMesh);


	UStaticMesh* GetSourceStaticMesh() const { return SourceStaticMesh; }
	const FDynamicMesh3& GetSourceMesh() const { return SourceMesh; }

	const FString& GetSourceAssetPath() const { return SourceAssetPath; }
	const FString& GetSourceAssetFolder() const { return SourceAssetFolder; }
	const FString& GetSourceAssetName() const { return SourceAssetName; }
	static FString GetDefaultDerivedAssetSuffix() { return TEXT("_AutoLOD"); }

	void CalculateDerivedPathName(FString NewAssetSuffix);

	bool ComputeDerivedSourceData();
	const FDynamicMesh3& GetDerivedLOD0Mesh() const { return DerivedLODMesh; }
	const FMeshTangentsd& GetDerivedLOD0MeshTangents() const { return DerivedLODMeshTangents; }


	/**
	 * Creates new Asset
	 */
	bool WriteDerivedAssetData();


	/**
	 * Updates existing SM Asset
	 */
	void UpdateSourceAsset();



	struct FPreviewMaterials
	{
		TArray<UMaterialInterface*> Materials;
		TArray<UTexture2D*> Textures;
	};
	void GetDerivedMaterialsPreview(FPreviewMaterials& MaterialSetOut);


protected:

	UStaticMesh* SourceStaticMesh;
	FString SourceAssetPath;
	FString SourceAssetFolder;
	FString SourceAssetName;

	FDynamicMesh3 SourceMesh;

	struct FTextureInfo
	{
		UTexture2D* SourceTexture;
		FName ParameterName;
		FImageDimensions Dimensions;
		TImageBuilder<FVector4f> Image;
		bool bIsNormalMap = false;
		bool bIsDefaultTexture = false;
		bool bShouldBakeTexture = false;
	};

	struct FMaterialInfo
	{
		FStaticMaterial SourceMaterial;
		TArray<FTextureInfo> SourceTextures;
	};

	TArray<FMaterialInfo> SourceMaterials;



	FString DerivedSuffix;
	FString DerivedAssetFolder;
	FString DerivedAssetPath;

	FDynamicMesh3 DerivedLODMesh;
	FMeshTangentsd DerivedLODMeshTangents;
	FSimpleShapeSet3d DerivedCollision;
	UE::GeometryFlow::FNormalMapImage DerivedNormalMapImage;
	TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>> DerivedTextureImages;
	TMap<UTexture2D*, int32> TextureToDerivedTexIndex;

	TArray<FMaterialInfo> DerivedMaterials;
	UTexture2D* DerivedNormalMapTex;

	TUniquePtr<FGenerateMeshLODGraph> Generator;
	bool InitializeGenerator();

	bool WriteDerivedTexture(UTexture2D* SourceTexture, UTexture2D* DerivedTexture);
	bool WriteDerivedTexture(UTexture2D* DerivedTexture, FString BaseTexName);
	void WriteDerivedTextures();
	void WriteDerivedMaterials();
	void UpdateMaterialTextureParameters(UMaterialInstanceConstant* Material, FMaterialInfo& DerivedMaterialInfo);
	void WriteDerivedStaticMeshAsset();

	void UpdateSourceStaticMeshAsset();

	void UpdateMaterialTextureParameters(UMaterialInstanceDynamic* Material, const FMaterialInfo& SourceMaterialInfo, 
		const TMap<UTexture2D*,UTexture2D*>& PreviewTextures, UTexture2D* PreviewNormalMap);
};