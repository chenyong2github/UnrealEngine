// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshBakeFunctions.h"
#include "UDynamicMesh.h"

#include "Sampling/MeshBakerCommon.h"
#include "Sampling/MeshConstantMapEvaluator.h"
#include "Sampling/MeshCurvatureMapEvaluator.h"
#include "Sampling/MeshOcclusionMapEvaluator.h"
#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshResampleImageEvaluator.h"

#include "DynamicMesh/MeshTransforms.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/Texture2DUtil.h"
#include "Async/Async.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshBakeFunctions"

namespace GeometryScriptBakeLocals
{
	FImageDimensions GetDimensions(const EGeometryScriptBakeResolution Resolution)
	{
		int Dimension = 256;
		switch(Resolution)
		{
		case EGeometryScriptBakeResolution::Resolution16:
			Dimension = 16;
			break;
		case EGeometryScriptBakeResolution::Resolution32:
			Dimension = 32;
			break;
		case EGeometryScriptBakeResolution::Resolution64:
			Dimension = 64;
			break;
		case EGeometryScriptBakeResolution::Resolution128:
			Dimension = 128;
			break;
		case EGeometryScriptBakeResolution::Resolution256:
			Dimension = 256;
			break;
		case EGeometryScriptBakeResolution::Resolution512:
			Dimension = 512;
			break;
		case EGeometryScriptBakeResolution::Resolution1024:
			Dimension = 1024;
			break;
		case EGeometryScriptBakeResolution::Resolution2048:
			Dimension = 2048;
			break;
		case EGeometryScriptBakeResolution::Resolution4096:
			Dimension = 4096;
			break;
		case EGeometryScriptBakeResolution::Resolution8192:
			Dimension = 8192;
			break;
		}
		return FImageDimensions(Dimension, Dimension);
	}

	int GetSamplesPerPixel(EGeometryScriptBakeSamplesPerPixel SamplesPerPixel)
	{
		int Samples = 1;
		switch(SamplesPerPixel)
		{
		case EGeometryScriptBakeSamplesPerPixel::Sample1:
			Samples = 1;
			break;
		case EGeometryScriptBakeSamplesPerPixel::Sample4:
			Samples = 4;
			break;
		case EGeometryScriptBakeSamplesPerPixel::Sample16:
			Samples = 16;
			break;
		case EGeometryScriptBakeSamplesPerPixel::Sample64:
			Samples = 64;
			break;
		case EGeometryScriptBakeSamplesPerPixel::Samples256:
			Samples = 256;
			break;
		}
		return Samples;
	}

	FTexture2DBuilder::ETextureType GetTextureType(const FMeshMapEvaluator* Evaluator, const EGeometryScriptBakeBitDepth MapFormat)
	{
		FTexture2DBuilder::ETextureType TexType = FTexture2DBuilder::ETextureType::Color;
		switch (Evaluator->Type())
		{
		default:
			checkNoEntry();
			break;
		case EMeshMapEvaluatorType::Normal:
		{
			TexType = FTexture2DBuilder::ETextureType::NormalMap;
			break;
		}
		case EMeshMapEvaluatorType::Occlusion:
		{
			const FMeshOcclusionMapEvaluator* OcclusionEval = static_cast<const FMeshOcclusionMapEvaluator*>(Evaluator); 
			if (static_cast<bool>(OcclusionEval->OcclusionType & EMeshOcclusionMapType::AmbientOcclusion))
			{
				ensure(OcclusionEval->OcclusionType == EMeshOcclusionMapType::AmbientOcclusion);
				TexType = FTexture2DBuilder::ETextureType::AmbientOcclusion;
			}
			else if (static_cast<bool>(OcclusionEval->OcclusionType & EMeshOcclusionMapType::BentNormal))
			{
				ensure(OcclusionEval->OcclusionType == EMeshOcclusionMapType::BentNormal);
				TexType = FTexture2DBuilder::ETextureType::NormalMap;
			}
			break;
		}
		case EMeshMapEvaluatorType::Property:
		{
			const FMeshPropertyMapEvaluator* PropertyEval = static_cast<const FMeshPropertyMapEvaluator*>(Evaluator);
			switch (PropertyEval->Property)
			{
			case EMeshPropertyMapType::Normal:
			case EMeshPropertyMapType::FacetNormal:
			case EMeshPropertyMapType::Position:
			case EMeshPropertyMapType::UVPosition:
				TexType = FTexture2DBuilder::ETextureType::ColorLinear;
				break;
			case EMeshPropertyMapType::VertexColor:
			case EMeshPropertyMapType::MaterialID:
				TexType = FTexture2DBuilder::ETextureType::Color;
				break;
			}
			break;
		}
		case EMeshMapEvaluatorType::Curvature:
		{
			TexType = FTexture2DBuilder::ETextureType::ColorLinear;
			break;
		}
		case EMeshMapEvaluatorType::ResampleImage:
		case EMeshMapEvaluatorType::MultiResampleImage:
		{
			// For texture output with 16-bit source data, output HDR texture
			if (MapFormat == EGeometryScriptBakeBitDepth::ChannelBits16)
			{
				TexType = FTexture2DBuilder::ETextureType::EmissiveHDR;
			}
			else
			{
				TexType = FTexture2DBuilder::ETextureType::Color;
			}
			break;
		}
		}
		return TexType;
	}

	FMeshCurvatureMapEvaluator::ECurvatureType GetCurvatureType(EGeometryScriptBakeCurvatureTypeMode CurvatureType)
	{
		FMeshCurvatureMapEvaluator::ECurvatureType Result = FMeshCurvatureMapEvaluator::ECurvatureType::Mean;
		switch(CurvatureType)
		{
		case EGeometryScriptBakeCurvatureTypeMode::Mean:
			Result = FMeshCurvatureMapEvaluator::ECurvatureType::Mean;
			break;
		case EGeometryScriptBakeCurvatureTypeMode::Gaussian:
			Result = FMeshCurvatureMapEvaluator::ECurvatureType::Gaussian;
			break;
		case EGeometryScriptBakeCurvatureTypeMode::Min:
			Result = FMeshCurvatureMapEvaluator::ECurvatureType::MinPrincipal;
			break;
		case EGeometryScriptBakeCurvatureTypeMode::Max:
			Result = FMeshCurvatureMapEvaluator::ECurvatureType::MaxPrincipal;
			break;
		}
		return Result;
	}

	FMeshCurvatureMapEvaluator::EColorMode GetCurvatureColorMode(EGeometryScriptBakeCurvatureColorMode ColorMode)
	{
		FMeshCurvatureMapEvaluator::EColorMode Result = FMeshCurvatureMapEvaluator::EColorMode::BlackGrayWhite;
		switch(ColorMode)
		{
		case EGeometryScriptBakeCurvatureColorMode::Grayscale:
			Result = FMeshCurvatureMapEvaluator::EColorMode::BlackGrayWhite;
			break;
		case EGeometryScriptBakeCurvatureColorMode::RedGreenBlue:
			Result = FMeshCurvatureMapEvaluator::EColorMode::RedGreenBlue;
			break;
		case EGeometryScriptBakeCurvatureColorMode::RedBlue:
			Result = FMeshCurvatureMapEvaluator::EColorMode::RedBlue;
			break;
		}
		return Result;
	}

	FMeshCurvatureMapEvaluator::EClampMode GetCurvatureClampMode(EGeometryScriptBakeCurvatureClampMode ClampMode)
	{
		FMeshCurvatureMapEvaluator::EClampMode Result = FMeshCurvatureMapEvaluator::EClampMode::FullRange;
		switch(ClampMode)
		{
		case EGeometryScriptBakeCurvatureClampMode::None:
			Result = FMeshCurvatureMapEvaluator::EClampMode::FullRange;
			break;
		case EGeometryScriptBakeCurvatureClampMode::OnlyNegative:
			Result = FMeshCurvatureMapEvaluator::EClampMode::Negative;
			break;
		case EGeometryScriptBakeCurvatureClampMode::OnlyPositive:
			Result = FMeshCurvatureMapEvaluator::EClampMode::Positive;
			break;
		}
		return Result;
	}

	TUniquePtr<FMeshMapBaker> BakeTextureImpl(
		UDynamicMesh* TargetMesh,
		FTransform TargetTransform,
		FGeometryScriptBakeTargetMeshOptions TargetOptions,
		UDynamicMesh* SourceMesh,
		FTransform SourceTransform,
		FGeometryScriptBakeSourceMeshOptions SourceOptions,
		const TArray<FGeometryScriptBakeTypeOptions>& BakeTypes,
		FGeometryScriptBakeTextureOptions BakeOptions,
		TArray<FGeometryScriptDebugMessage>* Debug)
	{
		if (TargetMesh == nullptr)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidTargetMesh", "BakeTexture: TargetMesh is Null"));
			return nullptr;
		}
		if (SourceMesh == nullptr)
		{
			UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidSourceMesh", "BakeTexture: SourceMesh is Null"));
			return nullptr;
		}
		if (BakeTypes.Num() == 0)
		{
			UE::Geometry::AppendWarning(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_BakeTypesEmpty", "BakeTexture: BakeTypes is empty"));
			return nullptr;
		}

		TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> TargetMeshTangents;
		TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> SourceMeshTangents;
		auto GetMeshTangents = [TargetMesh, SourceMesh, Debug](UDynamicMesh* Mesh, TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe>& Tangents)
		{
			if (!Tangents)
			{
				Tangents = MakeShared<FMeshTangentsd, ESPMode::ThreadSafe>(Mesh->GetMeshPtr());
				Tangents->CopyTriVertexTangents(Mesh->GetMeshRef());

				// Validate the tangents
				if (!FDynamicMeshTangents(Mesh->GetMeshPtr()).HasValidTangents(true))
				{
					if (Mesh == TargetMesh)
					{
						UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidTargetTangents", "BakeTexture: Target Mesh tangents are invalid."));
					}
					else
					{
						ensure(Mesh == SourceMesh);
						UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidSourceTangents", "BakeTexture: Source Mesh tangents are invalid."));
					}
					return false;
				}
			}
			return true;
		};

		const bool bIsBakeToSelf = (TargetMesh == SourceMesh);

		FDynamicMesh3 SourceMeshCopy;
		const FDynamicMesh3* SourceMeshOriginal = SourceMesh->GetMeshPtr();
		const FDynamicMesh3* SourceMeshToUse = SourceMeshOriginal;
		if (BakeOptions.bProjectionInWorldSpace && !bIsBakeToSelf)
		{
			// Transform the SourceMesh into TargetMesh local space using a copy (oof)
			// TODO: Remove this once we have support for transforming rays in the core bake loop
			SourceMeshCopy = *SourceMeshOriginal;
			const FTransformSRT3d SourceToWorld = SourceTransform;
			MeshTransforms::ApplyTransform(SourceMeshCopy, SourceToWorld);
			const FTransformSRT3d TargetToWorld = TargetTransform;
			MeshTransforms::ApplyTransform(SourceMeshCopy, TargetToWorld.Inverse());
			SourceMeshToUse = &SourceMeshCopy;
		}

		FImageDimensions BakeDimensions = GetDimensions(BakeOptions.Resolution);
		const FDynamicMeshAABBTree3 DetailSpatial(SourceMeshToUse);
		FMeshBakerDynamicMeshSampler DetailSampler(SourceMeshToUse, &DetailSpatial);

		TUniquePtr<FMeshMapBaker> Result = MakeUnique<FMeshMapBaker>();
		FMeshMapBaker& Baker = *Result;
		Baker.SetTargetMesh(TargetMesh->GetMeshPtr());
		Baker.SetTargetMeshUVLayer(TargetOptions.TargetUVLayer);
		Baker.SetDetailSampler(&DetailSampler);
		Baker.SetDimensions(BakeDimensions);
		Baker.SetProjectionDistance(BakeOptions.ProjectionDistance);
		Baker.SetSamplesPerPixel(GetSamplesPerPixel(BakeOptions.SamplesPerPixel));

		bool bSupportsSourceNormalMap = false;
		TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> SourceTexture;
		for (const FGeometryScriptBakeTypeOptions& Options : BakeTypes)
		{
			switch(Options.BakeType)
			{
			case EGeometryScriptBakeTypes::TangentSpaceNormal:
			{
				if (!GetMeshTangents(TargetMesh, TargetMeshTangents))
				{
					return nullptr;
				}
				TSharedPtr<FMeshNormalMapEvaluator> NormalEval = MakeShared<FMeshNormalMapEvaluator>();
				bSupportsSourceNormalMap = true;
				Baker.AddEvaluator(NormalEval);
				break;
			}
			case EGeometryScriptBakeTypes::ObjectSpaceNormal:
			{
				TSharedPtr<FMeshPropertyMapEvaluator> PropertyEval = MakeShared<FMeshPropertyMapEvaluator>();
				PropertyEval->Property = EMeshPropertyMapType::Normal;
				bSupportsSourceNormalMap = true;
				Baker.AddEvaluator(PropertyEval);
				break;
			}
			case EGeometryScriptBakeTypes::FaceNormal:
			{
				TSharedPtr<FMeshPropertyMapEvaluator> PropertyEval = MakeShared<FMeshPropertyMapEvaluator>();
				PropertyEval->Property = EMeshPropertyMapType::FacetNormal;
				Baker.AddEvaluator(PropertyEval);
				break;
			}
			case EGeometryScriptBakeTypes::BentNormal:
			{
				if (!GetMeshTangents(TargetMesh, TargetMeshTangents))
				{
					return nullptr;
				}
				FGeometryScriptBakeType_Occlusion* OcclusionOptions = static_cast<FGeometryScriptBakeType_Occlusion*>(Options.Options.Get());
				TSharedPtr<FMeshOcclusionMapEvaluator> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator>();
				OcclusionEval->OcclusionType = EMeshOcclusionMapType::BentNormal;
				OcclusionEval->NumOcclusionRays = OcclusionOptions->OcclusionRays;
				OcclusionEval->MaxDistance = (OcclusionOptions->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionOptions->MaxDistance;
				OcclusionEval->SpreadAngle = OcclusionOptions->SpreadAngle;
				Baker.AddEvaluator(OcclusionEval);
				break;
			}
			case EGeometryScriptBakeTypes::Position:
			{
				TSharedPtr<FMeshPropertyMapEvaluator> PropertyEval = MakeShared<FMeshPropertyMapEvaluator>();
				PropertyEval->Property = EMeshPropertyMapType::Position;
				Baker.AddEvaluator(PropertyEval);
				break;
			}
			case EGeometryScriptBakeTypes::Curvature:
			{
				FGeometryScriptBakeType_Curvature* CurvatureOptions = static_cast<FGeometryScriptBakeType_Curvature*>(Options.Options.Get());
				TSharedPtr<FMeshCurvatureMapEvaluator> CurvatureEval = MakeShared<FMeshCurvatureMapEvaluator>();
				CurvatureEval->UseCurvatureType = GetCurvatureType(CurvatureOptions->CurvatureType);
				CurvatureEval->UseColorMode = GetCurvatureColorMode(CurvatureOptions->ColorMapping);
				CurvatureEval->RangeScale = CurvatureOptions->ColorRangeMultiplier;
				CurvatureEval->MinRangeScale = CurvatureOptions->MinRangeMultiplier;
				CurvatureEval->UseClampMode = GetCurvatureClampMode(CurvatureOptions->Clamping);
				Baker.AddEvaluator(CurvatureEval);
				break;
			}
			case EGeometryScriptBakeTypes::AmbientOcclusion:
			{
				FGeometryScriptBakeType_Occlusion* OcclusionOptions = static_cast<FGeometryScriptBakeType_Occlusion*>(Options.Options.Get());
				TSharedPtr<FMeshOcclusionMapEvaluator> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator>();
				OcclusionEval->OcclusionType = EMeshOcclusionMapType::AmbientOcclusion;
				OcclusionEval->NumOcclusionRays = OcclusionOptions->OcclusionRays;
				OcclusionEval->MaxDistance = (OcclusionOptions->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionOptions->MaxDistance;
				OcclusionEval->SpreadAngle = OcclusionOptions->SpreadAngle;
				OcclusionEval->BiasAngleDeg = OcclusionOptions->BiasAngle;
				Baker.AddEvaluator(OcclusionEval);
				break;
			}
			case EGeometryScriptBakeTypes::Texture:
			{
				FGeometryScriptBakeType_Texture* TextureOptions = static_cast<FGeometryScriptBakeType_Texture*>(Options.Options.Get());

				// TODO: Add support for sampling different texture maps per Texture evaluator in a single pass. 
				if (!SourceTexture && TextureOptions->SourceTexture)
				{
					SourceTexture = MakeShared<TImageBuilder<FVector4f>>();
					if (!UE::AssetUtils::ReadTexture(TextureOptions->SourceTexture, *SourceTexture, false))
					{
						UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidSourceTexture", "BakeTexture: Failed to read SourceTexture"));
					}
					else
					{
						DetailSampler.SetTextureMap(SourceMeshToUse, IMeshBakerDetailSampler::FBakeDetailTexture(SourceTexture.Get(), TextureOptions->SourceUVLayer));
					}
				}
				
				TSharedPtr<FMeshResampleImageEvaluator> TextureEval = MakeShared<FMeshResampleImageEvaluator>();
				Baker.AddEvaluator(TextureEval);
				break;
			}
			case EGeometryScriptBakeTypes::MultiTexture:
			{
				FGeometryScriptBakeType_MultiTexture* TextureOptions = static_cast<FGeometryScriptBakeType_MultiTexture*>(Options.Options.Get());
				TSharedPtr<FMeshMultiResampleImageEvaluator> TextureEval = MakeShared<FMeshMultiResampleImageEvaluator>();

				if (TextureOptions->MaterialIDSourceTextures.Num())
				{
					TextureEval->MultiTextures.SetNum(TextureOptions->MaterialIDSourceTextures.Num());
					for (int32 MaterialId = 0; MaterialId < TextureEval->MultiTextures.Num(); ++MaterialId)
					{
						if (UTexture2D* Texture = TextureOptions->MaterialIDSourceTextures[MaterialId])
						{
							TextureEval->MultiTextures[MaterialId] = MakeShared<TImageBuilder<FVector4f>>();
							if (!UE::AssetUtils::ReadTexture(Texture, *TextureEval->MultiTextures[MaterialId], false))
							{
								UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidMultiTexture", "BakeTexture: Failed to read MaterialIDSourceTexture"));
							}
						}
					}
				}
				Baker.AddEvaluator(TextureEval);
				break;
			}
			case EGeometryScriptBakeTypes::VertexColor:
			{
				TSharedPtr<FMeshPropertyMapEvaluator> PropertyEval = MakeShared<FMeshPropertyMapEvaluator>();
				PropertyEval->Property = EMeshPropertyMapType::VertexColor;
				Baker.AddEvaluator(PropertyEval);
				break;
			}
			case EGeometryScriptBakeTypes::MaterialID:
			{
				TSharedPtr<FMeshPropertyMapEvaluator> PropertyEval = MakeShared<FMeshPropertyMapEvaluator>();
				PropertyEval->Property = EMeshPropertyMapType::MaterialID;
				Baker.AddEvaluator(PropertyEval);
				break;
			}
			default:
				break;
			}
		}

		TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> SourceNormalMap;
		if (bSupportsSourceNormalMap && SourceOptions.SourceNormalMap)
		{
			SourceNormalMap = MakeShared<TImageBuilder<FVector4f>>();
			if (!UE::AssetUtils::ReadTexture(SourceOptions.SourceNormalMap, *SourceNormalMap, false))
			{
				UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BakeTexture_InvalidSourceNormalMap", "BakeTexture: Failed to read SourceNormalMap"));
			}
			else
			{
				DetailSampler.SetNormalTextureMap(SourceMeshToUse,
				IMeshBakerDetailSampler::FBakeDetailNormalTexture(
						SourceNormalMap.Get(),
						SourceOptions.SourceNormalUVLayer,
						SourceOptions.SourceNormalSpace == EGeometryScriptBakeNormalSpace::Tangent ? IMeshBakerDetailSampler::EBakeDetailNormalSpace::Tangent : IMeshBakerDetailSampler::EBakeDetailNormalSpace::Object));

				if (!GetMeshTangents(SourceMesh, SourceMeshTangents))
				{
					return nullptr;
				}
				DetailSampler.SetTangents(SourceMeshToUse, SourceMeshTangents.Get());
			}
		}

		if (TargetMeshTangents)
		{
			Baker.SetTargetMeshTangents(TargetMeshTangents);
		}

		Baker.Bake();

		return MoveTemp(Result);
	}

	void GetTexturesFromBaker(FMeshMapBaker* Baker, const EGeometryScriptBakeBitDepth BakeBitDepth, TArray<UTexture2D*>& Textures)
	{
		if (!Baker)
		{
			return;
		}
		
		const FImageDimensions BakeDimensions = Baker->GetDimensions();
		const int NumEval = Baker->NumEvaluators();
		for (int EvalIdx = 0; EvalIdx < NumEval; ++EvalIdx)
		{
			// For 8-bit color textures, ensure that the source data is in sRGB.
			const FTexture2DBuilder::ETextureType TexType = GetTextureType(Baker->GetEvaluator(EvalIdx), BakeBitDepth);
			const bool bConvertToSRGB = (TexType == FTexture2DBuilder::ETextureType::Color);
			const ETextureSourceFormat SourceDataFormat = (BakeBitDepth == EGeometryScriptBakeBitDepth::ChannelBits16 ? TSF_RGBA16F : TSF_BGRA8);

			constexpr int ResultIdx = 0;
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(TexType, BakeDimensions);
			TextureBuilder.Copy(*Baker->GetBakeResults(EvalIdx)[ResultIdx], bConvertToSRGB);
			TextureBuilder.Commit(false);

			// Copy image to source data after commit. This will avoid incurring
			// the cost of hitting the DDC for texture compile while iterating on
			// bake settings. Since this dirties the texture, the next time the texture
			// is used after accepting the final texture, the DDC will trigger and
			// properly recompile the platform data.
			const bool bConvertSourceToSRGB = bConvertToSRGB && SourceDataFormat == TSF_BGRA8;
			TextureBuilder.CopyImageToSourceData(*Baker->GetBakeResults(EvalIdx)[ResultIdx], SourceDataFormat, bConvertSourceToSRGB);
			Textures.Add(TextureBuilder.GetTexture2D());
		}
	}
}


FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeTangentNormal()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::TangentSpaceNormal;
	return Output;
}


FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeObjectNormal()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::ObjectSpaceNormal;
	return Output;
}


FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeFaceNormal()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::FaceNormal;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeBentNormal(
	int OcclusionRays,
	float MaxDistance,
	float SpreadAngle)
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::BentNormal;
	const TSharedPtr<FGeometryScriptBakeType_Occlusion> OcclusionOptions = MakeShared<FGeometryScriptBakeType_Occlusion>();
	OcclusionOptions->OcclusionRays = OcclusionRays;
	OcclusionOptions->MaxDistance = MaxDistance;
	OcclusionOptions->SpreadAngle = SpreadAngle;
	Output.Options = OcclusionOptions;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypePosition()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::Position;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeCurvature(
	EGeometryScriptBakeCurvatureTypeMode CurvatureType,
	EGeometryScriptBakeCurvatureColorMode ColorMapping,
	float ColorRangeMultiplier,
	float MinRangeMultiplier,
	EGeometryScriptBakeCurvatureClampMode Clamping)
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::Curvature;
	const TSharedPtr<FGeometryScriptBakeType_Curvature> CurvatureOptions = MakeShared<FGeometryScriptBakeType_Curvature>();
	CurvatureOptions->CurvatureType = CurvatureType;
	CurvatureOptions->ColorMapping = ColorMapping;
	CurvatureOptions->ColorRangeMultiplier = ColorRangeMultiplier;
	CurvatureOptions->MinRangeMultiplier = MinRangeMultiplier;
	Output.Options = CurvatureOptions;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeAmbientOcclusion(
	int OcclusionRays,
	float MaxDistance,
	float SpreadAngle,
	float BiasAngle)
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::AmbientOcclusion;
	const TSharedPtr<FGeometryScriptBakeType_Occlusion> OcclusionOptions = MakeShared<FGeometryScriptBakeType_Occlusion>();
	OcclusionOptions->OcclusionRays = OcclusionRays;
	OcclusionOptions->MaxDistance = MaxDistance;
	OcclusionOptions->SpreadAngle = SpreadAngle;
	OcclusionOptions->BiasAngle = BiasAngle;
	Output.Options = OcclusionOptions;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeTexture(
	UTexture2D* SourceTexture,
	int SourceUVLayer)
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::Texture;
	const TSharedPtr<FGeometryScriptBakeType_Texture> TextureOptions = MakeShared<FGeometryScriptBakeType_Texture>();
	TextureOptions->SourceTexture = SourceTexture;
	TextureOptions->SourceUVLayer = SourceUVLayer;
	Output.Options = TextureOptions;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeMultiTexture(
	const TArray<UTexture2D*>& MaterialIDSourceTextures,
	int SourceUVLayer)
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::MultiTexture;
	const TSharedPtr<FGeometryScriptBakeType_MultiTexture> MultiTextureOptions = MakeShared<FGeometryScriptBakeType_MultiTexture>();
	MultiTextureOptions->MaterialIDSourceTextures = MaterialIDSourceTextures;
	MultiTextureOptions->SourceUVLayer = SourceUVLayer;
	Output.Options = MultiTextureOptions;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeVertexColor()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::VertexColor;
	return Output;
}

FGeometryScriptBakeTypeOptions UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeMaterialID()
{
	FGeometryScriptBakeTypeOptions Output;
	Output.BakeType = EGeometryScriptBakeTypes::MaterialID;
	return Output;
}


TArray<UTexture2D*> UGeometryScriptLibrary_MeshBakeFunctions::BakeTexture(
	UDynamicMesh* TargetMesh,
	FTransform TargetTransform,
	FGeometryScriptBakeTargetMeshOptions TargetOptions,
	UDynamicMesh* SourceMesh,
	FTransform SourceTransform,
	FGeometryScriptBakeSourceMeshOptions SourceOptions,
	const TArray<FGeometryScriptBakeTypeOptions>& BakeTypes,
	FGeometryScriptBakeTextureOptions BakeOptions,
	UGeometryScriptDebug* Debug)
{
	const TUniquePtr<FMeshMapBaker> Baker = GeometryScriptBakeLocals::BakeTextureImpl(
		TargetMesh,
		TargetTransform,
		TargetOptions,
		SourceMesh,
		SourceTransform,
		SourceOptions,
		BakeTypes,
		BakeOptions,
		Debug ? &Debug->Messages : nullptr);

	TArray<UTexture2D*> TextureOutput;
	GeometryScriptBakeLocals::GetTexturesFromBaker(Baker.Get(), BakeOptions.BitDepth, TextureOutput);

	return TextureOutput;
}

void UGeometryScriptLibrary_MeshBakeFunctions::BakeTextureAsyncBegin(
	const FBakeTextureDelegate& Completed,
	const int BakeId,
	UDynamicMesh* TargetMesh,
	FTransform TargetTransform,
	FGeometryScriptBakeTargetMeshOptions TargetOptions,
	UDynamicMesh* SourceMesh,
	FTransform SourceTransform,
	FGeometryScriptBakeSourceMeshOptions SourceOptions,
	const TArray<FGeometryScriptBakeTypeOptions>& BakeTypes,
	FGeometryScriptBakeTextureOptions BakeOptions,
	UGeometryScriptDebug* Debug)
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Completed, BakeId, TargetMesh, TargetTransform, TargetOptions, SourceMesh, SourceTransform, SourceOptions, BakeTypes, BakeOptions, Debug]()
	{
		// FGeometryScriptDebug is not thread-safe. Instantiate a local
		// debug message array to collect any debug errors and append those
		// messages back on the game thread.
		TSharedPtr<TArray<FGeometryScriptDebugMessage>, ESPMode::ThreadSafe> DebugMessages;
		if (Debug)
		{
			DebugMessages = MakeShared<TArray<FGeometryScriptDebugMessage>, ESPMode::ThreadSafe>();
		}
		
		TUniquePtr<FMeshMapBaker> Baker = GeometryScriptBakeLocals::BakeTextureImpl(
			TargetMesh,
			TargetTransform,
			TargetOptions,
			SourceMesh,
			SourceTransform,
			SourceOptions,
			BakeTypes,
			BakeOptions,
			DebugMessages.Get());

		TSharedPtr<FMeshMapBaker> SharedBaker = MakeShareable<FMeshMapBaker>(Baker.Release());

		AsyncTask(ENamedThreads::GameThread, [Completed, BakeId, SharedBaker, BakeOptions, Debug, DebugMessages]()
		{
			if (Debug && DebugMessages)
			{
				for (const FGeometryScriptDebugMessage& Message : *DebugMessages)
				{
					Debug->Messages.Add(Message);
				}
			}
			
			FGeometryScriptBakeTextureAsyncResult Result;
			Result.BakeOptions = BakeOptions;
			Result.BakeResult = SharedBaker;
			Completed.ExecuteIfBound(BakeId, Result);
		});
	});
}

TArray<UTexture2D*> UGeometryScriptLibrary_MeshBakeFunctions::BakeTextureAsyncEnd(
	const FGeometryScriptBakeTextureAsyncResult& Result)
{
	FMeshMapBaker* Baker = Result.BakeResult.Get();
	const FGeometryScriptBakeTextureOptions BakeOptions = Result.BakeOptions;

	TArray<UTexture2D*> TextureOutput;
	GeometryScriptBakeLocals::GetTexturesFromBaker(Baker, BakeOptions.BitDepth, TextureOutput);

	return TextureOutput;
}

#undef LOCTEXT_NAMESPACE
