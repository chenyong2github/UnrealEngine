// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graphs/GenerateStaticMeshLODProcess.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "DynamicMeshAttributeSet.h"

#include "AssetUtils/Texture2DUtil.h"
#include "AssetUtils/Texture2DBuilder.h"

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

#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"

#define LOCTEXT_NAMESPACE "FGenerateStaticMeshLODProcess"


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


bool FGenerateStaticMeshLODProcess::Initialize(UStaticMesh* StaticMeshIn)
{
	if (!ensure(StaticMeshIn)) return false;
	if (!ensure(StaticMeshIn->GetNumSourceModels() > 0)) return false;

	// make sure we are not in rendering
	FlushRenderingCommands();

	SourceStaticMesh = StaticMeshIn;
	const FMeshDescription* SourceMeshDescription = StaticMeshIn->GetMeshDescription(0);

	// start async mesh-conversion
	SourceMesh.Clear();
	TFuture<void> ConvertMesh = Async(GenerateSMLODAsyncExecTarget, [&]()
	{
		FMeshDescriptionToDynamicMesh GetSourceMesh;
		GetSourceMesh.Convert(SourceMeshDescription, SourceMesh);
	});

	// get list of source materials and find all the input texture params
	const TArray<FStaticMaterial>& Materials = SourceStaticMesh->GetStaticMaterials();
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
					TexInfo.SourceTexture = Cast<UTexture2D>(CurTexture);
					TexInfo.ParameterName = ParamName;

					TexInfo.bIsNormalMap = TexInfo.SourceTexture->IsNormalMap();
					//TexInfo.bIsDefaultTexture = TexInfo.SourceTexture->IsDefaultTexture();
					TexInfo.bIsDefaultTexture = UEditorAssetLibrary::GetPathNameForLoadedAsset(TexInfo.SourceTexture).StartsWith(TEXT("/Engine/"));

					UE_LOG(LogTemp, Warning, TEXT("SOURCE TEX PATH IS %s - DefaultTex %d - SRGB %d"), *UEditorAssetLibrary::GetPathNameForLoadedAsset(TexInfo.SourceTexture),
						TexInfo.bIsDefaultTexture?1:0, TexInfo.SourceTexture->SRGB);

					TexInfo.bShouldBakeTexture = (TexInfo.bIsNormalMap == false && TexInfo.bIsDefaultTexture == false);

					if (TexInfo.bShouldBakeTexture)
					{
						ReadJobs.Add(FReadTextureJob{ mi, SourceMaterials[mi].SourceTextures.Num() });
					}

					SourceMaterials[mi].SourceTextures.Add(TexInfo);
				}
			}
		}
	}


	// extract all the texture params
	TFuture<void> ReadTextures = Async(GenerateSMLODAsyncExecTarget, [&]()
	{
		ParallelFor(ReadJobs.Num(), [&](int32 ji)
		{
			FReadTextureJob job = ReadJobs[ji];
			FTextureInfo& TexInfo = SourceMaterials[job.MatIndex].SourceTextures[job.TexIndex];
			UE::AssetUtils::ReadTexture(TexInfo.SourceTexture, TexInfo.Dimensions, TexInfo.Image);
		});
	});
	// single-thread path
	//for (FReadTextureJob job : ReadJobs)
	//{
	//	FTextureInfo& TexInfo = SourceMaterials[job.MatIndex].SourceTextures[job.TexIndex];
	//	UE::AssetUtils::ReadTexture(TexInfo.SourceTexture, TexInfo.Dimensions, TexInfo.Image);
	//}


	ConvertMesh.Wait();
	ReadTextures.Wait();


	FString FullPathWithExtension = UEditorAssetLibrary::GetPathNameForLoadedAsset(SourceStaticMesh);
	SourceAssetPath = FPaths::GetBaseFilename(FullPathWithExtension, false);
	SourceAssetFolder = FPaths::GetPath(SourceAssetPath);
	SourceAssetName = FPaths::GetBaseFilename(FullPathWithExtension, true);

	CalculateDerivedPathName(GetDefaultDerivedAssetSuffix());

	InitializeGenerator();

	return true;
}




void FGenerateStaticMeshLODProcess::CalculateDerivedPathName(FString NewAssetSuffix)
{
	DerivedSuffix = FPaths::MakeValidFileName(NewAssetSuffix);
	if (DerivedSuffix.Len() == 0)
	{
		DerivedSuffix = GetDefaultDerivedAssetSuffix();
	}
	DerivedAssetPath = FString::Printf(TEXT("%s%s"), *GetSourceAssetPath(), *DerivedSuffix);
	DerivedAssetFolder = FPaths::GetPath(DerivedAssetPath);
}


bool FGenerateStaticMeshLODProcess::InitializeGenerator()
{
	Generator = MakeUnique<FGenerateMeshLODGraph>();
	Generator->BuildGraph();

	// initialize source textures
	TextureToDerivedTexIndex.Reset();
	for (const FMaterialInfo& MatInfo : SourceMaterials)
	{
		for (const FTextureInfo& TexInfo : MatInfo.SourceTextures)
		{
			if (TexInfo.bShouldBakeTexture &&
				(TextureToDerivedTexIndex.Contains(TexInfo.SourceTexture) == false))
			{
				int32 NewIndex = Generator->AppendTextureBakeNode(TexInfo.Image, TexInfo.SourceTexture->GetName());
				TextureToDerivedTexIndex.Add(TexInfo.SourceTexture, NewIndex);
			}
		}
	}
	

	// initialize source mesh
	Generator->SetSourceMesh(this->SourceMesh);


	// read back default settings

	CurrentSettings.SolidifyVoxelResolution = Generator->GetCurrentSolidifySettings().VoxelResolution;
	CurrentSettings.WindingThreshold = Generator->GetCurrentSolidifySettings().WindingThreshold;

	//CurrentSettings.MorphologyVoxelResolution = Generator->GetCurrentMorphologySettings().VoxelResolution;
	CurrentSettings.ClosureDistance = Generator->GetCurrentMorphologySettings().Distance;

	CurrentSettings.SimplifyTriangleCount = Generator->GetCurrentSimplifySettings().TargetCount;

	CurrentSettings.BakeResolution = (EGenerateStaticMeshLODBakeResolution)Generator->GetCurrentBakeCacheSettings().Dimensions.GetWidth();
	CurrentSettings.BakeThickness = Generator->GetCurrentBakeCacheSettings().Thickness;

	CurrentSettings.ConvexTriangleCount = Generator->GetCurrentGenerateConvexCollisionSettings().SimplifyToTriangleCount;


	return true;
}


void FGenerateStaticMeshLODProcess::UpdateSettings(const FGenerateStaticMeshLODProcessSettings& NewSettings)
{
	bool bSharedVoxelResolutionChanged = (NewSettings.SolidifyVoxelResolution != CurrentSettings.SolidifyVoxelResolution);
	if ( bSharedVoxelResolutionChanged
		|| (NewSettings.WindingThreshold != CurrentSettings.WindingThreshold))
	{
		UE::GeometryFlow::FMeshSolidifySettings NewSolidifySettings = Generator->GetCurrentSolidifySettings();
		NewSolidifySettings.VoxelResolution = NewSettings.SolidifyVoxelResolution;
		NewSolidifySettings.WindingThreshold = NewSettings.WindingThreshold;
		Generator->UpdateSolidifySettings(NewSolidifySettings);
	}


	if ( bSharedVoxelResolutionChanged
		|| (NewSettings.ClosureDistance != CurrentSettings.ClosureDistance))
	{
		UE::GeometryFlow::FVoxClosureSettings NewClosureSettings = Generator->GetCurrentMorphologySettings();
		NewClosureSettings.VoxelResolution = NewSettings.SolidifyVoxelResolution;
		NewClosureSettings.Distance = NewSettings.ClosureDistance;
		Generator->UpdateMorphologySettings(NewClosureSettings);
	}


	if (NewSettings.SimplifyTriangleCount != CurrentSettings.SimplifyTriangleCount)
	{
		UE::GeometryFlow::FMeshSimplifySettings NewSimplifySettings = Generator->GetCurrentSimplifySettings();
		NewSimplifySettings.TargetCount = NewSettings.SimplifyTriangleCount;
		Generator->UpdateSimplifySettings(NewSimplifySettings);
	}

	if (NewSettings.NumAutoUVCharts != CurrentSettings.NumAutoUVCharts)
	{
		UE::GeometryFlow::FMeshAutoGenerateUVsSettings NewAutoUVSettings = Generator->GetCurrentAutoUVSettings();
		NewAutoUVSettings.NumCharts = NewSettings.NumAutoUVCharts;
		Generator->UpdateAutoUVSettings(NewAutoUVSettings);
	}


	if ( (NewSettings.BakeResolution != CurrentSettings.BakeResolution) ||
		 (NewSettings.BakeThickness != CurrentSettings.BakeThickness))
	{
		UE::GeometryFlow::FMeshMakeBakingCacheSettings NewBakeSettings = Generator->GetCurrentBakeCacheSettings();
		NewBakeSettings.Dimensions = FImageDimensions((int32)NewSettings.BakeResolution, (int32)NewSettings.BakeResolution);
		NewBakeSettings.Thickness = NewSettings.BakeThickness;
		Generator->UpdateBakeCacheSettings(NewBakeSettings);
	}


	if (NewSettings.ConvexTriangleCount != CurrentSettings.ConvexTriangleCount)
	{
		UE::GeometryFlow::FGenerateConvexHullsCollisionSettings NewGenConvexSettings = Generator->GetCurrentGenerateConvexCollisionSettings();
		NewGenConvexSettings.SimplifyToTriangleCount = NewSettings.ConvexTriangleCount;
		Generator->UpdateGenerateConvexCollisionSettings(NewGenConvexSettings);
	}

	CurrentSettings = NewSettings;
}




bool FGenerateStaticMeshLODProcess::ComputeDerivedSourceData()
{
	DerivedTextureImages.Reset();
	if (bUseParallelExecutor)
	{
		Generator->EvaluateResultParallel(
			this->DerivedLODMesh,
			this->DerivedLODMeshTangents,
			this->DerivedCollision,
			this->DerivedNormalMapImage,
			this->DerivedTextureImages);
	}
	else
	{
		Generator->EvaluateResult(
			this->DerivedLODMesh,
			this->DerivedLODMeshTangents,
			this->DerivedCollision,
			this->DerivedNormalMapImage,
			this->DerivedTextureImages);
	}


	// copy all materials for now...we are going to replace all the images though, and
	// should not copy those?
	DerivedMaterials = SourceMaterials;

	// update texture data in derived materials
	for (FMaterialInfo& MatInfo : DerivedMaterials)
	{
		for (FTextureInfo& TexInfo : MatInfo.SourceTextures)
		{
			if (TextureToDerivedTexIndex.Contains(TexInfo.SourceTexture))
			{
				int32 BakedTexIndex = TextureToDerivedTexIndex[TexInfo.SourceTexture];
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




void FGenerateStaticMeshLODProcess::GetDerivedMaterialsPreview(FPreviewMaterials& MaterialSetOut)
{
	// force garbage collection of outstanding preview materials
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);


	// create derived textures
	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	TMap<UTexture2D*, UTexture2D*> SourceToPreviewTexMap;
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		const FMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		int32 NumTextures = SourceMaterialInfo.SourceTextures.Num();
		check(DerivedMaterialInfo.SourceTextures.Num() == NumTextures);
		for (int32 ti = 0; ti < NumTextures; ++ti)
		{
			const FTextureInfo& SourceTex = SourceMaterialInfo.SourceTextures[ti];
			bool bConvertToSRGB = SourceTex.SourceTexture->SRGB;
			const FTextureInfo& DerivedTex = DerivedMaterialInfo.SourceTextures[ti];
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
					SourceToPreviewTexMap.Add(SourceTex.SourceTexture, PreviewTex);
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
		const FMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];

		UMaterialInterface* MaterialInterface = SourceMaterialInfo.SourceMaterial.MaterialInterface;
		UMaterialInstanceDynamic* GeneratedMID = UMaterialInstanceDynamic::Create(MaterialInterface, NULL);

		// rewrite texture parameters to new textures
		UpdateMaterialTextureParameters(GeneratedMID, SourceMaterialInfo, SourceToPreviewTexMap, PreviewNormalMapTex);

		MaterialSetOut.Materials.Add(GeneratedMID);
	}

}


void FGenerateStaticMeshLODProcess::UpdateMaterialTextureParameters(
	UMaterialInstanceDynamic* Material, 
	const FMaterialInfo& SourceMaterialInfo,
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
			UTexture2D*const* FoundTexture = PreviewTextures.Find(SourceTex.SourceTexture);
			if (ensure(FoundTexture))
			{
				FMaterialParameterInfo ParamInfo(SourceTex.ParameterName);
				Material->SetTextureParameterValueByInfo(ParamInfo, *FoundTexture);
			}
		}
	}
	Material->PostEditChange();
}





bool FGenerateStaticMeshLODProcess::WriteDerivedAssetData()
{
	WriteDerivedTextures();

	WriteDerivedMaterials();

	WriteDerivedStaticMeshAsset();

	return true;
}


void FGenerateStaticMeshLODProcess::UpdateSourceAsset()
{
	WriteDerivedTextures();

	WriteDerivedMaterials();

	UpdateSourceStaticMeshAsset();
}


void FGenerateStaticMeshLODProcess::WriteDerivedTextures()
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// this is a workaround for handling multiple materials that reference the same texture. Currently the code
	// below will try to write that texture multiple times, which will fail when it tries to create a package
	// for a filename that already exists
	TArray<UTexture2D*> SourceTexturesWritten;

	// write derived textures
	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		FMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		int32 NumTextures = SourceMaterialInfo.SourceTextures.Num();
		check(DerivedMaterialInfo.SourceTextures.Num() == NumTextures);
		for (int32 ti = 0; ti < NumTextures; ++ti)
		{
			const FTextureInfo& SourceTex = SourceMaterialInfo.SourceTextures[ti];
			if (SourceTexturesWritten.Contains(SourceTex.SourceTexture))
			{
				continue;
			}

			bool bConvertToSRGB = SourceTex.SourceTexture->SRGB;
			FTextureInfo& DerivedTex = DerivedMaterialInfo.SourceTextures[ti];

			if (DerivedTex.bShouldBakeTexture == false)
			{
				continue;
			}

			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, DerivedTex.Dimensions);
			TextureBuilder.GetTexture2D()->SRGB = bConvertToSRGB;
			TextureBuilder.Copy(DerivedTex.Image, bConvertToSRGB);
			TextureBuilder.Commit(false);

			DerivedTex.SourceTexture = TextureBuilder.GetTexture2D();
			if (ensure(DerivedTex.SourceTexture))
			{
				FTexture2DBuilder::CopyPlatformDataToSourceData(DerivedTex.SourceTexture, FTexture2DBuilder::ETextureType::Color);

				// write asset
				bool bWriteOK = WriteDerivedTexture(SourceTex.SourceTexture, DerivedTex.SourceTexture);
				ensure(bWriteOK);

				SourceTexturesWritten.Add(SourceTex.SourceTexture);
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
			bool bWriteOK = WriteDerivedTexture(DerivedNormalMapTex, SourceAssetName + TEXT("_NormalMap"));
			ensure(bWriteOK);
		}
	}

	
}



bool FGenerateStaticMeshLODProcess::WriteDerivedTexture(UTexture2D* SourceTexture, UTexture2D* DerivedTexture)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString SourceTexPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(SourceTexture);
	FString TexName = FPaths::GetBaseFilename(SourceTexPath, true);
	return WriteDerivedTexture(DerivedTexture, TexName);
}


bool FGenerateStaticMeshLODProcess::WriteDerivedTexture(UTexture2D* DerivedTexture, FString BaseTexName)
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString NewTexName = FString::Printf(TEXT("%s%s"), *BaseTexName, *DerivedSuffix);
	FString NewAssetPath = FPaths::Combine(DerivedAssetFolder, NewTexName);

	// delete existing asset so that we can have a clean duplicate
	bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewAssetPath);
	if (bNewAssetExists)
	{
		bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewAssetPath);
		ensure(bDeleteOK);
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



void FGenerateStaticMeshLODProcess::WriteDerivedMaterials()
{
	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	int32 NumMaterials = SourceMaterials.Num();
	check(DerivedMaterials.Num() == NumMaterials);
	for (int32 mi = 0; mi < NumMaterials; ++mi)
	{
		const FMaterialInfo& SourceMaterialInfo = SourceMaterials[mi];
		FMaterialInfo& DerivedMaterialInfo = DerivedMaterials[mi];

		UMaterialInterface* MaterialInterface = SourceMaterialInfo.SourceMaterial.MaterialInterface;
		bool bSourceIsMIC = (Cast<UMaterialInstanceConstant>(MaterialInterface) != nullptr);

		FString SourceMaterialPath = UEditorAssetLibrary::GetPathNameForLoadedAsset(MaterialInterface);
		FString MaterialName = FPaths::GetBaseFilename(SourceMaterialPath, true);
		FString NewMaterialName = FString::Printf(TEXT("%s%s"), *MaterialName, *DerivedSuffix);
		FString NewMaterialPath = FPaths::Combine(DerivedAssetFolder, NewMaterialName);

		// delete existing material so that we can have a clean duplicate
		bool bNewMaterialExists = UEditorAssetLibrary::DoesAssetExist(NewMaterialPath);
		if (bNewMaterialExists)
		{
			bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewMaterialPath);
			ensure(bDeleteOK);
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
		DerivedMaterialInfo.SourceMaterial.MaterialInterface = GeneratedMIC;
	}
}



void FGenerateStaticMeshLODProcess::UpdateMaterialTextureParameters(UMaterialInstanceConstant* Material, FMaterialInfo& DerivedMaterialInfo)
{
	Material->Modify();

	int32 NumTextures = DerivedMaterialInfo.SourceTextures.Num();
	for (int32 ti = 0; ti < NumTextures; ++ti)
	{
		FTextureInfo& DerivedTex = DerivedMaterialInfo.SourceTextures[ti];

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
			UTexture2D* NewTexture = DerivedTex.SourceTexture;
			if (ensure(NewTexture))
			{
				FMaterialParameterInfo ParamInfo(DerivedTex.ParameterName);
				Material->SetTextureParameterValueEditorOnly(ParamInfo, NewTexture);
			}
		}
	}

	Material->PostEditChange();
}



void FGenerateStaticMeshLODProcess::WriteDerivedStaticMeshAsset()
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

	TArray<FStaticMaterial> NewMaterials;
	for (FMaterialInfo& DerivedMaterialInfo : DerivedMaterials)
	{
		NewMaterials.Add(DerivedMaterialInfo.SourceMaterial);
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




void FGenerateStaticMeshLODProcess::UpdateSourceStaticMeshAsset()
{
	GEditor->BeginTransaction(LOCTEXT("UpdateExistingAssetMessage", "Added Generated LOD"));

	SourceStaticMesh->Modify();

	TArray<FStaticMaterial> ExistingMaterials = SourceStaticMesh->GetStaticMaterials();

	// append new materials to material set
	TArray<int32> NewMatIndexMap;
	for (FMaterialInfo& DerivedMaterialInfo : DerivedMaterials)
	{
		int32 NewIndex = ExistingMaterials.Num();
		ExistingMaterials.Add(DerivedMaterialInfo.SourceMaterial);
		NewMatIndexMap.Add(NewIndex);
	}

	// rewrite material IDs on derived mesh
	DerivedLODMesh.Attributes()->EnableMaterialID();
	FDynamicMeshMaterialAttribute* MaterialIDs = DerivedLODMesh.Attributes()->GetMaterialID();
	for (int32 tid : DerivedLODMesh.TriangleIndicesItr())
	{
		int32 CurMaterialID = MaterialIDs->GetValue(tid);
		int32 NewMaterialID = NewMatIndexMap[CurMaterialID];
		MaterialIDs->SetValue(tid, NewMaterialID);
	}

	// update materials on generated mesh
	SourceStaticMesh->SetStaticMaterials(ExistingMaterials);

	// store new derived LOD as LOD 1
	SourceStaticMesh->SetNumSourceModels(2);
	FMeshDescription* MeshDescription = SourceStaticMesh->GetMeshDescription(1);
	if (MeshDescription == nullptr)
	{
		MeshDescription = SourceStaticMesh->CreateMeshDescription(1);
	}
	FConversionToMeshDescriptionOptions ConversionOptions;
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	Converter.Convert(&DerivedLODMesh, *MeshDescription);
	SourceStaticMesh->CommitMeshDescription(1);

	// this will prevent simplification?
	FStaticMeshSourceModel& SrcModel = SourceStaticMesh->GetSourceModel(1);
	SrcModel.ReductionSettings.MaxDeviation = 0.0f;
	SrcModel.ReductionSettings.PercentTriangles = 1.0f;
	SrcModel.ReductionSettings.PercentVertices = 1.0f;

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