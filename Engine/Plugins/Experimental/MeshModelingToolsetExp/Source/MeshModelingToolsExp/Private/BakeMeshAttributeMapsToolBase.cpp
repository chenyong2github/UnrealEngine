// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeMapsToolBase.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"

#include "Sampling/MeshOcclusionMapEvaluator.h"
#include "Sampling/MeshPropertyMapEvaluator.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/Texture2DUtil.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "ModelingToolTargetUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeMeshAttributeMapsToolBase"


void UBakeMeshAttributeMapsToolBase::Setup()
{
	Super::Setup();

	InitializeEmptyMaps();

	// Setup preview materials
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/BakePreviewMaterial"));
	check(Material);
	if (Material != nullptr)
	{
		PreviewMaterial = UMaterialInstanceDynamic::Create(Material, GetToolManager());
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
	}
	UMaterial* BentNormalMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/BakeBentNormalPreviewMaterial"));
	check(BentNormalMaterial);
	if (BentNormalMaterial != nullptr)
	{
		BentNormalPreviewMaterial = UMaterialInstanceDynamic::Create(BentNormalMaterial, GetToolManager());
	}
	UMaterial* WorkingMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/InProgressMaterial"));
	check(WorkingMaterial);
	if (WorkingMaterial != nullptr)
	{
		WorkingPreviewMaterial = UMaterialInstanceDynamic::Create(WorkingMaterial, GetToolManager());
	}

	// Initialize preview mesh
	UE::ToolTarget::HideSourceObject(Targets[0]);

	const FDynamicMesh3 InputMesh = UE::ToolTarget::GetDynamicMeshCopy(Targets[0], true);
	const UE::Geometry::FTransform3d BaseToWorld = UE::ToolTarget::GetLocalToWorldTransform(Targets[0]);
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(PreviewMesh, nullptr);
	PreviewMesh->SetTransform(static_cast<FTransform>(BaseToWorld));
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::ExternallyProvided);
	PreviewMesh->ReplaceMesh(InputMesh);
	PreviewMesh->SetMaterials(UE::ToolTarget::GetMaterialSet(Targets[0]).Materials);
	PreviewMesh->SetOverrideRenderMaterial(PreviewMaterial);
	PreviewMesh->SetVisible(true);
}

void UBakeMeshAttributeMapsToolBase::SetupBaseToolProperties()
{
	VisualizationProps = NewObject<UBakedOcclusionMapVisualizationProperties>(this);
	VisualizationProps->RestoreProperties(this);
	AddToolPropertySource(VisualizationProps);
}

void UBakeMeshAttributeMapsToolBase::Shutdown(EToolShutdownType ShutdownType)
{
	VisualizationProps->SaveProperties(this);

	if (PreviewMesh != nullptr)
	{
		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

	UE::ToolTarget::ShowSourceObject(Targets[0]);
}


void UBakeMeshAttributeMapsToolBase::OnTick(float DeltaTime)
{
	if (Compute)
	{
		Compute->Tick(DeltaTime);

		float ElapsedComputeTime = Compute->GetElapsedComputeTime();
		if (!CanAccept() && ElapsedComputeTime > SecondsBeforeWorkingMaterial)
		{
			PreviewMesh->SetOverrideRenderMaterial(WorkingPreviewMaterial);
		}
	}
}


void UBakeMeshAttributeMapsToolBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();
	
	float GrayLevel = VisualizationProps->BaseGrayLevel;
	PreviewMaterial->SetVectorParameterValue(TEXT("BaseColor"), FVector(GrayLevel, GrayLevel, GrayLevel) );
	float AOWeight = VisualizationProps->OcclusionMultiplier;
	PreviewMaterial->SetScalarParameterValue(TEXT("AOWeight"), AOWeight );
}


TUniquePtr<UE::Geometry::TGenericDataOperator<FMeshMapBaker>> UBakeMeshAttributeMapsToolBase::MakeNewOperator()
{
	return nullptr;
}


void UBakeMeshAttributeMapsToolBase::SetWorld(UWorld* World)
{
	TargetWorld = World;
}

void UBakeMeshAttributeMapsToolBase::UpdateResult()
{
}

void UBakeMeshAttributeMapsToolBase::UpdateVisualization()
{
}

void UBakeMeshAttributeMapsToolBase::OnMapTypesUpdated(const int32 MapTypes)
{
	const EBakeMapType BakeMapTypes = GetMapTypes(MapTypes);
	ResultTypes = GetMapTypesArray(MapTypes);
	
	// Generate a map between EBakeMapType and CachedMaps
	CachedMapIndices.Empty();
	int32 CachedMapIdx = 0;

	// Use the processed bitfield which may contain additional targets
	// (ex. AO if BentNormal was requested).
	for (EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
	{
		if (MapType == EBakeMapType::Occlusion)
		{
			if ((bool)(BakeMapTypes & EBakeMapType::AmbientOcclusion))
			{
				CachedMapIndices.Add(EBakeMapType::AmbientOcclusion, CachedMapIdx++);
			}
			if ((bool)(BakeMapTypes & EBakeMapType::BentNormal))
			{
				CachedMapIndices.Add(EBakeMapType::BentNormal, CachedMapIdx++);
			}
		}
		else if( (bool)(BakeMapTypes & MapType) )
		{
			CachedMapIndices.Add(MapType, CachedMapIdx++);
		}
	}
	CachedMaps.Empty();
	CachedMaps.SetNum(CachedMapIndices.Num());
}

void UBakeMeshAttributeMapsToolBase::UpdatePreview(const int PreviewIdx)
{
	const EBakeMapType& PreviewMapType = ResultTypes[PreviewIdx];
	if (PreviewMapType == EBakeMapType::None)
	{
		return;
	}
	
	UTexture2D* PreviewMap = CachedMaps[CachedMapIndices[PreviewMapType]];
	switch (PreviewMapType)
	{
	default:
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		break;
	case EBakeMapType::TangentSpaceNormalMap:
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), PreviewMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		break;
	case EBakeMapType::AmbientOcclusion:
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), PreviewMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		break;
	case EBakeMapType::BentNormal:
	{
		UTexture2D* AOMap = CachedMapIndices.Contains(EBakeMapType::AmbientOcclusion) ? CachedMaps[CachedMapIndices[EBakeMapType::AmbientOcclusion]] : EmptyColorMapWhite;
		BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), AOMap);
		BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("BentNormalMap"), PreviewMap);
		PreviewMesh->SetOverrideRenderMaterial(BentNormalPreviewMaterial);
		break;
	}	
	case EBakeMapType::Curvature:
	case EBakeMapType::NormalImage:
	case EBakeMapType::FaceNormalImage:
	case EBakeMapType::PositionImage:
	case EBakeMapType::MaterialID:
	case EBakeMapType::Texture2DImage:
	case EBakeMapType::MultiTexture:
	case EBakeMapType::VertexColorImage:
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), PreviewMap);
		break;
	}
}


void UBakeMeshAttributeMapsToolBase::OnMapsUpdated(const TUniquePtr<UE::Geometry::FMeshMapBaker>& NewResult)
{
	FImageDimensions BakeDimensions = NewResult->GetDimensions();
	const int32 NumEval = NewResult->NumEvaluators();
	for (int32 EvalIdx = 0; EvalIdx < NumEval; ++EvalIdx)
	{
		FMeshMapEvaluator* Eval = NewResult->GetEvaluator(EvalIdx);

		auto UpdateCachedMap = [this, &NewResult, &EvalIdx, &BakeDimensions](const EBakeMapType BakeMapType, const FTexture2DBuilder::ETextureType TexType, const int32 ResultIdx) -> void
		{
			// For 8-bit color textures, ensure that the source data is in sRGB.
			const bool bConvertToSRGB = TexType == FTexture2DBuilder::ETextureType::Color;
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(TexType, BakeDimensions);
			TextureBuilder.Copy(*NewResult->GetBakeResults(EvalIdx)[ResultIdx], bConvertToSRGB);
			TextureBuilder.Commit(false);

			// The CachedMap & CachedMapIndices can be thrown out of sync if updated during
			// a background compute. Validate the computed type against our cached maps.
			if (CachedMapIndices.Contains(BakeMapType))
			{
				CachedMaps[CachedMapIndices[BakeMapType]] = TextureBuilder.GetTexture2D();
			}
		};

		switch (Eval->Type())
		{
		case EMeshMapEvaluatorType::Normal:
		{
			constexpr EBakeMapType MapType = EBakeMapType::TangentSpaceNormalMap;
			UpdateCachedMap(MapType, GetTextureType(MapType), 0);
			break;
		}
		case EMeshMapEvaluatorType::Occlusion:
		{
			// Occlusion Evaluator always outputs AmbientOcclusion then BentNormal.
			const FMeshOcclusionMapEvaluator* OcclusionEval = static_cast<FMeshOcclusionMapEvaluator*>(Eval);
			int32 OcclusionIdx = 0;
			if ((bool)(OcclusionEval->OcclusionType & EMeshOcclusionMapType::AmbientOcclusion))
			{
				constexpr EBakeMapType MapType = EBakeMapType::AmbientOcclusion;
				UpdateCachedMap(MapType, GetTextureType(MapType), OcclusionIdx++);
			}
			if ((bool)(OcclusionEval->OcclusionType & EMeshOcclusionMapType::BentNormal))
			{
				constexpr EBakeMapType MapType = EBakeMapType::BentNormal;
				UpdateCachedMap(MapType, GetTextureType(MapType), OcclusionIdx++);
			}
			break;
		}
		case EMeshMapEvaluatorType::Curvature:
		{
			constexpr EBakeMapType MapType = EBakeMapType::Curvature;
			UpdateCachedMap(MapType, GetTextureType(MapType), 0);
			break;
		}
		case EMeshMapEvaluatorType::Property:
		{
			const FMeshPropertyMapEvaluator* PropertyEval = static_cast<FMeshPropertyMapEvaluator*>(Eval);
			EBakeMapType MapType = EBakeMapType::None;
			switch (PropertyEval->Property)
			{
			case EMeshPropertyMapType::Normal:
				MapType = EBakeMapType::NormalImage;
				break;
			case EMeshPropertyMapType::FacetNormal:
				MapType = EBakeMapType::FaceNormalImage;
				break;
			case EMeshPropertyMapType::Position:
				MapType = EBakeMapType::PositionImage;
				break;
			case EMeshPropertyMapType::MaterialID:
				MapType = EBakeMapType::MaterialID;
				break;
			case EMeshPropertyMapType::VertexColor:
				MapType = EBakeMapType::VertexColorImage;
				break;
			case EMeshPropertyMapType::UVPosition:
			default:
				break;
			}

			UpdateCachedMap(MapType, GetTextureType(MapType), 0);
			break;
		}
		case EMeshMapEvaluatorType::ResampleImage:
		{
			constexpr EBakeMapType MapType = EBakeMapType::Texture2DImage;
			UpdateCachedMap(EBakeMapType::Texture2DImage, GetTextureType(MapType), 0);
			break;
		}
		case EMeshMapEvaluatorType::MultiResampleImage:
		{
			constexpr EBakeMapType MapType = EBakeMapType::MultiTexture;
			UpdateCachedMap(MapType, GetTextureType(MapType), 0);
			break;
		}
		default:
			break;
		}
	}

	UpdateVisualization();
	GetToolManager()->PostInvalidation();
}


EBakeMapType UBakeMeshAttributeMapsToolBase::GetMapTypes(const int32& MapTypes)
{
	EBakeMapType OutMapTypes = (EBakeMapType)MapTypes & EBakeMapType::All;
	// Force AO bake for BentNormal preview
	if ((bool)(OutMapTypes & EBakeMapType::BentNormal))
	{
		OutMapTypes |= EBakeMapType::AmbientOcclusion;
	}
	return OutMapTypes;
}


TArray<EBakeMapType> UBakeMeshAttributeMapsToolBase::GetMapTypesArray(const int32& MapTypes)
{
	TArray<EBakeMapType> OutMapTypes;
	int32 Bitfield = MapTypes & (int32)EBakeMapType::All;
	for (int32 BitIdx = 0; Bitfield; Bitfield >>= 1, ++BitIdx)
	{
		if (Bitfield & 1)
		{
			OutMapTypes.Add((EBakeMapType)(1 << BitIdx));
		}
	}
	return OutMapTypes;
}


FTexture2DBuilder::ETextureType UBakeMeshAttributeMapsToolBase::GetTextureType(EBakeMapType MapType)
{
	FTexture2DBuilder::ETextureType TexType = FTexture2DBuilder::ETextureType::Color;
	switch (MapType)
	{
	default:
		checkNoEntry();
		break;
	case EBakeMapType::TangentSpaceNormalMap:
		TexType = FTexture2DBuilder::ETextureType::NormalMap;
		break;
	case EBakeMapType::AmbientOcclusion:
		TexType = FTexture2DBuilder::ETextureType::AmbientOcclusion;
		break;
	case EBakeMapType::BentNormal:
		TexType = FTexture2DBuilder::ETextureType::NormalMap;
		break;
	case EBakeMapType::Curvature:
	case EBakeMapType::NormalImage:
	case EBakeMapType::FaceNormalImage:
	case EBakeMapType::PositionImage:
		TexType = FTexture2DBuilder::ETextureType::ColorLinear;
		break;
	case EBakeMapType::MaterialID:
	case EBakeMapType::VertexColorImage:
	case EBakeMapType::Texture2DImage:
	case EBakeMapType::MultiTexture:
		break;
	}
	return TexType;
}


void UBakeMeshAttributeMapsToolBase::GetTextureName(const EBakeMapType MapType, const FString& BaseName, FString& TexName)
{
	switch (MapType)
	{
	default:
		checkNoEntry();
		break;
	case EBakeMapType::TangentSpaceNormalMap:
		TexName = FString::Printf(TEXT("%s_Normals"), *BaseName);
		break;
	case EBakeMapType::AmbientOcclusion:
		TexName = FString::Printf(TEXT("%s_Occlusion"), *BaseName);
		break;
	case EBakeMapType::BentNormal:
		TexName = FString::Printf(TEXT("%s_BentNormal"), *BaseName);
		break;
	case EBakeMapType::Curvature:
		TexName = FString::Printf(TEXT("%s_Curvature"), *BaseName);
		break;
	case EBakeMapType::NormalImage:
		TexName = FString::Printf(TEXT("%s_NormalImg"), *BaseName);
		break;
	case EBakeMapType::FaceNormalImage:
		TexName = FString::Printf(TEXT("%s_FaceNormalImg"), *BaseName);
		break;
	case EBakeMapType::MaterialID:
		TexName = FString::Printf(TEXT("%s_MaterialIDImg"), *BaseName);
		break;
	case EBakeMapType::VertexColorImage:
		TexName = FString::Printf(TEXT("%s_VertexColorIDImg"), *BaseName);
		break;
	case EBakeMapType::PositionImage:
		TexName = FString::Printf(TEXT("%s_PositionImg"), *BaseName);
		break;
	case EBakeMapType::Texture2DImage:
		TexName = FString::Printf(TEXT("%s_TextureImg"), *BaseName);
		break;
	case EBakeMapType::MultiTexture:
		TexName = FString::Printf(TEXT("%s_MultiTextureImg"), *BaseName);
		break;
	}
}


int UBakeMeshAttributeMapsToolBase::SelectColorTextureToBake(const TArray<UTexture*>& Textures)
{
	TArray<int> TextureVotes;
	TextureVotes.Init(0, Textures.Num());

	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		UTexture* Tex = Textures[TextureIndex];
		UTexture2D* Tex2D = Cast<UTexture2D>(Tex);

		if (Tex2D)
		{
			// Texture uses SRGB
			if (Tex->SRGB != 0)
			{
				++TextureVotes[TextureIndex];
			}

#if WITH_EDITORONLY_DATA
			// Texture has multiple channels
			ETextureSourceFormat Format = Tex->Source.GetFormat();
			if (Format == TSF_BGRA8 || Format == TSF_BGRE8 || Format == TSF_RGBA16 || Format == TSF_RGBA16F)
			{
				++TextureVotes[TextureIndex];
			}
#endif

			// What else? Largest texture? Most layers? Most mipmaps?
		}
	}

	int MaxIndex = -1;
	int MaxVotes = -1;
	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		if (TextureVotes[TextureIndex] > MaxVotes)
		{
			MaxIndex = TextureIndex;
			MaxVotes = TextureVotes[TextureIndex];
		}
	}

	return MaxIndex;
}


void UBakeMeshAttributeMapsToolBase::InitializeEmptyMaps()
{
	FTexture2DBuilder NormalsBuilder;
	NormalsBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, FImageDimensions(16, 16));
	NormalsBuilder.Commit(false);
	EmptyNormalMap = NormalsBuilder.GetTexture2D();

	FTexture2DBuilder ColorBuilderBlack;
	ColorBuilderBlack.Initialize(FTexture2DBuilder::ETextureType::Color, FImageDimensions(16, 16));
	ColorBuilderBlack.Clear(FColor(0,0,0));
	ColorBuilderBlack.Commit(false);
	EmptyColorMapBlack = ColorBuilderBlack.GetTexture2D();

	FTexture2DBuilder ColorBuilderWhite;
	ColorBuilderWhite.Initialize(FTexture2DBuilder::ETextureType::Color, FImageDimensions(16, 16));
	ColorBuilderWhite.Clear(FColor::White);
	ColorBuilderWhite.Commit(false);
	EmptyColorMapWhite = ColorBuilderWhite.GetTexture2D();
}

#undef LOCTEXT_NAMESPACE
