// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshUtilities.h"
#include "MeshUtilitiesPrivate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "RawMesh.h"
#include "StaticMeshResources.h"
#include "DistanceFieldAtlas.h"
#include "MeshRepresentationCommon.h"
#include "Async/ParallelFor.h"

//@todo - implement required vector intrinsics for other implementations
#if PLATFORM_ENABLE_VECTORINTRINSICS

class FMeshDistanceFieldAsyncTask
{
public:
	FMeshDistanceFieldAsyncTask(
		TkDOPTree<const FMeshBuildDataProvider, uint32>* InkDopTree,
		bool bInUseEmbree,
		RTCScene InEmbreeScene,
		const TArray<FVector4>* InSampleDirections,
		FBox InVolumeBounds,
		FIntVector InVolumeDimensions,
		float InVolumeMaxDistance,
		int32 InZIndex,
		TArray<float>* DistanceFieldVolume)
		:
		kDopTree(InkDopTree),
		bUseEmbree(bInUseEmbree),
		EmbreeScene(InEmbreeScene),
		SampleDirections(InSampleDirections),
		VolumeBounds(InVolumeBounds),
		VolumeDimensions(InVolumeDimensions),
		VolumeMaxDistance(InVolumeMaxDistance),
		ZIndex(InZIndex),
		OutDistanceFieldVolume(DistanceFieldVolume),
		bNegativeAtBorder(false)
	{}

	void DoWork();

	bool WasNegativeAtBorder() const
	{
		return bNegativeAtBorder;
	}

private:

	// Readonly inputs
	TkDOPTree<const FMeshBuildDataProvider, uint32>* kDopTree;
	bool bUseEmbree;
	RTCScene EmbreeScene;
	const TArray<FVector4>* SampleDirections;
	FBox VolumeBounds;
	FIntVector VolumeDimensions;
	float VolumeMaxDistance;
	int32 ZIndex;

	// Output
	TArray<float>* OutDistanceFieldVolume;
	bool bNegativeAtBorder;
};

void FMeshDistanceFieldAsyncTask::DoWork()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshDistanceFieldAsyncTask::DoWork);

	FMeshBuildDataProvider kDOPDataProvider(*kDopTree);
	const FVector DistanceFieldVoxelSize(VolumeBounds.GetSize() / FVector(VolumeDimensions.X, VolumeDimensions.Y, VolumeDimensions.Z));
	const float VoxelDiameterSqr = DistanceFieldVoxelSize.SizeSquared();

	for (int32 YIndex = 0; YIndex < VolumeDimensions.Y; YIndex++)
	{
		for (int32 XIndex = 0; XIndex < VolumeDimensions.X; XIndex++)
		{
			const FVector VoxelPosition = FVector(XIndex + .5f, YIndex + .5f, ZIndex + .5f) * DistanceFieldVoxelSize + VolumeBounds.Min;
			const int32 Index = (ZIndex * VolumeDimensions.Y * VolumeDimensions.X + YIndex * VolumeDimensions.X + XIndex);

			float MinDistance = VolumeMaxDistance;
			int32 Hit = 0;
			int32 HitBack = 0;

			for (int32 SampleIndex = 0; SampleIndex < SampleDirections->Num(); SampleIndex++)
			{
				const FVector UnitRayDirection = (*SampleDirections)[SampleIndex];
				const FVector EndPosition = VoxelPosition + UnitRayDirection * VolumeMaxDistance;

				if (FMath::LineBoxIntersection(VolumeBounds, VoxelPosition, EndPosition, UnitRayDirection))
				{
#if USE_EMBREE
					if (bUseEmbree)
					{
						FEmbreeRay EmbreeRay;

						FVector RayDirection = EndPosition - VoxelPosition;
						EmbreeRay.org[0] = VoxelPosition.X;
						EmbreeRay.org[1] = VoxelPosition.Y;
						EmbreeRay.org[2] = VoxelPosition.Z;
						EmbreeRay.dir[0] = RayDirection.X;
						EmbreeRay.dir[1] = RayDirection.Y;
						EmbreeRay.dir[2] = RayDirection.Z;
						EmbreeRay.tnear = 0;
						EmbreeRay.tfar = 1.0f;

						rtcIntersect(EmbreeScene, EmbreeRay);

						if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
						{
							Hit++;

							const FVector HitNormal = EmbreeRay.GetHitNormal();

							if (FVector::DotProduct(UnitRayDirection, HitNormal) > 0 && !EmbreeRay.IsHitTwoSided())
							{
								HitBack++;
							}

							const float CurrentDistance = VolumeMaxDistance * EmbreeRay.tfar;

							if (CurrentDistance < MinDistance)
							{
								MinDistance = CurrentDistance;
							}
						}
					}
					else
#endif
					{
						FkHitResult Result;

						TkDOPLineCollisionCheck<const FMeshBuildDataProvider, uint32> kDOPCheck(
							VoxelPosition,
							EndPosition,
							true,
							kDOPDataProvider,
							&Result);

						bool bHit = kDopTree->LineCheck(kDOPCheck);

						if (bHit)
						{
							Hit++;

							const FVector HitNormal = kDOPCheck.GetHitNormal();

							if (FVector::DotProduct(UnitRayDirection, HitNormal) > 0
								// MaterialIndex on the build triangles was set to 1 if two-sided, or 0 if one-sided
								&& kDOPCheck.Result->Item == 0)
							{
								HitBack++;
							}

							const float CurrentDistance = VolumeMaxDistance * Result.Time;

							if (CurrentDistance < MinDistance)
							{
								MinDistance = CurrentDistance;
							}
						}
					}
				}
			}

			const float UnsignedDistance = MinDistance;

			// Consider this voxel 'inside' an object if more than 40% of the rays hit back faces
			if (Hit > 0 && HitBack > SampleDirections->Num() * .4f)
			{
				MinDistance *= -1;
			}

			// If we are very close to a surface and nearly all of our rays hit backfaces, treat as inside
			// This is important for one sided planes
			if (FMath::Square(UnsignedDistance) < VoxelDiameterSqr && HitBack > .95f * Hit)
			{
				MinDistance = -UnsignedDistance;
			}

			MinDistance = FMath::Min(MinDistance, VolumeMaxDistance);
			const float VolumeSpaceDistance = MinDistance / VolumeBounds.GetExtent().GetMax();

			if (MinDistance < 0 &&
				(XIndex == 0 || XIndex == VolumeDimensions.X - 1 ||
				YIndex == 0 || YIndex == VolumeDimensions.Y - 1 ||
				ZIndex == 0 || ZIndex == VolumeDimensions.Z - 1))
			{
				bNegativeAtBorder = true;
			}

			(*OutDistanceFieldVolume)[Index] = VolumeSpaceDistance;
		}
	}
}

void FMeshUtilities::GenerateSignedDistanceFieldVolumeData(
	FString MeshName,
	const FSourceMeshDataForDerivedDataTask& SourceMeshData,
	const FStaticMeshLODResources& LODModel,
	class FQueuedThreadPool& ThreadPool,
	const TArray<FSignedDistanceFieldBuildMaterialData>& MaterialBlendModes,
	const FBoxSphereBounds& Bounds,
	float DistanceFieldResolutionScale,
	bool bGenerateAsIfTwoSided,
	FDistanceFieldVolumeData& OutData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshUtilities::GenerateSignedDistanceFieldVolumeData);

	if (DistanceFieldResolutionScale > 0)
	{
		const double StartTime = FPlatformTime::Seconds();

		FEmbreeScene EmbreeScene;
		MeshRepresentation::SetupEmbreeScene(MeshName,
			SourceMeshData,
			LODModel,
			MaterialBlendModes,
			bGenerateAsIfTwoSided,
			EmbreeScene);

		//@todo - project setting
		const int32 NumVoxelDistanceSamples = 1200;
		TArray<FVector4> SampleDirections;
		FRandomStream RandomStream(0);
		MeshUtilities::GenerateStratifiedUniformHemisphereSamples(NumVoxelDistanceSamples, RandomStream, SampleDirections);
		TArray<FVector4> OtherHemisphereSamples;
		MeshUtilities::GenerateStratifiedUniformHemisphereSamples(NumVoxelDistanceSamples, RandomStream, OtherHemisphereSamples);

		for (int32 i = 0; i < OtherHemisphereSamples.Num(); i++)
		{
			FVector4 Sample = OtherHemisphereSamples[i];
			Sample.Z *= -1;
			SampleDirections.Add(Sample);
		}

		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.MaxPerMeshResolution"));
		const int32 PerMeshMax = CVar->GetValueOnAnyThread();

		// Meshes with explicit artist-specified scale can go higher
		const int32 MaxNumVoxelsOneDim = DistanceFieldResolutionScale <= 1 ? PerMeshMax / 2 : PerMeshMax;
		const int32 MinNumVoxelsOneDim = 8;

		static const auto CVarDensity = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.DistanceFields.DefaultVoxelDensity"));
		const float VoxelDensity = CVarDensity->GetValueOnAnyThread();

		const float NumVoxelsPerLocalSpaceUnit = VoxelDensity * DistanceFieldResolutionScale;
		FBox MeshBounds(Bounds.GetBox());

		// Make sure BBox isn't empty and we can generate an SDF for it. This handles e.g. infinitely thin planes.
		FVector MeshBoundsCenter = MeshBounds.GetCenter();
		FVector MeshBoundsExtent = FVector::Max(MeshBounds.GetExtent(), FVector(1.0f, 1.0f, 1.0f));
		MeshBounds.Min = MeshBoundsCenter - MeshBoundsExtent;
		MeshBounds.Max = MeshBoundsCenter + MeshBoundsExtent;

		static const auto CVarEightBit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.EightBit"));

		const bool bEightBitFixedPoint = CVarEightBit->GetValueOnAnyThread() != 0;
		const int32 FormatSize = GPixelFormats[bEightBitFixedPoint ? PF_G8 : PF_R16F].BlockBytes;

		{
			FVector DesiredDimensions = FVector(MeshBounds.GetSize()* FVector(NumVoxelsPerLocalSpaceUnit));
			FIntVector VolumeDimensions = FIntVector(
				FMath::Clamp(FMath::TruncToInt(DesiredDimensions.X) + 2 * DistanceField::MeshDistanceFieldBorder, MinNumVoxelsOneDim, MaxNumVoxelsOneDim),
				FMath::Clamp(FMath::TruncToInt(DesiredDimensions.Y) + 2 * DistanceField::MeshDistanceFieldBorder, MinNumVoxelsOneDim, MaxNumVoxelsOneDim),
				FMath::Clamp(FMath::TruncToInt(DesiredDimensions.Z) + 2 * DistanceField::MeshDistanceFieldBorder, MinNumVoxelsOneDim, MaxNumVoxelsOneDim));

			// Expand to guarantee one voxel border for gradient reconstruction using bilinear filtering
			const FVector TexelObjectSpaceSize = MeshBounds.GetSize() / FVector(VolumeDimensions - FIntVector(2 * DistanceField::MeshDistanceFieldBorder));
			const FBox DistanceFieldVolumeBounds = MeshBounds.ExpandBy(DistanceField::MeshDistanceFieldBorder * TexelObjectSpaceSize);
			const float DistanceFieldVolumeMaxDistance = DistanceFieldVolumeBounds.GetExtent().Size();

			TArray<float> DistanceFieldVolume;
			DistanceFieldVolume.Empty(VolumeDimensions.X * VolumeDimensions.Y * VolumeDimensions.Z);
			DistanceFieldVolume.AddZeroed(VolumeDimensions.X * VolumeDimensions.Y * VolumeDimensions.Z);

			TArray<FMeshDistanceFieldAsyncTask> AsyncTasks;
			AsyncTasks.Reserve(VolumeDimensions.Z);

			for (int32 ZIndex = 0; ZIndex < VolumeDimensions.Z; ZIndex++)
			{
				AsyncTasks.Emplace(
					&EmbreeScene.kDopTree,
					EmbreeScene.bUseEmbree,
					EmbreeScene.EmbreeScene,
					&SampleDirections,
					DistanceFieldVolumeBounds,
					VolumeDimensions,
					DistanceFieldVolumeMaxDistance,
					ZIndex,
					&DistanceFieldVolume);
			}

			bool bNegativeAtBorder = false;

			ParallelForTemplate(AsyncTasks.Num(), [&AsyncTasks](int32 TaskIndex)
			{
				AsyncTasks[TaskIndex].DoWork();
			}, EParallelForFlags::BackgroundPriority | EParallelForFlags::Unbalanced);

			for (int32 TaskIndex = 0; TaskIndex < AsyncTasks.Num(); TaskIndex++)
			{
				bNegativeAtBorder = bNegativeAtBorder || AsyncTasks[TaskIndex].WasNegativeAtBorder();
			}

			if (bNegativeAtBorder)
			{
				// Mesh distance fields which have negative values at the boundaries (unclosed meshes) are edited to have a virtual surface just behind the real surface, effectively closing them.

				bNegativeAtBorder = false;
				const float MinInteriorDistance = -.1f;

				for (int32 Index = 0; Index < DistanceFieldVolume.Num(); Index++)
				{
					const float OriginalVolumeSpaceDistance = DistanceFieldVolume[Index];
					float NewVolumeSpaceDistance = OriginalVolumeSpaceDistance;

					if (OriginalVolumeSpaceDistance < MinInteriorDistance)
					{
						NewVolumeSpaceDistance = MinInteriorDistance - OriginalVolumeSpaceDistance;
						DistanceFieldVolume[Index] = NewVolumeSpaceDistance;
					}

					const int32 XIndex = Index % VolumeDimensions.X;
					const int32 ZIndex = Index / (VolumeDimensions.Y * VolumeDimensions.X);
					const int32 YIndex = (Index - ZIndex * VolumeDimensions.Y * VolumeDimensions.X) / VolumeDimensions.X;

					if (NewVolumeSpaceDistance < 0 &&
						(XIndex == 0 || XIndex == VolumeDimensions.X - 1 ||
						YIndex == 0 || YIndex == VolumeDimensions.Y - 1 ||
						ZIndex == 0 || ZIndex == VolumeDimensions.Z - 1))
					{
						bNegativeAtBorder = true;
					}
				}
			}

			if (bNegativeAtBorder)
			{
				UE_LOG(LogMeshUtilities, Log, TEXT("Distance field for %s mesh has a negative border."), *MeshName);
			}

			TArray<uint8> QuantizedDistanceFieldVolume;
			QuantizedDistanceFieldVolume.Empty(VolumeDimensions.X * VolumeDimensions.Y * VolumeDimensions.Z * FormatSize);
			QuantizedDistanceFieldVolume.AddZeroed(VolumeDimensions.X * VolumeDimensions.Y * VolumeDimensions.Z * FormatSize);

			float MinVolumeDistance = 1.0f;
			float MaxVolumeDistance = -1.0f;

			for (int32 Index = 0; Index < DistanceFieldVolume.Num(); Index++)
			{
				const float VolumeSpaceDistance = DistanceFieldVolume[Index];
				MinVolumeDistance = FMath::Min(MinVolumeDistance, VolumeSpaceDistance);
				MaxVolumeDistance = FMath::Max(MaxVolumeDistance, VolumeSpaceDistance);
			}

			MinVolumeDistance = FMath::Max(MinVolumeDistance, -1.0f);
			MaxVolumeDistance = FMath::Min(MaxVolumeDistance, 1.0f);

			const float InvDistanceRange = 1.0f / (MaxVolumeDistance - MinVolumeDistance);

			for (int32 Index = 0; Index < DistanceFieldVolume.Num(); Index++)
			{
				const float VolumeSpaceDistance = DistanceFieldVolume[Index];

				if (bEightBitFixedPoint)
				{
					check(FormatSize == sizeof(uint8));
					// [MinVolumeDistance, MaxVolumeDistance] -> [0, 1]
					const float RescaledDistance = (VolumeSpaceDistance - MinVolumeDistance) * InvDistanceRange;
					// Encoding based on D3D format conversion rules for float -> UNORM
					const int32 QuantizedDistance = FMath::FloorToInt(RescaledDistance * 255.0f + .5f);
					QuantizedDistanceFieldVolume[Index * FormatSize] = (uint8)FMath::Clamp<int32>(QuantizedDistance, 0, 255);
				}
				else
				{
					check(FormatSize == sizeof(FFloat16));
					FFloat16* OutputPointer = (FFloat16*)&(QuantizedDistanceFieldVolume[Index * FormatSize]);
					*OutputPointer = FFloat16(VolumeSpaceDistance);
				}
			}

			DistanceFieldVolume.Empty();

			OutData.bBuiltAsIfTwoSided = bGenerateAsIfTwoSided;
			OutData.Size = VolumeDimensions;
			OutData.LocalBoundingBox = MeshBounds;
			OutData.DistanceMinMax = FVector2D(MinVolumeDistance, MaxVolumeDistance);

			if (QuantizedDistanceFieldVolume.Num() > 0)
			{
				static const auto CVarCompress = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.Compress"));
				const bool bCompress = CVarCompress->GetValueOnAnyThread() != 0;

				if (bCompress)
				{
					const int32 UncompressedSize = QuantizedDistanceFieldVolume.Num() * QuantizedDistanceFieldVolume.GetTypeSize();
					TArray<uint8> TempCompressedMemory;
					// Compressed can be slightly larger than uncompressed
					TempCompressedMemory.Empty(UncompressedSize * 4 / 3);
					TempCompressedMemory.AddUninitialized(UncompressedSize * 4 / 3);
					int32 CompressedSize = TempCompressedMemory.Num() * TempCompressedMemory.GetTypeSize();

					verify(FCompression::CompressMemory(
						NAME_LZ4, 
						TempCompressedMemory.GetData(), 
						CompressedSize, 
						QuantizedDistanceFieldVolume.GetData(), 
						UncompressedSize,
						COMPRESS_BiasMemory));

					OutData.CompressedDistanceFieldVolume.Empty(CompressedSize);
					OutData.CompressedDistanceFieldVolume.AddUninitialized(CompressedSize);

					FPlatformMemory::Memcpy(OutData.CompressedDistanceFieldVolume.GetData(), TempCompressedMemory.GetData(), CompressedSize);
				}
				else
				{
					int32 CompressedSize = QuantizedDistanceFieldVolume.Num() * QuantizedDistanceFieldVolume.GetTypeSize();

					OutData.CompressedDistanceFieldVolume.Empty(CompressedSize);
					OutData.CompressedDistanceFieldVolume.AddUninitialized(CompressedSize);

					FPlatformMemory::Memcpy(OutData.CompressedDistanceFieldVolume.GetData(), QuantizedDistanceFieldVolume.GetData(), CompressedSize);
				}
			}
			else
			{
				OutData.CompressedDistanceFieldVolume.Empty();
			}

			UE_LOG(LogMeshUtilities, Log, TEXT("Finished distance field build in %.1fs - %ux%ux%u distance field, %u triangles, Range [%.1f, %.1f], %s"),
				(float)(FPlatformTime::Seconds() - StartTime),
				VolumeDimensions.X,
				VolumeDimensions.Y,
				VolumeDimensions.Z,
				EmbreeScene.NumIndices / 3,
				MinVolumeDistance,
				MaxVolumeDistance,
				*MeshName);
		}

		MeshRepresentation::DeleteEmbreeScene(EmbreeScene);
	}
}

#else

void FMeshUtilities::GenerateSignedDistanceFieldVolumeData(
	FString MeshName,
	const FSourceMeshDataForDerivedDataTask& SourceMeshData,
	const FStaticMeshLODResources& LODModel,
	class FQueuedThreadPool& ThreadPool,
	const TArray<FSignedDistanceFieldBuildMaterialData>& MaterialBlendModes,
	const FBoxSphereBounds& Bounds,
	float DistanceFieldResolutionScale,
	bool bGenerateAsIfTwoSided,
	FDistanceFieldVolumeData& OutData)
{
	if (DistanceFieldResolutionScale > 0)
	{
		UE_LOG(LogMeshUtilities, Error, TEXT("Couldn't generate distance field for mesh, platform is missing required Vector intrinsics."));
	}
}

#endif // PLATFORM_ENABLE_VECTORINTRINSICS

void FMeshUtilities::DownSampleDistanceFieldVolumeData(FDistanceFieldVolumeData& DistanceFieldData, float Divider)
{
	static const auto CVarCompress = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.Compress"));
	const bool bDataIsCompressed = CVarCompress->GetValueOnAnyThread() != 0;

	static const auto CVarEightBit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.EightBit"));
	const bool bDataIsEightBit = CVarEightBit->GetValueOnAnyThread() != 0;

	if (!bDataIsEightBit)
	{
		return;
	}

	TArray<uint8> UncompressedData;
	const TArray<uint8>* SourceData = &DistanceFieldData.CompressedDistanceFieldVolume;

	if (DistanceFieldData.Size.X <= 4 || DistanceFieldData.Size.Y <= 4 || DistanceFieldData.Size.Z <= 4)
	{
		return;
	}

	int32 UncompressedSize = DistanceFieldData.Size.X * DistanceFieldData.Size.Y * DistanceFieldData.Size.Z;

	if (bDataIsCompressed)
	{
		UncompressedData.AddUninitialized(UncompressedSize);
		verify(FCompression::UncompressMemory(NAME_LZ4, UncompressedData.GetData(), UncompressedSize, DistanceFieldData.CompressedDistanceFieldVolume.GetData(), DistanceFieldData.CompressedDistanceFieldVolume.Num()));
		SourceData = &UncompressedData;
	}

	const FIntVector SrcSize = DistanceFieldData.Size;

	FVector DstSizeFloat = FVector(SrcSize);
	DstSizeFloat /= Divider;
	
	FIntVector DstSize;
	DstSize.X = FMath::TruncToInt(DstSizeFloat.X);
	DstSize.Y = FMath::TruncToInt(DstSizeFloat.Y);
	DstSize.Z = FMath::TruncToInt(DstSizeFloat.Z);

	FIntVector IntVectorOne = FIntVector(1);
	FIntVector SrcSizeMinusOne = SrcSize - IntVectorOne;
	FIntVector DstSizeMinusOne = DstSize - IntVectorOne;
	
	FVector Divider3 = FVector(SrcSizeMinusOne) / FVector(DstSizeMinusOne);

	TArray<uint8> DownSampledTexture;
	DownSampledTexture.SetNum(DstSize.X * DstSize.Y * DstSize.Z);

	int32 SrcPitchX = SrcSize.X;
	int32 SrcPitchY = SrcSize.X * SrcSize.Y;
	int32 DstPitchX = DstSize.X;
	int32 DstPitchY = DstSize.X * DstSize.Y;

	for (int32 DstPosZ = 0; DstPosZ < DstSize.Z; ++DstPosZ)
	{
		int32 SrcPosZ0 = FMath::TruncToInt((float(DstPosZ) + 0.0f) * Divider3.Z);
		int32 SrcPosZ1 = FMath::TruncToInt((float(DstPosZ) + 1.0f) * Divider3.Z);
		SrcPosZ1 = FMath::Min(SrcPosZ1, SrcSizeMinusOne.Z);

		int32 SrcOffsetZ0 = SrcPosZ0 * SrcPitchY;
		int32 SrcOffsetZ1 = SrcPosZ1 * SrcPitchY;
		
		int32 DstOffsetZ = DstPosZ * DstPitchY;

		for (int32 DstPosY = 0; DstPosY < DstSize.Y; ++DstPosY)
		{
			int32 SrcPosY0 = FMath::TruncToInt(float(DstPosY) + 0.0f) * Divider3.Y;
			int32 SrcPosY1 = FMath::TruncToInt(float(DstPosY) + 1.0f) * Divider3.Y;
			SrcPosY1 = FMath::Min(SrcPosY1, SrcSizeMinusOne.Y);

			int32 SrcOffsetZ0Y0 = SrcPosY0 * SrcPitchX + SrcOffsetZ0;
			int32 SrcOffsetZ0Y1 = SrcPosY1 * SrcPitchX + SrcOffsetZ0;
			int32 SrcOffsetZ1Y0 = SrcPosY0 * SrcPitchX + SrcOffsetZ1;
			int32 SrcOffsetZ1Y1 = SrcPosY1 * SrcPitchX + SrcOffsetZ1;

			int32 DstOffsetZY = DstPosY * DstPitchX + DstOffsetZ;

			for (int32 DstPosX = 0; DstPosX < DstSize.X; ++DstPosX)
			{
				int32 SrcPosX0 = FMath::TruncToInt(float(DstPosX) + 0.0f) * Divider3.X;
				int32 SrcPosX1 = FMath::TruncToInt(float(DstPosX) + 1.0f) * Divider3.X;
				SrcPosX1 = FMath::Min(SrcPosX1, SrcSizeMinusOne.X - 1);

				uint32 a = uint32((*SourceData)[SrcPosX0 + SrcOffsetZ0Y0]);
				uint32 b = uint32((*SourceData)[SrcPosX1 + SrcOffsetZ0Y0]);
				uint32 c = uint32((*SourceData)[SrcPosX0 + SrcOffsetZ0Y1]);
				uint32 d = uint32((*SourceData)[SrcPosX1 + SrcOffsetZ0Y1]);

				uint32 e = uint32((*SourceData)[SrcPosX0 + SrcOffsetZ1Y0]);
				uint32 f = uint32((*SourceData)[SrcPosX1 + SrcOffsetZ1Y0]);
				uint32 g = uint32((*SourceData)[SrcPosX0 + SrcOffsetZ1Y1]);
				uint32 h = uint32((*SourceData)[SrcPosX1 + SrcOffsetZ1Y1]);


				a = a + b + c + d + e + f + g + h;
				a /= 8;

				DownSampledTexture[DstOffsetZY + DstPosX] = uint8(a);
			}
		}
	}

	DistanceFieldData.Size = DstSize;
	UncompressedSize = DownSampledTexture.Num();

	if (bDataIsCompressed)
	{
		TArray<uint8> TempCompressedMemory;
		// Compressed can be slightly larger than uncompressed
		TempCompressedMemory.Empty(UncompressedSize * 4 / 3);
		TempCompressedMemory.AddUninitialized(UncompressedSize * 4 / 3);
		int32 CompressedSize = TempCompressedMemory.Num() * TempCompressedMemory.GetTypeSize();

		verify(FCompression::CompressMemory(
			NAME_LZ4,
			TempCompressedMemory.GetData(),
			CompressedSize,
			DownSampledTexture.GetData(),
			UncompressedSize,
			COMPRESS_BiasMemory));

		DistanceFieldData.CompressedDistanceFieldVolume.Empty(CompressedSize);
		DistanceFieldData.CompressedDistanceFieldVolume.AddUninitialized(CompressedSize);

		FPlatformMemory::Memcpy(DistanceFieldData.CompressedDistanceFieldVolume.GetData(), TempCompressedMemory.GetData(), CompressedSize);
	}
	else
	{
		DistanceFieldData.CompressedDistanceFieldVolume.Empty(UncompressedSize);
		DistanceFieldData.CompressedDistanceFieldVolume.AddUninitialized(UncompressedSize);
		FPlatformMemory::Memcpy(DistanceFieldData.CompressedDistanceFieldVolume.GetData(), DownSampledTexture.GetData(), UncompressedSize);
	}
}
