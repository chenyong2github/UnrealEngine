// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshUtilities.h"
#include "MeshUtilitiesPrivate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "RawMesh.h"
#include "StaticMeshResources.h"
#include "MeshCardRepresentation.h"
#include "DistanceFieldAtlas.h"

#if USE_EMBREE

#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

typedef TArray<uint32> FInputPrimitiveIds;

bool SetupEmbreeScene(
	FString MeshName,
	int32 PrimitiveRangeStart, 
	int32 PrimitiveRangeEnd, 
	const FInputPrimitiveIds& InputPrimitiveIds,
	const FSourceMeshDataForDerivedDataTask& SourceMeshData,
	const FStaticMeshLODResources& LODModel,
	RTCDevice EmbreeDevice,
	RTCScene& EmbreeScene)
{
	const int32 NumVertices = SourceMeshData.IsValid() ? SourceMeshData.Vertices.Num() : LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices();

	EmbreeScene = rtcDeviceNewScene(EmbreeDevice, RTC_SCENE_STATIC, RTC_INTERSECT1);
			
	RTCError ReturnErrorNewScene = rtcDeviceGetError(EmbreeDevice);
	if (ReturnErrorNewScene != RTC_NO_ERROR)
	{
		UE_LOG(LogMeshUtilities, Warning, TEXT("GenerateCardRepresentationData failed for %s. Embree rtcDeviceNewScene failed. Code: %d"), *MeshName, (int32)ReturnErrorNewScene);
		rtcDeleteDevice(EmbreeDevice);
		return false;
	}

	const int32 NumTriangles = PrimitiveRangeEnd - PrimitiveRangeStart;

	FVector4* EmbreeVertices = NULL;
	int32* EmbreeIndices = NULL;

	uint32 GeomID = rtcNewTriangleMesh(EmbreeScene, RTC_GEOMETRY_STATIC, NumTriangles, NumVertices);

	EmbreeVertices = (FVector4*)rtcMapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	EmbreeIndices = (int32*)rtcMapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	for (int32 PrimitiveIndex = PrimitiveRangeStart; PrimitiveIndex < PrimitiveRangeEnd; PrimitiveIndex++)
	{
		int32 TriangleIndex = InputPrimitiveIds[PrimitiveIndex];

		int32 I0, I1, I2;
		FVector V0, V1, V2;

		if (SourceMeshData.IsValid())
		{
			I0 = SourceMeshData.Indices[TriangleIndex * 3 + 0];
			I1 = SourceMeshData.Indices[TriangleIndex * 3 + 1];
			I2 = SourceMeshData.Indices[TriangleIndex * 3 + 2];

			V0 = SourceMeshData.Vertices[I0].Position;
			V1 = SourceMeshData.Vertices[I1].Position;
			V2 = SourceMeshData.Vertices[I2].Position;
		}
		else
		{
			const FIndexArrayView Indices = LODModel.IndexBuffer.GetArrayView();
			I0 = Indices[TriangleIndex * 3 + 0];
			I1 = Indices[TriangleIndex * 3 + 1];
			I2 = Indices[TriangleIndex * 3 + 2];

			V0 = LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(I0);
			V1 = LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(I1);
			V2 = LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(I2);
		}

		EmbreeIndices[TriangleIndex * 3 + 0] = I0;
		EmbreeIndices[TriangleIndex * 3 + 1] = I1;
		EmbreeIndices[TriangleIndex * 3 + 2] = I2;

		EmbreeVertices[I0] = FVector4(V0, 0);
		EmbreeVertices[I1] = FVector4(V1, 0);
		EmbreeVertices[I2] = FVector4(V2, 0);
	}

	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	RTCError ReturnError = rtcDeviceGetError(EmbreeDevice);
	if (ReturnError != RTC_NO_ERROR)
	{
		UE_LOG(LogMeshUtilities, Warning, TEXT("GenerateCardRepresentationData failed for %s. Embree rtcUnmapBuffer failed. Code: %d"), *MeshName, (int32)ReturnError);
		rtcDeleteScene(EmbreeScene);
		return false;
	}

	rtcCommit(EmbreeScene);
	RTCError ReturnError2 = rtcDeviceGetError(EmbreeDevice);
	if (ReturnError2 != RTC_NO_ERROR)
	{
		UE_LOG(LogMeshUtilities, Warning, TEXT("GenerateCardRepresentationData failed for %s. Embree rtcCommit failed. Code: %d"), *MeshName, (int32)ReturnError2);
		rtcDeleteScene(EmbreeScene);
		return false;
	}

	return true;
}

struct FCardBuildEmbreeRay : public RTCRay
{
	FCardBuildEmbreeRay()
	{
		u = v = 0;
		time = 0;
		mask = 0xFFFFFFFF;
		geomID = -1;
		instID = -1;
		primID = -1;
	}
};

class FGenerateCardMeshContext
{
public:
	const FString& MeshName;
	RTCScene FullMeshEmbreeScene;
	RTCDevice EmbreeDevice;
	FCardRepresentationData& OutData;

	FGenerateCardMeshContext(const FString& InMeshName, RTCScene InEmbreeScene, RTCDevice InEmbreeDevice, FCardRepresentationData& InOutData) :
		MeshName(InMeshName),
		FullMeshEmbreeScene(InEmbreeScene),
		EmbreeDevice(InEmbreeDevice),
		OutData(InOutData)
	{}
};

class FDistanceFieldSamplingData
{
public:
	bool bEightBitFixedPoint;
	const TArray<uint8>* SourceDataPtr;
	FVector2D DistanceMinMax;
	FBox LocalBoundingBox;
	float SDFScale;
	FIntVector Size;
};

float SampleDistanceField(const FVector& SamplePosition, FDistanceFieldSamplingData DistanceField)
{
	FVector VolumeUV = (SamplePosition - DistanceField.LocalBoundingBox.Min) / (DistanceField.LocalBoundingBox.Max - DistanceField.LocalBoundingBox.Min);
	FVector TexelPosition = VolumeUV * FVector(DistanceField.Size);
	TexelPosition.X = FMath::Clamp<float>(TexelPosition.X, 0.0f, DistanceField.Size.X - 1);
	TexelPosition.Y = FMath::Clamp<float>(TexelPosition.Y, 0.0f, DistanceField.Size.Y - 1);
	TexelPosition.Z = FMath::Clamp<float>(TexelPosition.Z, 0.0f, DistanceField.Size.Z - 1);

	FIntVector MinAddress(TexelPosition);
	FVector Fractional = TexelPosition - FVector(MinAddress);

	float Result = 0;
	float TotalWeight = 0;

	for (int32 Z = 0; Z < 2; Z++)
	{
		for (int32 Y = 0; Y < 2; Y++)
		{
			for (int32 X = 0; X < 2; X++)
			{
				FVector BilinearWeights(X == 0 ? 1 - Fractional.X : Fractional.X, Y == 0 ? 1 - Fractional.Y : Fractional.Y, Z == 0 ? 1 - Fractional.Z : Fractional.Z);
				const float BilinearWeight = BilinearWeights.X * BilinearWeights.Y * BilinearWeights.Z;

				if (BilinearWeight > 0.0f)
				{
					FIntVector TexelAddress(X + MinAddress.X, Y + MinAddress.Y, Z + MinAddress.Z);

					TexelAddress.X = FMath::Min(TexelAddress.X, DistanceField.Size.X - 1);
					TexelAddress.Y = FMath::Min(TexelAddress.Y, DistanceField.Size.Y - 1);
					TexelAddress.Z = FMath::Min(TexelAddress.Z, DistanceField.Size.Z - 1);

					const int32 TexelOffset = (TexelAddress.Z * DistanceField.Size.Y + TexelAddress.Y) * DistanceField.Size.X + TexelAddress.X;
					float SDFValue;

					if (DistanceField.bEightBitFixedPoint)
					{
						const uint8* SDFData = (const uint8*)DistanceField.SourceDataPtr->GetData();
						const uint8 CurrentTexelValue = SDFData[TexelOffset];
						SDFValue = CurrentTexelValue * (DistanceField.DistanceMinMax.Y - DistanceField.DistanceMinMax.X) + DistanceField.DistanceMinMax.X;
					}
					else
					{
						const FFloat16* SDFData = (const FFloat16*)DistanceField.SourceDataPtr->GetData();
						const FFloat16 CurrentTexelValue = SDFData[TexelOffset];
						SDFValue = CurrentTexelValue.GetFloat() * DistanceField.SDFScale;
					}

					TotalWeight += BilinearWeight;
					Result += SDFValue * BilinearWeight;
				}
			}	
		}
	}

	return Result;
}

#endif

FVector TransformFaceExtent(FVector Extent, int32 Orientation)
{
	if (Orientation / 2 == 2)
	{
		return FVector(Extent.Y, Extent.X, Extent.Z);
	}
	else if (Orientation / 2 == 1)
	{
		return FVector(Extent.Z, Extent.X, Extent.Y);
	}
	else
	{
		return FVector(Extent.Y, Extent.Z, Extent.X); 
	}
}

class FLayerIndexCell
{
public:
	int32 LayerIndex;
	int32 NumHits;
};

class FLayers
{
public:
	TArray<FBox> Bounds;
	TArray<FLayerIndexCell> IndexVolume;
	TArray<int32> LayerIndexToFaceIndex;
};

#if USE_EMBREE

void BuildCubeMapTree(const FBox& CardBounds, const FGenerateCardMeshContext& Context, FCardRepresentationData& OutData)
{
	static const auto CVarLumenCubeMapTreeBuildMinSurface = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.LumenCubeMapTreeBuild.MinSurface"));
	const float MinSurfaceThreshold = CVarLumenCubeMapTreeBuildMinSurface->GetValueOnAnyThread();

	const float MaxExtent = CardBounds.GetExtent().GetMax();
	// Ensure bounds don't have zero extent (was possible for planes)
	const FBox CubeMapTreeBounds = CardBounds.ExpandBy(FVector::Max(CardBounds.GetExtent() * 0.05f, .001f * FVector(MaxExtent, MaxExtent, MaxExtent)));

	FIntVector LUTVolumeResolution(1, 1, 1);

	OutData.CubeMapTreeBuildData.LUTVolumeBounds = CubeMapTreeBounds;
	OutData.CubeMapTreeBuildData.LUTVolumeResolution = LUTVolumeResolution;
	OutData.CubeMapTreeBuildData.CubeMapBuiltData.Reset();
	OutData.CubeMapTreeBuildData.FaceBuiltData.Reset();

	const int32 RaysPerCell = 64;

	FLayers LayersPerDirection[6];

	TArray<int32> NumHitVolume;
	NumHitVolume.SetNumUninitialized(LUTVolumeResolution.X * LUTVolumeResolution.Y * LUTVolumeResolution.Z);

	for (int32 Orientation = 0; Orientation < 6; ++Orientation)
	{
		FLayers& Layers = LayersPerDirection[Orientation];

		FIntPoint HeighfieldSize(0, 0);
		int32 NumLayers = 0;
		FVector RayDirection(0.0f, 0.0f, 0.0f);
		FVector RayOriginFrame = CubeMapTreeBounds.Min;
		FVector HeighfieldStepX(0.0f, 0.0f, 0.0f);
		FVector HeighfieldStepY(0.0f, 0.0f, 0.0f);

		switch (Orientation)
		{
			case 0: 
				RayDirection.X = +1.0f; 
				NumLayers = LUTVolumeResolution.X;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.Y, LUTVolumeResolution.Z) * RaysPerCell;
				HeighfieldStepX = FVector(0.0f, CubeMapTreeBounds.GetSize().Y / HeighfieldSize.X, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, CubeMapTreeBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 1: 
				RayDirection.X = -1.0f; 
				NumLayers = LUTVolumeResolution.X;
				RayOriginFrame.X = CubeMapTreeBounds.Max.X;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.Y, LUTVolumeResolution.Z) * RaysPerCell;
				HeighfieldStepX = FVector(0.0f, CubeMapTreeBounds.GetSize().Y / HeighfieldSize.X, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, CubeMapTreeBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 2: 
				RayDirection.Y = +1.0f; 
				NumLayers = LUTVolumeResolution.Y;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.X, LUTVolumeResolution.Z) * RaysPerCell;
				HeighfieldStepX = FVector(CubeMapTreeBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, CubeMapTreeBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 3: 
				RayDirection.Y = -1.0f; 
				NumLayers = LUTVolumeResolution.Y;
				RayOriginFrame.Y = CubeMapTreeBounds.Max.Y;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.X, LUTVolumeResolution.Z) * RaysPerCell;
				HeighfieldStepX = FVector(CubeMapTreeBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, CubeMapTreeBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 4: 
				RayDirection.Z = +1.0f; 
				NumLayers = LUTVolumeResolution.Z;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.X, LUTVolumeResolution.Y) * RaysPerCell;
				HeighfieldStepX = FVector(CubeMapTreeBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, CubeMapTreeBounds.GetSize().Y / HeighfieldSize.Y, 0.0f);
				break;

			case 5: 
				RayDirection.Z = -1.0f; 
				NumLayers = LUTVolumeResolution.Z;
				RayOriginFrame.Z = CubeMapTreeBounds.Max.Z;
				HeighfieldSize = FIntPoint(LUTVolumeResolution.X, LUTVolumeResolution.Y) * RaysPerCell;
				HeighfieldStepX = FVector(CubeMapTreeBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, CubeMapTreeBounds.GetSize().Y / HeighfieldSize.Y, 0.0f);
				break;

			default: 
				check(false);
		};

		Layers.IndexVolume.SetNumUninitialized(LUTVolumeResolution.X * LUTVolumeResolution.Y * LUTVolumeResolution.Z);

		for (int32 CoordZ = 0; CoordZ < LUTVolumeResolution.Z; ++CoordZ)
		{
			for (int32 CoordY = 0; CoordY < LUTVolumeResolution.Y; ++CoordY)
			{
				for (int32 CoordX = 0; CoordX < LUTVolumeResolution.X; ++CoordX)
				{
					const int32 CoordIndex = CoordX + CoordY * LUTVolumeResolution.X + CoordZ * LUTVolumeResolution.X * LUTVolumeResolution.Y;

					Layers.IndexVolume[CoordIndex].LayerIndex = -1;
					Layers.IndexVolume[CoordIndex].NumHits = 0;
				}
			}
		}

		const FVector CellSize = CubeMapTreeBounds.GetSize() / FVector(LUTVolumeResolution);

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			for (int32 CellIndex = 0; CellIndex < LUTVolumeResolution.X * LUTVolumeResolution.Y * LUTVolumeResolution.Z; ++CellIndex)
			{
				NumHitVolume[CellIndex] = 0;
			}

			const FVector LayerOffset = (RayDirection * LayerIndex * CubeMapTreeBounds.GetSize()) / FVector(LUTVolumeResolution);

			for (int32 HeighfieldY = 0; HeighfieldY < HeighfieldSize.Y; ++HeighfieldY)
			{
				for (int32 HeighfieldX = 0; HeighfieldX < HeighfieldSize.X; ++HeighfieldX)
				{
					FVector RayStart = RayOriginFrame + LayerOffset;
					RayStart += (HeighfieldX + 0.5f) * HeighfieldStepX;
					RayStart += (HeighfieldY + 0.5f) * HeighfieldStepY;

					FCardBuildEmbreeRay EmbreeRay;
					EmbreeRay.org[0] = RayStart.X;
					EmbreeRay.org[1] = RayStart.Y;
					EmbreeRay.org[2] = RayStart.Z;
					EmbreeRay.dir[0] = RayDirection.X;
					EmbreeRay.dir[1] = RayDirection.Y;
					EmbreeRay.dir[2] = RayDirection.Z;
					EmbreeRay.tnear = 0.0f;
					EmbreeRay.tfar = 1e9f;

					rtcIntersect(Context.FullMeshEmbreeScene, EmbreeRay);

					if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
					{
						const FVector HitNormal = FVector(EmbreeRay.Ng[0], EmbreeRay.Ng[1], EmbreeRay.Ng[2]).GetSafeNormal();

						if (FVector::DotProduct(RayDirection, HitNormal) < 0)
						{
							const FVector RayHit = RayStart + EmbreeRay.tfar * RayDirection;

							const int32 CellX = FMath::Clamp<int32>((RayHit.X - CubeMapTreeBounds.Min.X) / CellSize.X, 0, LUTVolumeResolution.X - 1);
							const int32 CellY = FMath::Clamp<int32>((RayHit.Y - CubeMapTreeBounds.Min.Y) / CellSize.Y, 0, LUTVolumeResolution.Y - 1);
							const int32 CellZ = FMath::Clamp<int32>((RayHit.Z - CubeMapTreeBounds.Min.Z) / CellSize.Z, 0, LUTVolumeResolution.Z - 1);
							++NumHitVolume[CellX + CellY * LUTVolumeResolution.X + CellZ * LUTVolumeResolution.X * LUTVolumeResolution.Y];
						}
					}
				}
			}

			int32 LayerAddedHits = 0;

			for (int32 CellZ = 0; CellZ < LUTVolumeResolution.Z; ++CellZ)
			{
				for (int32 CellY = 0; CellY < LUTVolumeResolution.Y; ++CellY)
				{
					for (int32 CellX = 0; CellX < LUTVolumeResolution.X; ++CellX)
					{
						const int32 CellIndex = CellX + CellY * LUTVolumeResolution.X + CellZ * LUTVolumeResolution.X * LUTVolumeResolution.Y;
						if (NumHitVolume[CellIndex] > Layers.IndexVolume[CellIndex].NumHits)
						{
							LayerAddedHits += NumHitVolume[CellIndex] - Layers.IndexVolume[CellIndex].NumHits;
						}
					}
				}
			}

			// Add a new layer only if it adds a considerable amount of new surface.
			if (LayerAddedHits / float(HeighfieldSize.X * HeighfieldSize.Y) > MinSurfaceThreshold)
			{
				for (int32 CellZ = 0; CellZ < LUTVolumeResolution.Z; ++CellZ)
				{
					for (int32 CellY = 0; CellY < LUTVolumeResolution.Y; ++CellY)
					{
						for (int32 CellX = 0; CellX < LUTVolumeResolution.X; ++CellX)
						{
							const int32 CellIndex = CellX + CellY * LUTVolumeResolution.X + CellZ * LUTVolumeResolution.X * LUTVolumeResolution.Y;
							if (NumHitVolume[CellIndex] > Layers.IndexVolume[CellIndex].NumHits)
							{
								Layers.IndexVolume[CellIndex].NumHits = NumHitVolume[CellIndex];
								Layers.IndexVolume[CellIndex].LayerIndex = LayerIndex;
							}
						}
					}
				}
			}
		}

		Layers.Bounds.SetNumUninitialized(NumLayers);
		Layers.LayerIndexToFaceIndex.SetNumUninitialized(NumLayers);
		for (int32 LayerIndex = 0; LayerIndex < Layers.Bounds.Num(); ++LayerIndex)
		{
			Layers.Bounds[LayerIndex].Init();
			Layers.LayerIndexToFaceIndex[LayerIndex] = -1;
		}

		for (int32 CellZ = 0; CellZ < LUTVolumeResolution.Z; ++CellZ)
		{
			for (int32 CellY = 0; CellY < LUTVolumeResolution.Y; ++CellY)
			{
				for (int32 CellX = 0; CellX < LUTVolumeResolution.X; ++CellX)
				{
					const int32 CellIndex = CellX + CellY * LUTVolumeResolution.X + CellZ * LUTVolumeResolution.X * LUTVolumeResolution.Y;

					const int32 LayerIndex = Layers.IndexVolume[CellIndex].LayerIndex;

					if (LayerIndex != -1)
					{
						const FVector CellMin = CubeMapTreeBounds.Min + CellSize * FVector(CellX, CellY, CellZ);

						Layers.Bounds[LayerIndex] += CellMin;
						Layers.Bounds[LayerIndex] += CellMin + CellSize;
					}
				}
			}
		}
	}

	OutData.CubeMapTreeBuildData.LookupVolumeData.SetNumZeroed(LUTVolumeResolution.X * LUTVolumeResolution.Y * LUTVolumeResolution.Z);
	
	// Allocate cube map tree faces.
	for (int32 Orientation = 0; Orientation < 6; ++Orientation)
	{
		FLayers& Layers = LayersPerDirection[Orientation];

		for (int32 LayerIndex = 0; LayerIndex < Layers.Bounds.Num(); ++LayerIndex)
		{
			if (Layers.Bounds[LayerIndex].IsValid)
			{
				const int32 FaceIndex = OutData.CubeMapTreeBuildData.FaceBuiltData.Num();
				Layers.LayerIndexToFaceIndex[LayerIndex] = FaceIndex;

				FLumenCubeMapFaceBuildData CubeMapFaceBuildData;

				CubeMapFaceBuildData.Center = Layers.Bounds[LayerIndex].GetCenter();
				CubeMapFaceBuildData.Extent = Layers.Bounds[LayerIndex].GetExtent();
				CubeMapFaceBuildData.Extent = TransformFaceExtent(CubeMapFaceBuildData.Extent, Orientation);
				CubeMapFaceBuildData.Orientation = Orientation;

				OutData.CubeMapTreeBuildData.FaceBuiltData.Add(CubeMapFaceBuildData);
			}
		}
	}

	// Allocate cube maps.
	for (int32 CellZ = 0; CellZ < LUTVolumeResolution.Z; ++CellZ)
	{
		for (int32 CellY = 0; CellY < LUTVolumeResolution.Y; ++CellY)
		{
			for (int32 CellX = 0; CellX < LUTVolumeResolution.X; ++CellX)
			{
				const int32 CellIndex = CellX + CellY * LUTVolumeResolution.X + CellZ * LUTVolumeResolution.X * LUTVolumeResolution.Y;

				FLumenCubeMapBuildData CubeMap;

				for (int32 Orientation = 0; Orientation < 6; ++Orientation)
				{
					FLayers& Layers = LayersPerDirection[Orientation];

					const int32 LayerIndex = Layers.IndexVolume[CellIndex].LayerIndex;

					CubeMap.FaceIndices[Orientation] = -1;

					if (LayerIndex != -1)
					{
						CubeMap.FaceIndices[Orientation] = Layers.LayerIndexToFaceIndex[LayerIndex];
					}
				}

				int32 CubeMapIndex = OutData.CubeMapTreeBuildData.CubeMapBuiltData.Find(CubeMap);

				if (CubeMapIndex == -1)
				{
					CubeMapIndex = OutData.CubeMapTreeBuildData.CubeMapBuiltData.Num();
					OutData.CubeMapTreeBuildData.CubeMapBuiltData.Add(CubeMap);
				}

				OutData.CubeMapTreeBuildData.LookupVolumeData[CellIndex] = CubeMapIndex;
			}
		}
	}
}

#endif // #if USE_EMBREE

bool FMeshUtilities::GenerateCardRepresentationData(
	FString MeshName,
	const FSourceMeshDataForDerivedDataTask& SourceMeshData,
	const FStaticMeshLODResources& LODModel,
	class FQueuedThreadPool& ThreadPool,
	const FBoxSphereBounds& Bounds,
	const FDistanceFieldVolumeData* DistanceFieldVolumeData,
	FCardRepresentationData& OutData)
{
#if USE_EMBREE
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshUtilities::GenerateCardRepresentationData);

	RTCDevice EmbreeDevice = rtcNewDevice(nullptr);

	RTCError ReturnErrorNewDevice = rtcDeviceGetError(EmbreeDevice);
	if (ReturnErrorNewDevice != RTC_NO_ERROR)
	{
		UE_LOG(LogMeshUtilities, Warning, TEXT("GenerateCardRepresentationData failed for %s. Embree rtcNewDevice failed. Code: %d"), *MeshName, (int32)ReturnErrorNewDevice);
		return false;
	}

	const int32 NumIndices = SourceMeshData.IsValid() ? SourceMeshData.Indices.Num() : LODModel.IndexBuffer.GetNumIndices();
	const int32 NumTriangles = NumIndices / 3;

	FInputPrimitiveIds InputPrimitiveIds;
	InputPrimitiveIds.Reserve(NumTriangles);

	for (int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
	{
		InputPrimitiveIds.Add(TriangleIndex);
	}

	RTCScene EmbreeScene = nullptr;

	const bool bEmbreeSetup = SetupEmbreeScene(MeshName, 0, NumTriangles, InputPrimitiveIds, SourceMeshData, LODModel, EmbreeDevice, EmbreeScene);

	if (!bEmbreeSetup)
	{
		return false;
	}

	FGenerateCardMeshContext Context(MeshName, EmbreeScene, EmbreeDevice, OutData);

	BuildCubeMapTree(Bounds.GetBox(), Context, OutData);

	rtcDeleteScene(EmbreeScene);
	rtcDeleteDevice(EmbreeDevice);

	return true;
#else
	UE_LOG(LogMeshUtilities, Warning, TEXT("Platform did not set USE_EMBREE, GenerateCardRepresentationData failed."));
	return false;
#endif
}
