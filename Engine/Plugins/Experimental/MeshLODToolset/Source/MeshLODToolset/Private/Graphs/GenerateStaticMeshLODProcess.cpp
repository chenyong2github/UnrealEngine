// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graphs/GenerateStaticMeshLODProcess.h"

#include "MeshLODToolsetModule.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "DynamicMeshAttributeSet.h"

#include "AssetUtils/Texture2DUtil.h"
#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/MeshDescriptionUtil.h"

#include "Physics/PhysicsDataCollection.h"

#include "Misc/Paths.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "FileHelpers.h"

#include "Engine/Engine.h"
#include "Editor.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "WeightMapUtil.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "GeometryFlowTypes.h"

#define LOCTEXT_NAMESPACE "UGenerateStaticMeshLODProcess"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;
using namespace UE::GeometryFlow;

namespace
{
#if WITH_EDITOR
	static EAsyncExecution GenerateSMLODAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution GenerateSMLODAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif
}



struct FReadTextureJob
{
	int32 MatIndex;
	int32 TexIndex;
};


namespace GenerateStaticMeshLODProcessHelpers
{
	// Given "xxxxx" returns "xxxxx_1"
	// Given "xxxxx_1" returns "xxxxx_2"
	// etc.
	// If anything goes wrong, just add "_1" to the end
	void AppendOrIncrementSuffix(FString& S)
	{
		TArray<FString> Substrings;
		S.ParseIntoArray(Substrings, TEXT("_"));
		if ( Substrings.Num() <= 1 ) 
		{
			S += TEXT("_1");
		}
		else
		{
			FString LastSubstring = Substrings.Last();
			int32 Num;
			bool bParsed = LexTryParseString(Num, *LastSubstring);
			if (bParsed)
			{
				++Num;
				Substrings.RemoveAt(Substrings.Num() - 1);
				S = FString::Join(Substrings, TEXT("_"));
				S += TEXT("_") + FString::FromInt(Num);
			}
			else
			{
				S += TEXT("_1");
			}
		}
	}

	TArray<int32> FindUnreferencedMaterials(UStaticMesh* StaticMesh, const FMeshDescription* MeshDescription)
	{
		const TArray<FStaticMaterial>& MaterialSet = StaticMesh->GetStaticMaterials();
		const int32 NumMaterials = MaterialSet.Num();

		auto IsValidMaterial = [&MaterialSet](int32 MaterialID) {
			return MaterialSet[MaterialID].MaterialInterface != nullptr;
		};

		// Initially flag only valid materials as potentially unused.
		TArray<bool> MatUnusedFlags;
		MatUnusedFlags.SetNum(NumMaterials);
		int32 NumMatUnused = 0;
		for (int32 MaterialID = 0; MaterialID < NumMaterials; ++MaterialID)
		{
			MatUnusedFlags[MaterialID] = IsValidMaterial(MaterialID);
			NumMatUnused += MatUnusedFlags[MaterialID];
		}

		TMap<FPolygonGroupID, int32> PolygonGroupToMaterialIndex;
		const FStaticMeshConstAttributes Attributes(*MeshDescription);
		TPolygonGroupAttributesConstRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		for (FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
		{
			int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
			if (MaterialIndex == INDEX_NONE)
			{
				MaterialIndex = PolygonGroupID.GetValue();
			}
			PolygonGroupToMaterialIndex.Add(PolygonGroupID, MaterialIndex);
		}

		for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
		{
			const FPolygonGroupID PolygonGroupID = MeshDescription->GetTrianglePolygonGroup(TriangleID);
			const int32 MaterialIndex = PolygonGroupToMaterialIndex[PolygonGroupID];
			bool& bMatUnusedFlag = MatUnusedFlags[MaterialIndex];
			NumMatUnused -= static_cast<int32>(bMatUnusedFlag);
			bMatUnusedFlag = false;
			if (NumMatUnused == 0)
			{
				break;
			}
		}

		TArray<int32> UnreferencedMaterials;
		UnreferencedMaterials.Reserve(NumMatUnused);
		for (int32 MaterialID = 0; MaterialID < NumMaterials; ++MaterialID)
		{
			if (MatUnusedFlags[MaterialID])
			{
				UnreferencedMaterials.Emplace(MaterialID);
			}
		}
		return UnreferencedMaterials;
	}
}

bool UGenerateStaticMeshLODProcess::Initialize(UStaticMesh* StaticMeshIn, FProgressCancel* Progress)
{
	if (!ensure(StaticMeshIn)) return false;
	if (!ensure(StaticMeshIn->GetNumSourceModels() > 0)) return false;

	// make sure we are not in rendering
	FlushRenderingCommands();

	SourceStaticMesh = StaticMeshIn;

	bUsingHiResSource = SourceStaticMesh->IsHiResMeshDescriptionValid();
	const FMeshDescription* UseSourceMeshDescription =
		(bUsingHiResSource) ? SourceStaticMesh->GetHiResMeshDescription() : SourceStaticMesh->GetMeshDescription(0);
	SourceMeshDescription = MakeShared<FMeshDescription>();
	*SourceMeshDescription = *UseSourceMeshDescription;

	// if not the high-res source, compute autogenerated normals/tangents
	if (bUsingHiResSource == false)
	{
		UE::MeshDescription::InitializeAutoGeneratedAttributes(*SourceMeshDescription, SourceStaticMesh, 0);
	}

	// start async mesh-conversion
	SourceMesh.Clear();
	TFuture<void> ConvertMesh = Async(GenerateSMLODAsyncExecTarget, [&]()
	{
		FMeshDescriptionToDynamicMesh GetSourceMesh;
		GetSourceMesh.Convert(SourceMeshDescription.Get(), SourceMesh);
	});

	// get list of source materials and find all the input texture params
	const TArray<FStaticMaterial>& Materials = SourceStaticMesh->GetStaticMaterials();

	// warn the user if there are any unsed materials in the mesh
	if (Progress)
	{
		for (int32 UnusedMaterialIndex : GenerateStaticMeshLODProcessHelpers::FindUnreferencedMaterials(SourceStaticMesh, SourceMeshDescription.Get()))
		{
			const TObjectPtr<class UMaterialInterface> MaterialInterface = Materials[UnusedMaterialIndex].MaterialInterface;
			if (ensure(MaterialInterface != nullptr))
			{
				FText WarningText = FText::Format(LOCTEXT("UnusedMaterialWarning", "Found an unused material ({0}). Consider removing it before using this tool."),
					                              FText::FromName(MaterialInterface->GetFName()));
				UE_LOG(LogMeshLODToolset, Warning, TEXT("%s"), *WarningText.ToString());
				Progress->AddWarning(WarningText, FProgressCancel::EMessageLevel::UserWarning);
			}
		}
	}


	SourceMaterials.SetNum(Materials.Num());
	TArray<FReadTextureJob> ReadJobs;
	for (int32 mi = 0; mi < Materials.Num(); ++mi)
	{
		SourceMaterials[mi].SourceMaterial = Materials[mi];

		UMaterialInterface* MaterialInterface = Materials[mi].MaterialInterface;
		if (MaterialInterface == nullptr)
		{
			continue;
		}

		// detect hard-coded (non-parameter) texture samples
		{
			UMaterial* Material = Materials[mi].MaterialInterface->GetMaterial();

			// go over the nodes in the material graph looking for texture samples
			const UMaterialGraph* MatGraph = Material->MaterialGraph;

			if (MatGraph == nullptr)	// create a material graph from the material if necessary
			{
				UMaterialGraph* NewMatGraph = CastChecked<UMaterialGraph>(NewObject<UEdGraph>(Material, UMaterialGraph::StaticClass(), NAME_None, RF_Transient));
				NewMatGraph->Material = Material;
				NewMatGraph->RebuildGraph();
				MatGraph = NewMatGraph;
			}

			bool bFoundTextureNonParamExpession = false;
			const TArray<TObjectPtr<class UEdGraphNode>>& Nodes = MatGraph->Nodes;
			for (const TObjectPtr<UEdGraphNode>& Node : Nodes)
			{
				const UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(Node);
				if (GraphNode)
				{
					const UMaterialExpressionTextureSampleParameter* TextureSampleParameterBase = Cast<UMaterialExpressionTextureSampleParameter>(GraphNode->MaterialExpression);
					if (!TextureSampleParameterBase)
					{
						const UMaterialExpressionTextureSample* TextureSampleBase = Cast<UMaterialExpressionTextureSample>(GraphNode->MaterialExpression);
						if (TextureSampleBase)
						{
							// node is UMaterialExpressionTextureSample but not UMaterialExpressionTextureSampleParameter
							bFoundTextureNonParamExpession = true;
							break;
						}
					}

				}
			}
			if (bFoundTextureNonParamExpession)
			{
				FText WarningText = FText::Format(LOCTEXT("NonParameterTextureWarning", "Non-parameter texture sampler detected in input material [{0}]. Output materials may have unexpected behaviour."),
												  FText::FromString(Material->GetName()));
				UE_LOG(LogMeshLODToolset, Warning, TEXT("%s"), *WarningText.ToString());
				if (Progress)
				{
					Progress->AddWarning(WarningText, FProgressCancel::EMessageLevel::UserWarning);
				}
			}
		}

		SourceMaterials[mi].bHasNormalMap = false;
		SourceMaterials[mi].bHasTexturesToBake = false;

		TArray<FMaterialParameterInfo> ParameterInfo;
		TArray<FGuid> ParameterIds;
		MaterialInterface->GetAllTextureParameterInfo(ParameterInfo, ParameterIds);
		for (int32 ti = 0; ti < ParameterInfo.Num(); ++ti)
		{
			FName ParamName = ParameterInfo[ti].Name;

			UTexture* CurTexture = nullptr;
			bool bFound = MaterialInterface->GetTextureParameterValue(
				FMemoryImageMaterialParameterInfo(ParameterInfo[ti]), CurTexture);
			if (ensure(bFound))
			{
				if (Cast<UTexture2D>(CurTexture) != nullptr)
				{
					FTextureInfo TexInfo;
					TexInfo.Texture = Cast<UTexture2D>(CurTexture);
					TexInfo.ParameterName = ParamName;

					TexInfo.bIsNormalMap = TexInfo.Texture->IsNormalMap();
					SourceMaterials[mi].bHasNormalMap |= TexInfo.bIsNormalMap;

					TexInfo.bIsDefaultTexture = UEditorAssetLibrary::GetPathNameForLoadedAsset(TexInfo.Texture).StartsWith(TEXT("/Engine/"));

					TexInfo.bShouldBakeTexture = (TexInfo.bIsNormalMap == false && TexInfo.bIsDefaultTexture == false);
					if (TexInfo.bShouldBakeTexture)
					{
						ReadJobs.Add(FReadTextureJob{ mi, SourceMaterials[mi].SourceTextures.Num() });

						SourceMaterials[mi].bHasTexturesToBake = true;
					}

					SourceMaterials[mi].SourceTextures.Add(TexInfo);
				}
			}
		}

		// if material does not have a normal map parameter or any textures we want to bake, we can just re-use it
		SourceMaterials[mi].bIsReusable = (SourceMaterials[mi].bHasNormalMap == false && SourceMaterials[mi].bHasTexturesToBake == false);
	}


	// If we have hi-res source we can discard any materials that are only used on the previously-generated LOD0.
	// We cannot explicitly tag the materials as being generated so we infer, ie we assume a material was generated
	// if it is only used in LOD0 and not HiRes
	if (bUsingHiResSource)
	{
		// have to wait for SourceMesh to finish converting
		ConvertMesh.Wait();

		const FMeshDescription* LOD0MeshDescription = SourceStaticMesh->GetMeshDescription(0);
		FMeshDescriptionToDynamicMesh GetLOD0Mesh;
		FDynamicMesh3 LOD0Mesh;
		GetLOD0Mesh.Convert(LOD0MeshDescription, LOD0Mesh);
		const FDynamicMeshMaterialAttribute* SourceMaterialIDs = SourceMesh.Attributes()->GetMaterialID();
		const FDynamicMeshMaterialAttribute* LOD0MaterialIDs = LOD0Mesh.Attributes()->GetMaterialID();
		if (ensure(SourceMaterialIDs != nullptr && LOD0MaterialIDs != nullptr))
		{
			int32 NumMaterials = SourceMaterials.Num();
			TArray<bool> IsBaseMaterial;
			IsBaseMaterial.Init(false, NumMaterials);
			TArray<bool> IsLOD0Material;
			IsLOD0Material.Init(false, NumMaterials);

			for (int32 tid : SourceMesh.TriangleIndicesItr())
			{
				int32 MatIdx = SourceMaterialIDs->GetValue(tid);
				if (MatIdx >= 0 && MatIdx < NumMaterials)
				{
					IsBaseMaterial[MatIdx] = true;
				}
			}
			for (int32 tid : LOD0Mesh.TriangleIndicesItr())
			{
				int32 MatIdx = LOD0MaterialIDs->GetValue(tid);
				if (MatIdx >= 0 && MatIdx < NumMaterials)
				{
					IsLOD0Material[MatIdx] = true;
				}
			}

			for (int32 k = 0; k < NumMaterials; ++k)
			{
				if (IsLOD0Material[k] == true && IsBaseMaterial[k] == false)
				{
					SourceMaterials[k].bIsPreviouslyGeneratedMaterial = true;
					SourceMaterials[k].bHasTexturesToBake = false;
					SourceMaterials[k].bIsReusable = false;
				}
			}
		}
	}



	// extract all the texture params
	// TODO: this triggers a checkSlow in the serialization code when it runs async. Find out why. Jira: UETOOL-2985
	//TFuture<void> ReadTextures = Async(GenerateSMLODAsyncExecTarget, [&]()
	//{
	//	ParallelFor(ReadJobs.Num(), [&](int32 ji)
	//	{
	//		FReadTextureJob job = ReadJobs[ji];
	//		FTextureInfo& TexInfo = SourceMaterials[job.MatIndex].SourceTextures[job.TexIndex];
	//		UE::AssetUtils::ReadTexture(TexInfo.Texture, TexInfo.Dimensions, TexInfo.Image);
	//	});
	//});

	// single-thread path
	for (FReadTextureJob job : ReadJobs)
	{
		// only read textures that are from materials we are going to possibly bake
		FSourceMaterialInfo& SourceMaterial = SourceMaterials[job.MatIndex];
		if (SourceMaterial.bIsPreviouslyGeneratedMaterial == false && SourceMaterial.bIsReusable == false)
		{
			FTextureInfo& TexInfo = SourceMaterials[job.MatIndex].SourceTextures[job.TexIndex];
			UE::AssetUtils::ReadTexture(TexInfo.Texture, TexInfo.Dimensions, TexInfo.Image);
		}
	}


	ConvertMesh.Wait();
	//ReadTextures.Wait();


	FString FullPathWithExtension = UEditorAssetLibrary::GetPathNameForLoadedAsset(SourceStaticMesh);
	SourceAssetPath = FPaths::GetBaseFilename(FullPathWithExtension, false);
	SourceAssetFolder = FPaths::GetPath(SourceAssetPath);
	SourceAssetName = FPaths::GetBaseFilename(FullPathWithExtension, true);

	CalculateDerivedPathName(SourceAssetName, GetDefaultDerivedAssetSuffix());

	InitializeGenerator();

	return true;
}


void UGenerateStaticMeshLODProcess::CalculateDerivedPathName(const FString& NewAssetBaseName, const FString& NewAssetSuffix)
{
	DerivedAssetNameNoSuffix = FPaths::MakeValidFileName(NewAssetBaseName);
	if (DerivedAssetNameNoSuffix.Len() == 0)
	{
		DerivedAssetNameNoSuffix = SourceAssetName;
	}

	DerivedSuffix = FPaths::MakeValidFileName(NewAssetSuffix);
	if (DerivedSuffix.Len() == 0)
	{
		DerivedSuffix = GetDefaultDerivedAssetSuffix();
	}

	DerivedAssetName = FString::Printf(TEXT("%s%s"), *DerivedAssetNameNoSuffix, *DerivedSuffix);
	DerivedAssetFolder = SourceAssetFolder;
	DerivedAssetPath = FPaths::Combine(DerivedAssetFolder, DerivedAssetName);
}


bool UGenerateStaticMeshLODProcess::InitializeGenerator()
{
	Generator = MakeUnique<FGenerateMeshLODGraph>();
	Generator->BuildGraph();

	// initialize source textures that need to be baked
	SourceTextureToDerivedTexIndex.Reset();
	for (const FSourceMaterialInfo& MatInfo : SourceMaterials)
	{
		if ( MatInfo.bIsPreviouslyGeneratedMaterial == false && MatInfo.bIsReusable == false && MatInfo.bHasTexturesToBake)
		{
			for (const FTextureInfo& TexInfo : MatInfo.SourceTextures)
			{
				if (TexInfo.bShouldBakeTexture &&
					(SourceTextureToDerivedTexIndex.Contains(TexInfo.Texture) == false))
				{
					int32 NewIndex = Generator->AppendTextureBakeNode(TexInfo.Image, TexInfo.Texture->GetName());
					SourceTextureToDerivedTexIndex.Add(TexInfo.Texture, NewIndex);
				}
			}
		}
	}

	// initialize source mesh
	Generator->SetSourceMesh(this->SourceMesh);


	// read back default settings

	CurrentSettings.FilterGroupLayer = Generator->GetCurrentPreFilterSettings().FilterGroupLayerName;

	CurrentSettings.SolidifyVoxelResolution = Generator->GetCurrentSolidifySettings().VoxelResolution;
	CurrentSettings.WindingThreshold = Generator->GetCurrentSolidifySettings().WindingThreshold;

	//CurrentSettings.MorphologyVoxelResolution = Generator->GetCurrentMorphologySettings().VoxelResolution;
	CurrentSettings.ClosureDistance = Generator->GetCurrentMorphologySettings().Distance;

	CurrentSettings.SimplifyTriangleCount = Generator->GetCurrentSimplifySettings().TargetCount;

	CurrentSettings.BakeResolution = (EGenerateStaticMeshLODBakeResolution)Generator->GetCurrentBakeCacheSettings().Dimensions.GetWidth();
	CurrentSettings.BakeThickness = Generator->GetCurrentBakeCacheSettings().Thickness;

	
	const UE::GeometryFlow::FGenerateSimpleCollisionSettings& SimpleCollisionSettings = Generator->GetCurrentGenerateSimpleCollisionSettings();
	CurrentSettings.CollisionType = static_cast<EGenerateStaticMeshLODSimpleCollisionGeometryType>(SimpleCollisionSettings.Type);
	CurrentSettings.ConvexTriangleCount = SimpleCollisionSettings.ConvexHullSettings.SimplifyToTriangleCount;
	CurrentSettings.bPrefilterVertices = SimpleCollisionSettings.ConvexHullSettings.bPrefilterVertices;
	CurrentSettings.PrefilterGridResolution = SimpleCollisionSettings.ConvexHullSettings.PrefilterGridResolution;
	CurrentSettings.bSimplifyPolygons = SimpleCollisionSettings.SweptHullSettings.bSimplifyPolygons;
	CurrentSettings.HullTolerance = SimpleCollisionSettings.SweptHullSettings.HullTolerance;

	FMeshSimpleShapeApproximation::EProjectedHullAxisMode RHSMode = SimpleCollisionSettings.SweptHullSettings.SweepAxis;
	CurrentSettings.SweepAxis = static_cast<EGenerateStaticMeshLODProjectedHullAxisMode>(RHSMode);


	return true;
}


void UGenerateStaticMeshLODProcess::UpdateSettings(const FGenerateStaticMeshLODProcessSettings& NewCombinedSettings)
{

	if (NewCombinedSettings.FilterGroupLayer != CurrentSettings.FilterGroupLayer)
	{
		FMeshLODGraphPreFilterSettings NewPreFilterSettings = Generator->GetCurrentPreFilterSettings();
		NewPreFilterSettings.FilterGroupLayerName = NewCombinedSettings.FilterGroupLayer;
		Generator->UpdatePreFilterSettings(NewPreFilterSettings);
	}

	if (NewCombinedSettings.ThickenAmount != CurrentSettings.ThickenAmount)
	{
		UE::GeometryFlow::FMeshThickenSettings NewThickenSettings = Generator->GetCurrentThickenSettings();
		NewThickenSettings.ThickenAmount = NewCombinedSettings.ThickenAmount;
		Generator->UpdateThickenSettings(NewThickenSettings);
	}

	if (NewCombinedSettings.ThickenWeightMapName != CurrentSettings.ThickenWeightMapName)
	{
		FIndexedWeightMap1f WeightMap;
		float DefaultValue = 0.0f;
		bool bOK = UE::WeightMaps::GetVertexWeightMap(SourceMeshDescription.Get(), NewCombinedSettings.ThickenWeightMapName, WeightMap, DefaultValue);

		if (bOK)
		{
			Generator->UpdateThickenWeightMap(WeightMap.Values);
		}
		else
		{
			Generator->UpdateThickenWeightMap(TArray<float>());
		}
	}

	

	bool bSharedVoxelResolutionChanged = (NewCombinedSettings.SolidifyVoxelResolution != CurrentSettings.SolidifyVoxelResolution);
	if ( bSharedVoxelResolutionChanged
		|| (NewCombinedSettings.WindingThreshold != CurrentSettings.WindingThreshold))
	{
		UE::GeometryFlow::FMeshSolidifySettings NewSolidifySettings = Generator->GetCurrentSolidifySettings();
		NewSolidifySettings.VoxelResolution = NewCombinedSettings.SolidifyVoxelResolution;
		NewSolidifySettings.WindingThreshold = NewCombinedSettings.WindingThreshold;
		Generator->UpdateSolidifySettings(NewSolidifySettings);
	}


	if ( bSharedVoxelResolutionChanged
		|| (NewCombinedSettings.ClosureDistance != CurrentSettings.ClosureDistance))
	{
		UE::GeometryFlow::FVoxClosureSettings NewClosureSettings = Generator->GetCurrentMorphologySettings();
		NewClosureSettings.VoxelResolution = NewCombinedSettings.SolidifyVoxelResolution;
		NewClosureSettings.Distance = NewCombinedSettings.ClosureDistance;
		Generator->UpdateMorphologySettings(NewClosureSettings);
	}


	if (NewCombinedSettings.SimplifyTriangleCount != CurrentSettings.SimplifyTriangleCount)
	{
		UE::GeometryFlow::FMeshSimplifySettings NewSimplifySettings = Generator->GetCurrentSimplifySettings();
		NewSimplifySettings.TargetCount = NewCombinedSettings.SimplifyTriangleCount;
		Generator->UpdateSimplifySettings(NewSimplifySettings);
	}

	if (NewCombinedSettings.NumAutoUVCharts != CurrentSettings.NumAutoUVCharts)
	{
		UE::GeometryFlow::FMeshAutoGenerateUVsSettings NewAutoUVSettings = Generator->GetCurrentAutoUVSettings();
		NewAutoUVSettings.NumCharts = NewCombinedSettings.NumAutoUVCharts;
		Generator->UpdateAutoUVSettings(NewAutoUVSettings);
	}


	if ( (NewCombinedSettings.BakeResolution != CurrentSettings.BakeResolution) ||
		 (NewCombinedSettings.BakeThickness != CurrentSettings.BakeThickness))
	{
		UE::GeometryFlow::FMeshMakeBakingCacheSettings NewBakeSettings = Generator->GetCurrentBakeCacheSettings();
		NewBakeSettings.Dimensions = FImageDimensions((int32)NewCombinedSettings.BakeResolution, (int32)NewCombinedSettings.BakeResolution);
		NewBakeSettings.Thickness = NewCombinedSettings.BakeThickness;
		Generator->UpdateBakeCacheSettings(NewBakeSettings);
	}

	if (NewCombinedSettings.CollisionGroupLayerName != CurrentSettings.CollisionGroupLayerName)
	{
		Generator->UpdateCollisionGroupLayerName(NewCombinedSettings.CollisionGroupLayerName);
	}

	if (NewCombinedSettings.ConvexTriangleCount != CurrentSettings.ConvexTriangleCount ||
		NewCombinedSettings.bPrefilterVertices != CurrentSettings.bPrefilterVertices ||
		NewCombinedSettings.PrefilterGridResolution != CurrentSettings.PrefilterGridResolution ||
		NewCombinedSettings.bSimplifyPolygons != CurrentSettings.bSimplifyPolygons ||
		NewCombinedSettings.HullTolerance != CurrentSettings.HullTolerance ||
		NewCombinedSettings.SweepAxis != CurrentSettings.SweepAxis ||
		NewCombinedSettings.CollisionType != CurrentSettings.CollisionType)
	{
		UE::GeometryFlow::FGenerateSimpleCollisionSettings NewGenCollisionSettings = Generator->GetCurrentGenerateSimpleCollisionSettings();
		NewGenCollisionSettings.Type = static_cast<UE::GeometryFlow::ESimpleCollisionGeometryType>(NewCombinedSettings.CollisionType);
		NewGenCollisionSettings.ConvexHullSettings.SimplifyToTriangleCount = NewCombinedSettings.ConvexTriangleCount;
		NewGenCollisionSettings.ConvexHullSettings.bPrefilterVertices = NewCombinedSettings.bPrefilterVertices;
		NewGenCollisionSettings.ConvexHullSettings.PrefilterGridResolution = NewCombinedSettings.PrefilterGridResolution;
		NewGenCollisionSettings.SweptHullSettings.bSimplifyPolygons = NewCombinedSettings.bSimplifyPolygons;
		NewGenCollisionSettings.SweptHullSettings.HullTolerance = NewCombinedSettings.HullTolerance;
		NewGenCollisionSettings.SweptHullSettings.SweepAxis = static_cast<FMeshSimpleShapeApproximation::EProjectedHullAxisMode>(NewCombinedSettings.SweepAxis);
		Generator->UpdateGenerateSimpleCollisionSettings(NewGenCollisionSettings);
	}

	CurrentSettings = NewCombinedSettings;
}




bool UGenerateStaticMeshLODProcess::ComputeDerivedSourceData(FProgressCancel* Progress)
{
	DerivedTextureImages.Reset();

	Generator->EvaluateResult(
		this->DerivedLODMesh,
		this->DerivedLODMeshTangents,
		this->DerivedCollision,
		this->DerivedNormalMapImage,
		this->DerivedTextureImages,
		Progress);

	if (Progress && Progress->Cancelled())
	{
		return false;
	}

	// copy materials
	int32 NumMaterials = SourceMaterials.Num();
	DerivedMaterials.SetNum(NumMaterials);
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		DerivedMaterials[mi].SourceMaterialIndex = mi;
		DerivedMaterials[mi].bUseSourceMaterialDirectly = (SourceMaterials[mi].bIsReusable || SourceMaterials[mi].bIsPreviouslyGeneratedMaterial);
		
		if (DerivedMaterials[mi].bUseSourceMaterialDirectly)
		{
			DerivedMaterials[mi].DerivedMaterial = SourceMaterials[mi].SourceMaterial;
		}
		else
		{
			// TODO this is a lot of wasted overhead, we do not need to copy images here for example
			DerivedMaterials[mi].DerivedTextures = SourceMaterials[mi].SourceTextures;
		}
	}

	// update texture image data in derived materials
	for (FDerivedMaterialInfo& MatInfo : DerivedMaterials)
	{
		for (FTextureInfo& TexInfo : MatInfo.DerivedTextures)
		{
			if (SourceTextureToDerivedTexIndex.Contains(TexInfo.Texture))
			{
				int32 BakedTexIndex = SourceTextureToDerivedTexIndex[TexInfo.Texture];
				const TUniquePtr<UE::GeometryFlow::FTextureImage>& DerivedTex = DerivedTextureImages[BakedTexIndex];
				TexInfo.Dimensions = DerivedTex->Image.GetDimensions();

				// Cannot currently MoveTemp here because this Texture may appear in multiple Materials, 
				// and currently we do not handle that. The Materials need to learn how to share.
				//TexInfo.Image = MoveTemp(DerivedTex->Image);
				TexInfo.Image = DerivedTex->Image;
			}
		}
	}

	return true;
}




void UGenerateStaticMeshLODProcess::GetDerivedMaterialsPreview(FPreviewMaterials& MaterialSetOut)
{
	// force garbage collection of outstanding preview materials
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);


	// create derived textures
	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	TMap<UTexture2D*, UTexture2D*> SourceToPreviewTexMap;
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FSourceMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		if (SourceMaterialInfo.bIsReusable || SourceMaterialInfo.bIsPreviouslyGeneratedMaterial)
		{
			continue;
		}

		const FDerivedMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		int32 NumTextures = SourceMaterialInfo.SourceTextures.Num();
		check(DerivedMaterialInfo.DerivedTextures.Num() == NumTextures);
		for (int32 ti = 0; ti < NumTextures; ++ti)
		{
			const FTextureInfo& SourceTex = SourceMaterialInfo.SourceTextures[ti];
			bool bConvertToSRGB = SourceTex.Texture->SRGB;
			const FTextureInfo& DerivedTex = DerivedMaterialInfo.DerivedTextures[ti];
			if (DerivedTex.bShouldBakeTexture)
			{
				FTexture2DBuilder TextureBuilder;
				TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, DerivedTex.Dimensions);
				TextureBuilder.GetTexture2D()->SRGB = bConvertToSRGB;
				TextureBuilder.Copy(DerivedTex.Image, bConvertToSRGB);
				TextureBuilder.Commit(false);
				UTexture2D* PreviewTex = TextureBuilder.GetTexture2D();
				if (ensure(PreviewTex))
				{
					SourceToPreviewTexMap.Add(SourceTex.Texture, PreviewTex);
					MaterialSetOut.Textures.Add(PreviewTex);
				}
			}
		}
	}

	// create derived normal map texture
	FTexture2DBuilder NormapMapBuilder;
	NormapMapBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, DerivedNormalMapImage.Image.GetDimensions());
	NormapMapBuilder.Copy(DerivedNormalMapImage.Image, false);
	NormapMapBuilder.Commit(false);
	UTexture2D* PreviewNormalMapTex = NormapMapBuilder.GetTexture2D();
	MaterialSetOut.Textures.Add(PreviewNormalMapTex);

	// create derived MIDs and point to new textures
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FSourceMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		UMaterialInterface* MaterialInterface = SourceMaterialInfo.SourceMaterial.MaterialInterface;

		if (SourceMaterialInfo.bIsReusable || SourceMaterialInfo.bIsPreviouslyGeneratedMaterial)
		{
			MaterialSetOut.Materials.Add(MaterialInterface);
		}
		else
		{
			// TODO: we should cache these instead of re-creating every time??
			UMaterialInstanceDynamic* GeneratedMID = UMaterialInstanceDynamic::Create(MaterialInterface, NULL);

			// rewrite texture parameters to new textures
			UpdateMaterialTextureParameters(GeneratedMID, SourceMaterialInfo, SourceToPreviewTexMap, PreviewNormalMapTex);

			MaterialSetOut.Materials.Add(GeneratedMID);
		}
	}

}


void UGenerateStaticMeshLODProcess::UpdateMaterialTextureParameters(
	UMaterialInstanceDynamic* Material, 
	const FSourceMaterialInfo& SourceMaterialInfo,
	const TMap<UTexture2D*, UTexture2D*>& PreviewTextures, 
	UTexture2D* PreviewNormalMap)
{
	Material->Modify();
	int32 NumTextures = SourceMaterialInfo.SourceTextures.Num();
	for (int32 ti = 0; ti < NumTextures; ++ti)
	{
		const FTextureInfo& SourceTex = SourceMaterialInfo.SourceTextures[ti];

		if (SourceTex.bIsNormalMap)
		{
			if (ensure(PreviewNormalMap))
			{
				FMaterialParameterInfo ParamInfo(SourceTex.ParameterName);
				Material->SetTextureParameterValueByInfo(ParamInfo, PreviewNormalMap);
			}
		}
		else if (SourceTex.bShouldBakeTexture)
		{
			UTexture2D*const* FoundTexture = PreviewTextures.Find(SourceTex.Texture);
			if (ensure(FoundTexture))
			{
				FMaterialParameterInfo ParamInfo(SourceTex.ParameterName);
				Material->SetTextureParameterValueByInfo(ParamInfo, *FoundTexture);
			}
		}
	}
	Material->PostEditChange();
}





bool UGenerateStaticMeshLODProcess::WriteDerivedAssetData()
{
	AllDerivedTextures.Reset();

	constexpr bool bCreatingNewStaticMeshAsset = true;

	WriteDerivedTextures(bCreatingNewStaticMeshAsset);

	WriteDerivedMaterials(bCreatingNewStaticMeshAsset);

	WriteDerivedStaticMeshAsset();

	// clear list of derived textures we were holding onto to prevent GC
	AllDerivedTextures.Reset();

	return true;
}


void UGenerateStaticMeshLODProcess::UpdateSourceAsset(bool bSetNewHDSourceAsset)
{
	AllDerivedTextures.Reset();

	constexpr bool bCreatingNewStaticMeshAsset = false;

	WriteDerivedTextures(bCreatingNewStaticMeshAsset);

	WriteDerivedMaterials(bCreatingNewStaticMeshAsset);

	UpdateSourceStaticMeshAsset(bSetNewHDSourceAsset);

	// clear list of derived textures we were holding onto to prevent GC
	AllDerivedTextures.Reset();
}



bool UGenerateStaticMeshLODProcess::IsSourceAsset(const FString& AssetPath) const
{
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		const FAssetData AssetData = UEditorAssetLibrary::FindAssetData(AssetPath);

		for (const FSourceMaterialInfo& MaterialInfo : SourceMaterials)
		{
			UMaterialInterface* MaterialInterface = MaterialInfo.SourceMaterial.MaterialInterface;
			if (MaterialInterface == nullptr)
			{
				continue;
			}

			FString SourceMaterialPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(MaterialInterface);
			if (UEditorAssetLibrary::FindAssetData(SourceMaterialPath) == AssetData)
			{
				return true;
			}

			for (const FTextureInfo& TextureInfo : MaterialInfo.SourceTextures)
			{
				FString SourceTexturePath = UEditorAssetLibrary::GetPathNameForLoadedAsset(TextureInfo.Texture);
				if (UEditorAssetLibrary::FindAssetData(SourceTexturePath) == AssetData)
				{
					return true;
				}
			}
		}
	}

	return false;
}



void UGenerateStaticMeshLODProcess::WriteDerivedTextures(bool bCreatingNewStaticMeshAsset)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// this is a workaround for handling multiple materials that reference the same texture. Currently the code
	// below will try to write that texture multiple times, which will fail when it tries to create a package
	// for a filename that already exists
	TMap<UTexture2D*, UTexture2D*> WrittenSourceToDerived;

	// write derived textures
	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FSourceMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		if (SourceMaterialInfo.bIsReusable || SourceMaterialInfo.bIsPreviouslyGeneratedMaterial)
		{
			continue;
		}

		FDerivedMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		int32 NumTextures = SourceMaterialInfo.SourceTextures.Num();
		check(DerivedMaterialInfo.DerivedTextures.Num() == NumTextures);
		for (int32 ti = 0; ti < NumTextures; ++ti)
		{
			const FTextureInfo& SourceTex = SourceMaterialInfo.SourceTextures[ti];
			FTextureInfo& DerivedTex = DerivedMaterialInfo.DerivedTextures[ti];

			if (WrittenSourceToDerived.Contains(SourceTex.Texture))
			{
				// Already computed the derived texture from this source
				DerivedTex.Texture = WrittenSourceToDerived[SourceTex.Texture];
				continue;
			}

			bool bConvertToSRGB = SourceTex.Texture->SRGB;

			if (DerivedTex.bShouldBakeTexture == false)
			{
				continue;
			}

			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, DerivedTex.Dimensions);
			TextureBuilder.GetTexture2D()->SRGB = bConvertToSRGB;
			TextureBuilder.Copy(DerivedTex.Image, bConvertToSRGB);
			TextureBuilder.Commit(false);

			DerivedTex.Texture = TextureBuilder.GetTexture2D();
			if (ensure(DerivedTex.Texture))
			{
				AllDerivedTextures.Add(DerivedTex.Texture);

				FTexture2DBuilder::CopyPlatformDataToSourceData(DerivedTex.Texture, FTexture2DBuilder::ETextureType::Color);

				// write asset
				bool bWriteOK = WriteDerivedTexture(SourceTex.Texture, DerivedTex.Texture, bCreatingNewStaticMeshAsset);
				ensure(bWriteOK);

				WrittenSourceToDerived.Add(SourceTex.Texture, DerivedTex.Texture);
			}
		}

	}


	// write derived normal map
	{
		FTexture2DBuilder NormapMapBuilder;
		NormapMapBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, DerivedNormalMapImage.Image.GetDimensions());
		NormapMapBuilder.Copy(DerivedNormalMapImage.Image, false);
		NormapMapBuilder.Commit(false);

		DerivedNormalMapTex = NormapMapBuilder.GetTexture2D();
		if (ensure(DerivedNormalMapTex))
		{
			FTexture2DBuilder::CopyPlatformDataToSourceData(DerivedNormalMapTex, FTexture2DBuilder::ETextureType::NormalMap);

			// write asset
			bool bWriteOK = WriteDerivedTexture(DerivedNormalMapTex, DerivedAssetNameNoSuffix + TEXT("_NormalMap"), bCreatingNewStaticMeshAsset);
			ensure(bWriteOK);
		}
	}

	
}



bool UGenerateStaticMeshLODProcess::WriteDerivedTexture(UTexture2D* SourceTexture, UTexture2D* DerivedTexture, bool bCreatingNewStaticMeshAsset)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString SourceTexPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(SourceTexture);
	FString TexName = FPaths::GetBaseFilename(SourceTexPath, true);
	return WriteDerivedTexture(DerivedTexture, TexName, bCreatingNewStaticMeshAsset);
}



bool UGenerateStaticMeshLODProcess::WriteDerivedTexture(UTexture2D* DerivedTexture, FString BaseTexName, bool bCreatingNewStaticMeshAsset)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString NewTexName = FString::Printf(TEXT("%s%s"), *BaseTexName, *DerivedSuffix);
	FString NewAssetPath = FPaths::Combine(DerivedAssetFolder, NewTexName);

	bool bNewAssetExistsInMemory = IsSourceAsset(NewAssetPath);

	if (bCreatingNewStaticMeshAsset || bNewAssetExistsInMemory)
	{
		// Don't delete an existing asset. If name collision occurs, rename the new asset.
		bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewAssetPath);
		while (bNewAssetExists)
		{
			GenerateStaticMeshLODProcessHelpers::AppendOrIncrementSuffix(NewTexName);
			NewAssetPath = FPaths::Combine(DerivedAssetFolder, NewTexName);
			bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewAssetPath);
		}
	}
	else
	{
		// Modifying the static mesh in place. Delete existing asset so that we can have a clean duplicate
		bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewAssetPath);
		if (bNewAssetExists)
		{
			bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewAssetPath);
			ensure(bDeleteOK);
		}
	}

	// create package
	FString UniquePackageName, UniqueAssetName;
	AssetTools.CreateUniqueAssetName(NewAssetPath, TEXT(""), UniquePackageName, UniqueAssetName);
	UPackage* AssetPackage = CreatePackage(*UniquePackageName);
	check(AssetPackage);

	// move texture from Transient package to new package
	DerivedTexture->Rename(*UniqueAssetName, AssetPackage, REN_None);
	// remove transient flag, add public/standalone/transactional
	DerivedTexture->ClearFlags(RF_Transient);
	DerivedTexture->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	// do we need to Modify() it? we are not doing any undo/redo
	DerivedTexture->Modify();
	DerivedTexture->UpdateResource();
	DerivedTexture->PostEditChange();		// this may be necessary if any Materials are using this texture
	DerivedTexture->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(DerivedTexture);		// necessary?

	return true;
}


void UGenerateStaticMeshLODProcess::WriteDerivedMaterials(bool bCreatingNewStaticMeshAsset)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FSourceMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		if (SourceMaterialInfo.bIsReusable || SourceMaterialInfo.bIsPreviouslyGeneratedMaterial)
		{
			continue;
		}

		FDerivedMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		UMaterialInterface* MaterialInterface = SourceMaterialInfo.SourceMaterial.MaterialInterface;
		if (MaterialInterface == nullptr)
		{
			DerivedMaterialInfo.DerivedMaterial.MaterialInterface = nullptr;
			continue;
		}
		bool bSourceIsMIC = (Cast<UMaterialInstanceConstant>(MaterialInterface) != nullptr);

		FString SourceMaterialPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(MaterialInterface);
		FString MaterialName = FPaths::GetBaseFilename(SourceMaterialPath, true);
		FString NewMaterialName = FString::Printf(TEXT("%s%s"), *MaterialName, *DerivedSuffix);
		FString NewMaterialPath = FPaths::Combine(DerivedAssetFolder, NewMaterialName);
		bool bNewAssetExistsInMemory = IsSourceAsset(NewMaterialPath);

		if (bCreatingNewStaticMeshAsset || bNewAssetExistsInMemory)
		{
			// Don't delete an existing material. If name collision occurs, rename the new material.
			bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewMaterialPath);
			while (bNewAssetExists)
			{
				GenerateStaticMeshLODProcessHelpers::AppendOrIncrementSuffix(NewMaterialName);
				NewMaterialPath = FPaths::Combine(DerivedAssetFolder, NewMaterialName);
				bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewMaterialPath);
			}
		}
		else
		{
			// Modifying the static mesh in place. Delete existing asset so that we can have a clean duplicate
			bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewMaterialPath);
			if (bNewAssetExists)
			{
				bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewMaterialPath);
				ensure(bDeleteOK);
			}
		}

		// If source is a MIC, we can just duplicate it. If it is a UMaterial, we want to
		// create a child MIC? Or we could dupe the Material and rewrite the textures.
		// Probably needs to be an option.
		UMaterialInstanceConstant* GeneratedMIC = nullptr;
		if (bSourceIsMIC)
		{
			UObject* DupeAsset = UEditorAssetLibrary::DuplicateAsset(SourceMaterialPath, NewMaterialPath);
			GeneratedMIC = Cast<UMaterialInstanceConstant>(DupeAsset);
		}
		else
		{
			UMaterial* SourceMaterial = MaterialInterface->GetBaseMaterial();
			if (ensure(SourceMaterial))
			{
				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = SourceMaterial;

				UObject* NewAsset = AssetTools.CreateAsset(NewMaterialName, FPackageName::GetLongPackagePath(NewMaterialPath),
					UMaterialInstanceConstant::StaticClass(), Factory);

				GeneratedMIC = Cast<UMaterialInstanceConstant>(NewAsset);
			}
		}

		// rewrite texture parameters to new textures
		UpdateMaterialTextureParameters(GeneratedMIC, DerivedMaterialInfo);

		// update StaticMaterial
		DerivedMaterialInfo.DerivedMaterial.MaterialInterface = GeneratedMIC;
		DerivedMaterialInfo.DerivedMaterial.MaterialSlotName = FName(FString::Printf(TEXT("GeneratedMat%d"), mi));
		DerivedMaterialInfo.DerivedMaterial.ImportedMaterialSlotName = DerivedMaterialInfo.DerivedMaterial.MaterialSlotName;
	}
}



void UGenerateStaticMeshLODProcess::UpdateMaterialTextureParameters(UMaterialInstanceConstant* Material, FDerivedMaterialInfo& DerivedMaterialInfo)
{
	Material->Modify();

	int32 NumTextures = DerivedMaterialInfo.DerivedTextures.Num();
	for (int32 ti = 0; ti < NumTextures; ++ti)
	{
		FTextureInfo& DerivedTex = DerivedMaterialInfo.DerivedTextures[ti];

		if (DerivedTex.bIsNormalMap)
		{
			if (ensure(DerivedNormalMapTex))
			{
				FMaterialParameterInfo ParamInfo(DerivedTex.ParameterName);
				Material->SetTextureParameterValueEditorOnly(ParamInfo, DerivedNormalMapTex);
			}
		}
		else if (DerivedTex.bShouldBakeTexture)
		{
			UTexture2D* NewTexture = DerivedTex.Texture;
			if (ensure(NewTexture))
			{
				FMaterialParameterInfo ParamInfo(DerivedTex.ParameterName);
				Material->SetTextureParameterValueEditorOnly(ParamInfo, NewTexture);
			}
		}
	}

	Material->PostEditChange();
}



void UGenerateStaticMeshLODProcess::WriteDerivedStaticMeshAsset()
{
	// [TODO] should we try to re-use existing asset here, or should we delete it? 
	// The source asset might have had any number of config changes that we want to
	// preserve in the duplicate...
	UStaticMesh* GeneratedStaticMesh = nullptr;
	if (UEditorAssetLibrary::DoesAssetExist(DerivedAssetPath))
	{
		UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(DerivedAssetPath);
		GeneratedStaticMesh = Cast<UStaticMesh>(LoadedAsset);
	}
	else
	{
		UObject* DupeAsset = UEditorAssetLibrary::DuplicateAsset(SourceAssetPath, DerivedAssetPath);
		GeneratedStaticMesh = Cast<UStaticMesh>(DupeAsset);
	}

	// make sure transactional flag is on
	GeneratedStaticMesh->SetFlags(RF_Transactional);
	GeneratedStaticMesh->Modify();

	// update MeshDescription LOD0 mesh
	GeneratedStaticMesh->SetNumSourceModels(1);
	FMeshDescription* MeshDescription = GeneratedStaticMesh->GetMeshDescription(0);
	FConversionToMeshDescriptionOptions ConversionOptions;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	Converter.Convert(&DerivedLODMesh, *MeshDescription);
	GeneratedStaticMesh->CommitMeshDescription(0);

	// construct new material slots list
	TArray<FStaticMaterial> NewMaterials;
	int32 NumMaterials = SourceMaterials.Num();
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		if (!SourceMaterials[mi].bIsPreviouslyGeneratedMaterial)	// Skip previously generated
		{
			if (SourceMaterials[mi].bIsReusable)
			{
				NewMaterials.Add(SourceMaterials[mi].SourceMaterial);
			}
			else
			{
				NewMaterials.Add(DerivedMaterials[mi].DerivedMaterial);
			}
		}
	}

	// update materials on generated mesh
	GeneratedStaticMesh->SetStaticMaterials(NewMaterials);

	// collision
	FPhysicsDataCollection NewCollisionGeo;
	NewCollisionGeo.Geometry = DerivedCollision;
	NewCollisionGeo.CopyGeometryToAggregate();

	// code below derived from FStaticMeshEditor::DuplicateSelectedPrims()
	UBodySetup* BodySetup = GeneratedStaticMesh->GetBodySetup();
	// mark the BodySetup for modification. Do we need to modify the UStaticMesh??
	BodySetup->Modify();
	//Clear the cache (PIE may have created some data), create new GUID    (comment from StaticMeshEditor)
	BodySetup->InvalidatePhysicsData();
	BodySetup->RemoveSimpleCollision();
	BodySetup->AggGeom = NewCollisionGeo.AggGeom;
	// update collision type
	BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
	// rebuild physics data
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	// do we need to do a post edit change here??

	// is this necessary? 
	GeneratedStaticMesh->CreateNavCollision(/*bIsUpdate=*/true);


	// done updating mesh
	GeneratedStaticMesh->PostEditChange();
}



void UGenerateStaticMeshLODProcess::UpdateSourceStaticMeshAsset(bool bSetNewHDSourceAsset)
{
	GEditor->BeginTransaction(LOCTEXT("UpdateExistingAssetMessage", "Added Generated LOD"));

	FStaticMeshSourceModel& SrcModel = SourceStaticMesh->GetSourceModel(0);
	SourceStaticMesh->ModifyMeshDescription(0);

	// if we want to save the input high-poly asset as the HiResSource, do that here
	if (bSetNewHDSourceAsset && bUsingHiResSource == false)
	{
		SourceStaticMesh->ModifyHiResMeshDescription();

		FMeshDescription* NewSourceMD = SourceStaticMesh->CreateHiResMeshDescription();
		*NewSourceMD = *SourceMeshDescription;		// todo: can MoveTemp here, we don't need this memory anymore??

		FStaticMeshSourceModel& HiResSrcModel = SourceStaticMesh->GetHiResSourceModel();
		// Generally copy LOD0 build settings, although many of these will be ignored
		HiResSrcModel.BuildSettings = SrcModel.BuildSettings;
		// on the HiRes we store the existing normals and tangents, which we already auto-computed if necessary
		HiResSrcModel.BuildSettings.bRecomputeNormals = false;
		HiResSrcModel.BuildSettings.bRecomputeTangents = false;
		// TODO: what should we do about Lightmap UVs?

		SourceStaticMesh->CommitHiResMeshDescription();
	}

	// Next bit is tricky, we have to build the final StaticMesh Material Set.
	// We have the existing Source materials we want to keep, except if some
	// were identified as being auto-generated by a previous run of AutoLOD, we want to leave those out.
	// Then we want to add any New generated materials. 
	// The main complication is that we cannot change the slot indices for the existing Source materials,
	// as we would have to fix up the HighRes source. Ideally they are the first N slots, and we
	// just append the new ones. But we cannot guarantee this, so if there are gaps we will interleave
	// the new materials when possible.


	// Figure out which derived materials we are actually going to use.
	// We will not use materials we can re-use from the source, or materials that were previously auto-generated.
	int32 NumMaterials = SourceMaterials.Num();
	TArray<int32> DerivedMatSlotIndexMap;		// this maps from current DerivedMesh slot indices to their final slot indices
	DerivedMatSlotIndexMap.SetNum(NumMaterials);
	TArray<int32> DerivedMaterialsToAdd;		// list of derived material indices we need to store in the final material set
	int32 NewMaterialCount = 0;
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		if (SourceMaterials[mi].bIsPreviouslyGeneratedMaterial)
		{
			DerivedMatSlotIndexMap[mi] = -2;		// these materials do not appear in DerivedMesh or SourceMesh and should be skipped/discarded (todo: and deleted?)
		}
		else  if (SourceMaterials[mi].bIsReusable)	// if we can re-use existing material we just rewrite to existing material slot index
		{
			DerivedMatSlotIndexMap[mi] = mi;
		}
		else
		{
			DerivedMatSlotIndexMap[mi] = -1;		// will need to allocate a new slot for this material
			DerivedMaterialsToAdd.Add(mi);
			NewMaterialCount++;
		}
	}
	int32 CurRemainingDerivedIdx = 0;

	// Copy existing materials we want to keep to new StaticMesh materials set. 
	// If there are any gaps left by skipping previously-derived materials, try to 
	// tuck in a new derived material that is waiting to be allocated to a slot
	TArray<FStaticMaterial> NewMaterialSet;
	TArray<int32> DerivedMaterialSlotIndices;
	for (int32 k = 0; k < SourceMaterials.Num(); ++k)
	{
		if (SourceMaterials[k].bIsPreviouslyGeneratedMaterial == false)
		{
			NewMaterialSet.Add(SourceMaterials[k].SourceMaterial);
		}
		else if (CurRemainingDerivedIdx < DerivedMaterialsToAdd.Num() )
		{
			int32 DerivedIdx = DerivedMaterialsToAdd[CurRemainingDerivedIdx];
			CurRemainingDerivedIdx++;
			DerivedMatSlotIndexMap[DerivedIdx] = NewMaterialSet.Num();
			DerivedMaterialSlotIndices.Add(DerivedMatSlotIndexMap[DerivedIdx]);
			NewMaterialSet.Add(DerivedMaterials[DerivedIdx].DerivedMaterial);
		}
		else
		{
			// we ran out of new materials to allocate and so just add empty ones??
			ensure(false);
			NewMaterialSet.Add(FStaticMaterial());
		}
	}

	// if we have any new derived materials left, append them to the material set
	while (CurRemainingDerivedIdx < DerivedMaterialsToAdd.Num())
	{
		int32 DerivedIdx = DerivedMaterialsToAdd[CurRemainingDerivedIdx];
		CurRemainingDerivedIdx++;
		DerivedMatSlotIndexMap[DerivedIdx] = NewMaterialSet.Num();
		DerivedMaterialSlotIndices.Add(DerivedMatSlotIndexMap[DerivedIdx]);
		NewMaterialSet.Add(DerivedMaterials[DerivedIdx].DerivedMaterial);
	}

	// apply the material slot index rewrite map to the DerivedMesh
	DerivedLODMesh.Attributes()->EnableMaterialID();
	FDynamicMeshMaterialAttribute* MaterialIDs = DerivedLODMesh.Attributes()->GetMaterialID();
	for (int32 tid : DerivedLODMesh.TriangleIndicesItr())
	{
		int32 CurMaterialID = MaterialIDs->GetValue(tid);
		int32 NewMaterialID = DerivedMatSlotIndexMap[CurMaterialID];
		if (ensure(NewMaterialID >= 0))
		{
			MaterialIDs->SetValue(tid, NewMaterialID);
		}
	}

	// update materials on generated mesh
	SourceStaticMesh->SetStaticMaterials(NewMaterialSet);

	// store new derived LOD as LOD 0
	SourceStaticMesh->SetNumSourceModels(1);
	FMeshDescription* MeshDescription = SourceStaticMesh->GetMeshDescription(0);
	if (MeshDescription == nullptr)
	{
		MeshDescription = SourceStaticMesh->CreateMeshDescription(0);
	}
	FConversionToMeshDescriptionOptions ConversionOptions;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	Converter.Convert(&DerivedLODMesh, *MeshDescription);

	// calculate tangents
	Converter.UpdateTangents(&DerivedLODMesh, *MeshDescription, &DerivedLODMeshTangents);

	// set slot names on MeshDescription to match those we set on the generated FStaticMaterials, 
	// because StaticMesh RenderBuffer setup will do matching-name lookups and if it is NAME_None
	// we will get the wrong material!
	FStaticMeshAttributes Attributes(*MeshDescription);
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	for (int32 SlotIdx : DerivedMaterialSlotIndices)
	{
		// It's possible that NewMaterialSet.Num() > PolygonGroupImportedMaterialSlotNames.GetNumElements()
		// if there are new materials that aren't referenced by any triangles...
		if (SlotIdx < PolygonGroupImportedMaterialSlotNames.GetNumElements())		
		{
			PolygonGroupImportedMaterialSlotNames.Set(SlotIdx, NewMaterialSet[SlotIdx].ImportedMaterialSlotName);
		}
	}

	// Disable auto-generated normals/tangents, we need to use the ones we computed in LOD Generator
	SrcModel.BuildSettings.bRecomputeNormals = false;
	SrcModel.BuildSettings.bRecomputeTangents = false;

	// this will prevent simplification?
	SrcModel.ReductionSettings.MaxDeviation = 0.0f;
	SrcModel.ReductionSettings.PercentTriangles = 1.0f;
	SrcModel.ReductionSettings.PercentVertices = 1.0f;

	// commit update
	SourceStaticMesh->CommitMeshDescription(0);

	// collision
	FPhysicsDataCollection NewCollisionGeo;
	NewCollisionGeo.Geometry = DerivedCollision;
	NewCollisionGeo.CopyGeometryToAggregate();

	// code below derived from FStaticMeshEditor::DuplicateSelectedPrims()
	UBodySetup* BodySetup = SourceStaticMesh->GetBodySetup();
	// mark the BodySetup for modification. Do we need to modify the UStaticMesh??
	BodySetup->Modify();
	//Clear the cache (PIE may have created some data), create new GUID    (comment from StaticMeshEditor)
	BodySetup->InvalidatePhysicsData();
	BodySetup->RemoveSimpleCollision();
	BodySetup->AggGeom = NewCollisionGeo.AggGeom;
	// update collision type
	BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
	// rebuild physics data
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	// do we need to do a post edit change here??

	// is this necessary? 
	SourceStaticMesh->CreateNavCollision(/*bIsUpdate=*/true);

	GEditor->EndTransaction();

	// done updating mesh
	SourceStaticMesh->PostEditChange();
}



#undef LOCTEXT_NAMESPACE