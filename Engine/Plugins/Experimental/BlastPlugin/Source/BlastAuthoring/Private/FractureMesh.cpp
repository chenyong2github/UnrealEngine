// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureMesh.h"
#include "Editor.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/Material.h"
#include "DrawDebugHelpers.h"
#include "MeshUtility.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Serialization/BulkData.h"

#if PLATFORM_WINDOWS
#include "PxPhysicsAPI.h"
#include "NvBlast.h"
#include "NvBlastAssert.h"
#include "NvBlastGlobals.h"
#include "NvBlastExtAuthoring.h"
#include "NvBlastExtAuthoringMesh.h"
#include "NvBlastExtAuthoringCutout.h"
#include "NvBlastExtAuthoringFractureTool.h"
#endif

#define LOCTEXT_NAMESPACE "FractureMesh"

#define FVEC_TO_PHYSX(x) (physx::PxVec3(x.X, x.Y, x.Z))
#define PHYSX_TO_FVEC(x) (FVector(x.x, x.y, x.z))

DEFINE_LOG_CATEGORY(LogFractureMesh);

#if PLATFORM_WINDOWS
using namespace Nv::Blast;
using namespace physx;
#endif

namespace FractureMesh
{
	static TAutoConsoleVariable<int32> CVarEnableBlastDebugVisualization(TEXT("physics.Destruction.BlastDebugVisualization"), 0, TEXT("If enabled, the blast fracture output will be rendered using debug rendering. Note: this must be enabled BEFORE fracturing."));
}

static UWorld* FindEditorWorld()
{
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor)
			{
				return Context.World();
			}
		}
	}

	return nullptr;
}

bool UFractureMesh::FractureMesh(const UEditableMesh* SourceMesh, const FString& ParentName, const UMeshFractureSettings& FractureSettings, int32 FracturedChunkIndex, const FTransform& Transform, int RandomSeed, UGeometryCollection* FracturedGeometryCollection, TArray<FGeneratedFracturedChunk>& GeneratedChunksOut, TArray<int32>& DeletedChunksOut, FBox Bounds, const FVector& InBoundsOffset)
{
	bool bAllChunksGood = false;

	FVector FBoundsCenter(Bounds.GetCenter());
	FTransform ChunkTransform(Transform);

	bool bUseReferenceActor = false;
	FTransform ReferenceTransform;
	if (FractureSettings.CommonSettings->ReferenceActor != nullptr)
	{
		bUseReferenceActor = true;
		Bounds = FractureSettings.CommonSettings->ReferenceActor->CalculateComponentsBoundingBoxInLocalSpace();
		ReferenceTransform = FractureSettings.CommonSettings->ReferenceActor->GetActorTransform();
		FBoundsCenter = ReferenceTransform.GetTranslation();
	}

#if PLATFORM_WINDOWS
	const double CacheStartTime = FPlatformTime::Seconds();
	check(FractureSettings.CommonSettings);

	FractureRandomGenerator RandomGenerator(RandomSeed);
	Nv::Blast::FractureTool* BlastFractureTool = NvBlastExtAuthoringCreateFractureTool();

	check(BlastFractureTool);

	// convert mesh and assign to fracture tool
	Nv::Blast::Mesh* NewBlastMesh = nullptr;

	if (FracturedChunkIndex == -1)
	{
		FMeshUtility::EditableMeshToBlastMesh(SourceMesh, NewBlastMesh);
	}
	else
	{
		FMeshUtility::EditableMeshToBlastMesh(SourceMesh, FracturedChunkIndex, NewBlastMesh);

		UGeometryCollection* GeometryCollectionObject = Cast<UGeometryCollection>(static_cast<UObject*>(SourceMesh->GetSubMeshAddress().MeshObjectPtr));

		if (GeometryCollectionObject)
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
			FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();

			if (GeometryCollection)
			{
				TArray<FTransform> Transforms;
				GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transforms);
				ChunkTransform = Transforms[FracturedChunkIndex];
			}
		}
	}

	if (NewBlastMesh)
	{
		BlastFractureTool->setSourceMesh(NewBlastMesh);
		BlastFractureTool->setRemoveIslands(FractureSettings.CommonSettings->RemoveIslands);

		// init Voronoi Site Generator if required
		Nv::Blast::VoronoiSitesGenerator* SiteGenerator = nullptr;
		if (FractureSettings.CommonSettings->FractureMode <= EMeshFractureMode::Radial)
		{
			SiteGenerator = NvBlastExtAuthoringCreateVoronoiSitesGenerator(NewBlastMesh, &RandomGenerator);
		}

		FRandomStream RandomStream(RandomSeed);

		// Where Voronoi sites are required
		switch (FractureSettings.CommonSettings->FractureMode)
		{
		case EMeshFractureMode::Uniform:
			check(FractureSettings.UniformSettings);
			{
				uint32 SitesCount = RandomStream.RandRange(FractureSettings.UniformSettings->NumberVoronoiSitesMin, FractureSettings.UniformSettings->NumberVoronoiSitesMax);

				TArray<FVector> Sites;
				GenerateUniformSites(RandomStream, FVector::ZeroVector, Bounds, SitesCount, Sites);

				if (bUseReferenceActor)
				{
					FVector ReferenceScale(ReferenceTransform.GetScale3D());
					FQuat ReferenceRotation(ReferenceTransform.GetRotation());

					for (auto& Site : Sites)
					{
						Site *= ReferenceScale;
						Site = ReferenceRotation.RotateVector(Site);
						Site += ReferenceTransform.GetLocation();
						Site = Transform.InverseTransformPosition(Site);
						Site -= FBoundsCenter;
					}
				}

				for (const FVector& Site : Sites)
				{
					FVector TransformedPosition(ChunkTransform.InverseTransformPosition(Site + FBoundsCenter));
					const physx::PxVec3 Position(FVEC_TO_PHYSX(TransformedPosition));
					SiteGenerator->addSite(Position);
				}

			}
			break;
		case EMeshFractureMode::Clustered:
			check(FractureSettings.ClusterSettings);
			{
				check(FractureSettings.ClusterSettings);

				// we're going to do some maths to get a reasonable radius based on the bbox dimension.
				FVector Extents = Bounds.GetSize();
				float AxisClusterSum = (Extents.X + Extents.Y + Extents.Z) / 3.0f;
				float ClusterRadius = (RandomStream.FRandRange(FractureSettings.ClusterSettings->ClusterRadiusPercentageMin, FractureSettings.ClusterSettings->ClusterRadiusPercentageMax)  * AxisClusterSum) + FractureSettings.ClusterSettings->ClusterRadius;

				uint32 NumberOfClusters = RandomStream.RandRange(FractureSettings.ClusterSettings->NumberClustersMin, FractureSettings.ClusterSettings->NumberClustersMax);

				TArray<FVector> Sites;
				GenerateUniformSites(RandomStream, FVector::ZeroVector, Bounds, NumberOfClusters, Sites);

				FRandomStream RandomStreamSphere(RandomStream.GetUnsignedInt());

				for (uint32 ii = 0, ni = Sites.Num(); ii < ni; ++ii)
				{
					int32 SitesPerClusters = RandomStream.RandRange(FractureSettings.ClusterSettings->SitesPerClusterMin, FractureSettings.ClusterSettings->SitesPerClusterMax);
					GenerateSitesInSphere(RandomStreamSphere, Sites[ii], ClusterRadius, SitesPerClusters, Sites);
				}

				if (bUseReferenceActor)
				{
					FVector ReferenceScale(ReferenceTransform.GetScale3D());
					FQuat ReferenceRotation(ReferenceTransform.GetRotation());

					for (auto& Site : Sites)
					{
						Site *= ReferenceScale;
						Site = ReferenceRotation.RotateVector(Site);
						Site += ReferenceTransform.GetLocation();
						Site = Transform.InverseTransformPosition(Site);
						Site -= FBoundsCenter;
					}
				}

				for (const FVector& Site : Sites)
				{
					FVector TransformedPosition(ChunkTransform.InverseTransformPosition(Site + FBoundsCenter));
					const physx::PxVec3 Position(FVEC_TO_PHYSX(TransformedPosition));
					SiteGenerator->addSite(Position);
				}
			}
			break;

		case EMeshFractureMode::Radial:
			check(FractureSettings.RadialSettings);
			PxVec3 Center(FractureSettings.RadialSettings->Center.X, FractureSettings.RadialSettings->Center.Y, FractureSettings.RadialSettings->Center.Z);
			PxVec3 Normal(FractureSettings.RadialSettings->Normal.X, FractureSettings.RadialSettings->Normal.Y, FractureSettings.RadialSettings->Normal.Z);
			SiteGenerator->radialPattern(Center, Normal, FractureSettings.RadialSettings->Radius, FractureSettings.RadialSettings->AngularSteps, FractureSettings.RadialSettings->RadialSteps, FractureSettings.RadialSettings->AngleOffset, FractureSettings.RadialSettings->Variability);
			break;
		}

		const physx::PxVec3* VononoiSites = nullptr;
		uint32 SitesCount = 0;
		int32 ReturnCode = 0;
		bool ReplaceChunk = false;
		int ChunkID = 0;

		switch (FractureSettings.CommonSettings->FractureMode)
		{
			// Voronoi
		case EMeshFractureMode::Uniform:
		case EMeshFractureMode::Clustered:
		case EMeshFractureMode::Radial:
		{
			SitesCount = SiteGenerator->getVoronoiSites(VononoiSites);
			ReturnCode = BlastFractureTool->voronoiFracturing(ChunkID, SitesCount, VononoiSites, ReplaceChunk);
			if (ReturnCode != 0)
			{
				UE_LOG(LogFractureMesh, Error, TEXT("Mesh Slicing failed ReturnCode=%d"), ReturnCode);
			}
		}
		break;
		// Slicing
		case EMeshFractureMode::Slicing:
		{
			check(FractureSettings.SlicingSettings);

			Nv::Blast::SlicingConfiguration SlicingConfiguration;
			SlicingConfiguration.x_slices = FractureSettings.SlicingSettings->SlicesX;
			SlicingConfiguration.y_slices = FractureSettings.SlicingSettings->SlicesY;
			SlicingConfiguration.z_slices = FractureSettings.SlicingSettings->SlicesZ;
			SlicingConfiguration.angle_variations = FractureSettings.SlicingSettings->SliceAngleVariation;
			SlicingConfiguration.offset_variations = FractureSettings.SlicingSettings->SliceOffsetVariation;

			SlicingConfiguration.noise.amplitude = FractureSettings.PlaneCutSettings->Amplitude;
			SlicingConfiguration.noise.frequency = FractureSettings.PlaneCutSettings->Frequency;
			SlicingConfiguration.noise.octaveNumber = FractureSettings.PlaneCutSettings->OctaveNumber;
			SlicingConfiguration.noise.surfaceResolution = FractureSettings.PlaneCutSettings->SurfaceResolution;

			ReturnCode = BlastFractureTool->slicing(ChunkID, SlicingConfiguration, ReplaceChunk, &RandomGenerator);
			if (ReturnCode != 0)
			{
				UE_LOG(LogFractureMesh, Error, TEXT("Mesh Slicing failed ReturnCode=%d"), ReturnCode);
			}
		}
		break;

		// Plane Cut
		case EMeshFractureMode::PlaneCut:
		{
			check(FractureSettings.PlaneCutSettings);

			TArray<int32> ChunkIDs;
			ChunkIDs.Push(ChunkID);
			int CutNumber = 0;

			Nv::Blast::NoiseConfiguration Noise;
			Noise.amplitude = FractureSettings.PlaneCutSettings->Amplitude;
			Noise.frequency = FractureSettings.PlaneCutSettings->Frequency;
			Noise.octaveNumber = FractureSettings.PlaneCutSettings->OctaveNumber;
			Noise.surfaceResolution = FractureSettings.PlaneCutSettings->SurfaceResolution;

			TArray<FVector> Positions;
			TArray<FVector> Normals;

			if (bUseReferenceActor)
			{
				FVector Site(ReferenceTransform.GetLocation());
				Site = Transform.InverseTransformPosition(Site);
				Site -= FBoundsCenter;

				Positions.Add(Site);
				Normals.Add(Transform.InverseTransformVector(ReferenceTransform.GetRotation().GetUpVector()));
			}
			else
			{
				ScatterInBounds(RandomStream, Bounds, FractureSettings.PlaneCutSettings->NumberOfCuts, Positions, Normals);
			}

			for (int32 ii = 0, ni = Positions.Num(); ii < ni; ++ii)
			{
				for (int32 CID : ChunkIDs)
				{
					if (RandomStream.GetFraction() <= FractureSettings.PlaneCutSettings->CutChunkChance)
					{
						FVector TransformedNormal(ChunkTransform.Inverse().GetRotation().RotateVector(Normals[ii]));
						const physx::PxVec3 Normal(FVEC_TO_PHYSX(TransformedNormal));
						FVector TransformedPosition(ChunkTransform.InverseTransformPosition(Positions[ii] + FBoundsCenter));
						const physx::PxVec3 Position(FVEC_TO_PHYSX(TransformedPosition));
						BlastFractureTool->cut(CID, Normal, Position, Noise, CutNumber != 0, &RandomGenerator);
					}
				}

				int32 NumChunks = static_cast<int32>(BlastFractureTool->getChunkCount());
				if (NumChunks > 2)
				{
					CutNumber++;
					ChunkIDs.Empty();

					// All generated chunks are candidates for any further cuts (however we must exclude the initial chunk from now on)
					for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ChunkIndex++)
					{
						int32 NewChunkID = BlastFractureTool->getChunkId(ChunkIndex);

						// don't try and fracture the initial chunk again
						if (NewChunkID != ChunkID)
						{
							ChunkIDs.Push(NewChunkID);
						}
					}
				}
			}

			// ReturnCode zero is a success, if we end with one chunk then the chunk we started with hasn't been split
			ReturnCode = !(BlastFractureTool->getChunkCount() > 2);
		}
		break;

		// Bitmap Cutout
		case EMeshFractureMode::Cutout:
		{
			if (FractureSettings.CutoutSettings->CutoutTexture.IsValid())
			{
				check(FractureSettings.CutoutSettings);

				CutoutConfiguration CutoutConfig;

				TArray<uint8> RawData;
				int32 Width = 0;
				int32 Height = 0;
				ExtractDataFromTexture(FractureSettings.CutoutSettings->CutoutTexture, RawData, Width, Height);

				CutoutConfiguration cutoutConfig;
				{
					cutoutConfig.scale.x = FractureSettings.CutoutSettings->Scale.X;
					cutoutConfig.scale.y = FractureSettings.CutoutSettings->Scale.Y;
#if 1
					physx::PxVec3 axis = PxVec3(0.f, 0.f, 1.f);
					//					physx::PxVec3 axis = FractureSettings.CutoutSettings->;
					if (axis.isZero())
					{
						axis = PxVec3(0.f, 0.f, 1.f);
					}
					axis.normalize();
					float d = axis.dot(physx::PxVec3(0.f, 0.f, 1.f));
					if (d < (1e-6f - 1.0f))
					{
						cutoutConfig.transform.q = physx::PxQuat(physx::PxPi, PxVec3(1.f, 0.f, 0.f));
					}
					else if (d < 1.f)
					{
						float s = physx::PxSqrt((1 + d) * 2);
						float invs = 1 / s;
						auto c = axis.cross(PxVec3(0.f, 0.f, 1.f));
						cutoutConfig.transform.q = physx::PxQuat(c.x * invs, c.y * invs, c.z * invs, s * 0.5f);
						cutoutConfig.transform.q.normalize();
					}
					cutoutConfig.transform.p = PxVec3(0, 0, 0);// = point.getValue();
#endif

				}

				CutoutConfig.cutoutSet = NvBlastExtAuthoringCreateCutoutSet();
				//				CutoutConfig.scale = PxVec2(FractureSettings.CutoutSettings->Scale.X, FractureSettings.CutoutSettings->Scale.Y);
				//				CutoutConfig.transform = U2PTransform(FractureSettings.CutoutSettings->Transform);
				FQuat Rot = FractureSettings.CutoutSettings->Transform.GetRotation();
				PxQuat PQuat = PxQuat(Rot.X, Rot.Y, Rot.Z, Rot.W);
				FVector Loc = FractureSettings.CutoutSettings->Transform.GetLocation();
				PxVec3 PPos = PxVec3(Loc.X, Loc.Y, Loc.Z);

				CutoutConfig.transform = PxTransform(PPos, PQuat);

				bool Periodic = true;
				bool ExpandGaps = false;

				const uint8_t* pixelBuffer = RawData.GetData();
				NvBlastExtAuthoringBuildCutoutSet(*CutoutConfig.cutoutSet, pixelBuffer, Width, Height, FractureSettings.CutoutSettings->SegmentationErrorThreshold, FractureSettings.CutoutSettings->SnapThreshold, Periodic, ExpandGaps);

				ReturnCode = BlastFractureTool->cutout(ChunkID, CutoutConfig, false, &RandomGenerator);
				if (ReturnCode != 0)
				{
					UE_LOG(LogFractureMesh, Error, TEXT("Mesh Slicing failed ReturnCode=%d"), ReturnCode);
				}
			}
		}
		break;

		case EMeshFractureMode::Brick:
		{
			Nv::Blast::NoiseConfiguration Noise;

			FVector StartPosition(0, 0, 0);
			FVector Normal(0, 0, 1);

			for (int ii = 0; ii < 10; ++ii)
			{
				FVector TransformedNormal(ChunkTransform.Inverse().GetRotation().RotateVector(Normal));
				FVector TransformedPosition(ChunkTransform.InverseTransformPosition(StartPosition + FBoundsCenter));

				PlaneCut(BlastFractureTool, Noise, &RandomGenerator, TransformedPosition, TransformedNormal);
				StartPosition.Z += 37.5;
			}


			TMap<FVector, uint32> SortedBrickLayers;
			TMap<float, FVector> SortedBricksY;
			{
				uint32 ChunkCount = BlastFractureTool->getChunkCount();

				for (uint32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
				{
					const ChunkInfo& Info = BlastFractureTool->getChunkInfo(ChunkIndex);
					if (Info.chunkId != 0)
					{
						const physx::PxBounds3& ChunkBounds = Info.meshData->getBoundingBox();
						const physx::PxVec3 Center = ChunkBounds.getCenter();
						SortedBrickLayers.Add(FVector(Center.x, Center.y, Center.z), Info.chunkId);
						SortedBricksY.Add(Center.z, FVector(Center.x, Center.y, Center.z));
					}
				}
			}
			TArray<FVector> BricksSortedInY;
			TArray<uint32> BricksInY, AltBricksInY;
			SortedBricksY.GenerateValueArray(BricksSortedInY);
			for (int32 ii = 0, ni = BricksSortedInY.Num(); ii < ni; ++ii)
			{
				if (ii % 2)
				{
					BricksInY.Add(SortedBrickLayers[BricksSortedInY[ii]]);
				}
				else
				{
					AltBricksInY.Add(SortedBrickLayers[BricksSortedInY[ii]]);
				}
			}


			Normal = FVector(1, 0, 0);

			for (int32 kk = 0, nk = BricksInY.Num(); kk < nk; ++kk)
			{
				StartPosition = FVector(0, 0, 0);
				TSet<int32> ChunksToCut;
				ChunksToCut.Add(BricksInY[kk]);

				while (ChunksToCut.Num() > 0)
				{
					TArray<int32> PreChunks = GetChunkIDs(BlastFractureTool);
					for (int32 ChunkIndex : ChunksToCut)
					{
						int32 ResultCode = PlaneCut(BlastFractureTool, Noise, &RandomGenerator, StartPosition, Normal, ChunkIndex);
						if (ResultCode != 0)
						{
							ChunksToCut.Remove(ChunkIndex);
						}
					}
					StartPosition.X += 115;
					TArray<int32> PostChunks = GetChunkIDs(BlastFractureTool);
					TArray<int32> DiffChunks = GetAddedIDs(PreChunks, PostChunks);
					if (DiffChunks.Num() > 1)
					{
						ChunksToCut.Empty();
						ChunksToCut.Append(DiffChunks);
					}
					else if (DiffChunks.Num() > 0)
					{
						ChunksToCut.Append(DiffChunks);
					}
					else
					{
						ChunksToCut.Empty();
					}
				}

			}


			// 			for (int32 ii = 0; ii < 10; ++ii)
			// 			{
			// 				for (int32 kk = 0, nk = BricksInY.Num(); kk < nk; ++kk)
			// 				{
			// 					uint32 ChunkIndex = BricksInY[kk];
			//  					PlaneCut(BlastFractureTool, Noise, &RandomGenerator, StartPosition, Normal, ChunkIndex);
			// 				}
			// 				for (int32 kk = 0, nk = AltBricksInY.Num(); kk < nk; ++kk)
			// 				{
			// 					uint32 ChunkIndex = AltBricksInY[kk];
			// 					PlaneCut(BlastFractureTool, Noise, &RandomGenerator, StartPosition + FVector(57.5,0,0), Normal, ChunkIndex);
			// 				}
			// 				StartPosition.X += 115;
			// 			}

			// 			StartPosition = FVector(0, 0, 0);
			// 			Normal = FVector(1, 0, 0);
			// 			for (int ii = 0; ii < 5; ++ii)
			// 			{
			// 				PlaneCut(BlastFractureTool, Noise, &RandomGenerator, StartPosition, Normal);
			// 				StartPosition.X += 37.0;
			// 			}


			//if (ReturnCode != 0)
			//{
			//	UE_LOG(LogFractureMesh, Error, TEXT("Mesh Brick failed ReturnCode=%d"), ReturnCode);
			//}
		}
		break;

		default:
			UE_LOG(LogFractureMesh, Error, TEXT("Invalid Mesh Fracture Mode"));

		}
		
		if (ReturnCode == 0)
		{
			// triangulates cut surfaces and fixes up UVs
			BlastFractureTool->finalizeFracturing();

			TArray<FGeneratedFracturedChunk> LocalGeneratedChunks;
			TArray<int32> LocalDeletedChunks;
			// Makes a Geometry collection for each of fracture chunks
			bAllChunksGood = GenerateChunkMeshes(BlastFractureTool, FractureSettings, FracturedChunkIndex, ParentName, Transform, NewBlastMesh, FracturedGeometryCollection, LocalGeneratedChunks, LocalDeletedChunks);

			static FCriticalSection Mutex;

			Mutex.Lock();
			GeneratedChunksOut += LocalGeneratedChunks;
			DeletedChunksOut += LocalDeletedChunks;
			Mutex.Unlock();

			float ProcessingTime = static_cast<float>(FPlatformTime::Seconds() - CacheStartTime);
			LogStatsAndTimings(NewBlastMesh, BlastFractureTool, Transform, FractureSettings, ProcessingTime);

			if (IsInGameThread() && FractureMesh::CVarEnableBlastDebugVisualization.GetValueOnGameThread() != 0)
			{
				RenderDebugGraphics(BlastFractureTool, FractureSettings, Transform);
			}
		}

		// release tools
		if (SiteGenerator)
		{
			SiteGenerator->release();
		}
		if (NewBlastMesh)
		{
			NewBlastMesh->release();
		}
	}

	BlastFractureTool->release();
#endif
	return bAllChunksGood;
}

#if PLATFORM_WINDOWS
bool UFractureMesh::GenerateChunkMeshes(Nv::Blast::FractureTool* BlastFractureTool, const UMeshFractureSettings& FractureSettings, int32 FracturedChunkIndex, const FString& ParentName, const FTransform& ParentTransform, Nv::Blast::Mesh* BlastMesh, UGeometryCollection* FracturedGeometryCollection, TArray<FGeneratedFracturedChunk>& GeneratedChunksOut, TArray<int32>& DeletedChunksOut)
{
	check(BlastFractureTool);
	check(BlastMesh);
	check(FracturedGeometryCollection);
	check(FractureSettings.CommonSettings);

	// -1 special case when fracturing a fresh static mesh
	if (FracturedChunkIndex < 0)
		FracturedChunkIndex = 0;
	return FMeshUtility::AddBlastMeshToGeometryCollection(BlastFractureTool, FracturedChunkIndex, ParentName, ParentTransform, FracturedGeometryCollection, GeneratedChunksOut, DeletedChunksOut);
}
#endif

void UFractureMesh::FixupHierarchy(int32 FracturedChunkIndex, class UGeometryCollection* GeometryCollectionObject, FGeneratedFracturedChunk& GeneratedChunk, const FString& Name)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();

	if (!GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
	{
		GeometryCollection->AddAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	}

	TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
	TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
	TManagedArray<int32>& Level = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	TManagedArray<int32>& Parent = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;

	int32 LastTransformGroupIndex = GeometryCollection->NumElements(FGeometryCollection::TransformGroup) - 1;

	TManagedArray<FTransform> & TransformsOut = GeometryCollection->Transform;
	// additional data to allow us to operate the exploded view slider in the editor
	ExplodedTransforms[LastTransformGroupIndex] = TransformsOut[LastTransformGroupIndex];
	ExplodedVectors[LastTransformGroupIndex] = GeneratedChunk.ChunkLocation;

	// bone hierarchy and chunk naming
	int32 ParentFractureLevel = Level[FracturedChunkIndex];

	if (GeneratedChunk.FirstChunk)
	{
		// the root/un-fractured piece, fracture level 0, No parent bone
		Level[LastTransformGroupIndex] = 0;
		BoneNames[LastTransformGroupIndex] = Name;
	}
	else
	{
		// all of the chunk fragments, fracture level > 0, has valid parent bone
		Level[LastTransformGroupIndex] = ParentFractureLevel + 1;
	}

	Parent[LastTransformGroupIndex] = GeneratedChunk.ParentBone;

	if (GeneratedChunk.ParentBone >= 0)
	{
		Children[GeneratedChunk.ParentBone].Add(LastTransformGroupIndex);
	}

	FGeometryCollectionClusteringUtility::RecursivelyUpdateChildBoneNames(FracturedChunkIndex, Children, BoneNames);
	FMeshUtility::ValidateGeometryCollectionState(GeometryCollectionObject);

}

#if PLATFORM_WINDOWS
void UFractureMesh::LogStatsAndTimings(const Nv::Blast::Mesh* BlastMesh, const Nv::Blast::FractureTool* BlastFractureTool, const FTransform& Transform, const UMeshFractureSettings& FractureSettings, float ProcessingTime)
{
	check(FractureSettings.CommonSettings);

	uint32 VertexCount = BlastMesh->getVerticesCount();
	uint32 EdgeCount = BlastMesh->getEdgesCount();
	uint32 FacetCount = BlastMesh->getFacetCount();

	FVector Scale = Transform.GetScale3D();
	UE_LOG(LogFractureMesh, Verbose, TEXT("Scaling %3.2f, %3.2f, %3.2f"), Scale.X, Scale.Y, Scale.Z);
	UE_LOG(LogFractureMesh, Verbose, TEXT("Mesh: VertCount=%d, EdgeCount=%d, FacetCount=%d"), VertexCount, EdgeCount, FacetCount);
	UE_LOG(LogFractureMesh, Verbose, TEXT("Fracture Chunk Count = %d"), BlastFractureTool->getChunkCount());
	if (ProcessingTime < 0.5f)
	{
		UE_LOG(LogFractureMesh, Verbose, TEXT("Fracture: Fracturing Time=%5.4f ms"), ProcessingTime*1000.0f);
	}
	else
	{
		UE_LOG(LogFractureMesh, Verbose, TEXT("Fracture: Fracturing Time=%5.4f seconds"), ProcessingTime);
	}
}
#endif

void UFractureMesh::ExtractDataFromTexture(const TWeakObjectPtr<UTexture> SourceTexture, TArray<uint8>& RawDataOut, int32& WidthOut, int32& HeightOut)
{
	// use the source art if it exists
	FTextureSource* TextureSource = nullptr;
	if ((SourceTexture != nullptr) && SourceTexture->Source.IsValid())
	{
		switch (SourceTexture->Source.GetFormat())
		{
		case TSF_G8:
		case TSF_BGRA8:
			TextureSource = &(SourceTexture->Source);
			break;
		default:
			break;
		};
	}

	if (TextureSource != nullptr)
	{
		TArray64<uint8> TextureRawData;
		TextureSource->GetMipData(TextureRawData, 0);
		int32 BytesPerPixel = TextureSource->GetBytesPerPixel();
		ETextureSourceFormat PixelFormat = TextureSource->GetFormat();
		WidthOut = TextureSource->GetSizeX();
		HeightOut = TextureSource->GetSizeY();

		// 		WidthOut = 100;
		// 		HeightOut = 100;


		RawDataOut.SetNumZeroed(WidthOut * HeightOut * 3);

		for (int32 ii = 0, ni = WidthOut * HeightOut; ii < ni; ++ii)
		{
			// 			uint8 val = (TextureRawData[ii*BytesPerPixel] + TextureRawData[1+ii*BytesPerPixel] + TextureRawData[2+ii*BytesPerPixel]) / 3;
			// 			RawDataOut[ii] = val > 25 ? val : 0;
			// 			int32 PixelByteOffset = (X + Y * WidthOut) * 3;
			RawDataOut[ii * 3 + 0] = TextureRawData[ii*BytesPerPixel + 0];
			RawDataOut[ii * 3 + 1] = TextureRawData[ii*BytesPerPixel + 1];
			RawDataOut[ii * 3 + 2] = TextureRawData[ii*BytesPerPixel + 2];


			// 			RawDataOut[ii] = TextureRawData[ii*BytesPerPixel];
		}

		//
		// 		for (int Y = 20; Y < 25; ++Y)
		// 		{
		// 			for (int X = 0; X < WidthOut; ++X)
		// 			{
		// 				int32 PixelByteOffset = (X + Y * WidthOut) * 3;
		// 				RawDataOut[PixelByteOffset + 0] = 255;
		// 				RawDataOut[PixelByteOffset + 1] = 255;
		// 				RawDataOut[PixelByteOffset + 2] = 255;
		// 			}
		// 		}
		//
		// 		for (int Y = 75; Y < 80; ++Y)
		// 		{
		// 			for (int X = 0; X < WidthOut; ++X)
		// 			{
		// 				int32 PixelByteOffset = (X + Y * WidthOut) * 3;
		// 				RawDataOut[PixelByteOffset + 0] = 255;
		// 				RawDataOut[PixelByteOffset + 1] = 255;
		// 				RawDataOut[PixelByteOffset + 2] = 255;
		// 			}
		// 		}


		// 		for (int Y = 0; Y < HeightOut; ++Y)
		// 		{
		// 			for (int X = 0; X < WidthOut; ++X)
		// 			{
		// 				int32 PixelByteOffset = (X + Y * WidthOut) * BytesPerPixel;
		// 				const uint8* PixelPtr = TextureRawData.GetData() + PixelByteOffset;
		// 				FColor Color;
		// 				if (PixelFormat == TSF_BGRA8)
		// 				{
		// 					Color = *((FColor*)PixelPtr);
		// 				}
		// 				else
		// 				{
		// 					checkSlow(PixelFormat == TSF_G8);
		// 					const uint8 Intensity = *PixelPtr;
		// 					Color = FColor(Intensity, Intensity, Intensity, Intensity);
		// 				}
		//
		// 				uint8 Val = (Color.R + Color.G + Color.B ) /3;
		// // 				if (Val > 100)
		// // 					RawDataOut[Y * WidthOut + X] = 255;
		// // 				else
		// // 					RawDataOut[Y * WidthOut + X] = 0;
		//
		// 				RawDataOut[Y * WidthOut + X] = Val;
		// 			}
		// 		}
	}
}

#if PLATFORM_WINDOWS
void UFractureMesh::RenderDebugGraphics(Nv::Blast::FractureTool* BlastFractureTool, const UMeshFractureSettings& FractureSettings, const FTransform& Transform)
{
	check(FractureSettings.CommonSettings);

	uint32 ChunkCount = BlastFractureTool->getChunkCount();
	for (uint32 ChunkIndex = 0; ChunkIndex < ChunkCount; ChunkIndex++)
	{
		const Nv::Blast::ChunkInfo& ChunkInfo = BlastFractureTool->getChunkInfo(ChunkIndex);
		Nv::Blast::Mesh* ChunkMesh = ChunkInfo.meshData;

		// only render the children
		bool DebugDrawParent = false;
		uint32 StartIndex = DebugDrawParent ? 0 : 1;
		if (ChunkIndex >= StartIndex)
		{
			DrawDebugBlastMesh(ChunkMesh, ChunkIndex, UMeshFractureSettings::ExplodedViewExpansion, Transform);
		}
	}
}

void UFractureMesh::DrawDebugBlastMesh(const Nv::Blast::Mesh* ChunkMesh, int ChunkIndex, float ExplodedViewAmount, const FTransform& Transform)
{
	UWorld* InWorld = FindEditorWorld();

	TArray<FColor> Colors = { FColor::Red, FColor::Green, FColor::Blue, FColor::Yellow, FColor::Magenta, FColor::Cyan, FColor::Black, FColor::Orange, FColor::Purple };
	FColor UseColor = Colors[ChunkIndex % 9];

	const physx::PxBounds3& Bounds = ChunkMesh->getBoundingBox();
	float MaxBounds = FMath::Max(FMath::Max(Bounds.getExtents().x, Bounds.getExtents().y), Bounds.getExtents().z);

	PxVec3 ApproxChunkCenter = Bounds.getCenter();

	uint32 NumEdges = ChunkMesh->getEdgesCount();
	const Nv::Blast::Edge* Edges = ChunkMesh->getEdges();
	const Nv::Blast::Vertex* Vertices = ChunkMesh->getVertices();
	for (uint32 i = 0; i < NumEdges; ++i)
	{
		const Nv::Blast::Vertex& V1 = Vertices[Edges[i].s];
		const Nv::Blast::Vertex& V2 = Vertices[Edges[i].e];

		PxVec3 S = V1.p + ApproxChunkCenter * MaxBounds*5.0f;
		PxVec3 E = V2.p + ApproxChunkCenter * MaxBounds*5.0f;

		FVector Start = FVector(S.x, S.y, S.z)*MagicScaling;
		FVector End = FVector(E.x, E.y, E.z)*MagicScaling;

		FVector TStart = Transform.TransformPosition(Start);
		FVector TEnd = Transform.TransformPosition(End);
		DrawDebugLine(InWorld, TStart, TEnd, UseColor, true);
	}
}

void UFractureMesh::GenerateUniformSites(const FRandomStream &RandomStream, const FVector Offset, const FBox& Bounds, uint32 NumberToGenerate, TArray<FVector> &Sites) const
{
	const FVector Extents = Bounds.GetExtent();
	Sites.Reserve(Sites.Num() + NumberToGenerate);
	for (uint32 ii = 0; ii < NumberToGenerate; ++ii)
	{
		FVector Site(RandomStream.FRandRange(-Extents.X, Extents.X), RandomStream.FRandRange(-Extents.Y, Extents.Y), RandomStream.FRandRange(-Extents.Z, Extents.Z));
		Site += Offset;
		Sites.Emplace(Site);
	}
}

void UFractureMesh::GenerateSitesInSphere(const FRandomStream &RandomStream, const FVector Offset, float Radius, uint32 NumberToGenerate, TArray<FVector> &Sites) const
{
	Sites.Reserve(Sites.Num() + NumberToGenerate);
	for (uint32 ii = 0; ii < NumberToGenerate; ++ii)
	{
		FVector Site(RandomStream.GetUnitVector());
		float Distance = RandomStream.GetFraction();
		Site *= Radius * Distance * Distance;
		Site += Offset;
		Sites.Emplace(Site);
	}
}

void UFractureMesh::ScatterInBounds(const FRandomStream& RandomStream, const FBox& Bounds, uint32 NumberToGenerate, TArray<FVector>& Positions, TArray<FVector>& Normals) const
{
	check(Positions.Num() == Normals.Num());

	int32 PositionCount = Positions.Num();
	Positions.Reserve(PositionCount + NumberToGenerate);
	FBox ShrunkBounds(ForceInitToZero);
	ShrunkBounds += Bounds.Min * 0.9;
	ShrunkBounds += Bounds.Max * 0.9;

	GenerateUniformSites(RandomStream, FVector::ZeroVector, ShrunkBounds, NumberToGenerate, Positions);

	Normals.SetNum(PositionCount + NumberToGenerate);

	FVector ScaleVector(Bounds.GetExtent());
	ScaleVector /= ScaleVector.GetMax();

	for (int32 ii = PositionCount, ni = Normals.Num(); ii < ni; ++ii)
	{
		Normals[ii] = RandomStream.GetUnitVector();
		Normals[ii] *= FMath::Lerp(ScaleVector, FVector::OneVector, (float)ii / (float)ni);
		Normals[ii].Normalize();
	}
}

int32 UFractureMesh::PlaneCut(Nv::Blast::FractureTool* BlastFractureTool, const Nv::Blast::NoiseConfiguration& Noise, FractureRandomGenerator* RandomGenerator, const FVector& InPosition, const FVector& InNormal, int32 ChunkID) const
{
	TArray<int32> ChunkIDs;
	ChunkIDs.Push(ChunkID);
	int CutNumber = 0;

	int32 NumChunks = static_cast<int32>(BlastFractureTool->getChunkCount());
	if (NumChunks > 2 && ChunkID == 0)
	{
		CutNumber++;
		ChunkIDs.Empty();

		// All generated chunks are candidates for any further cuts (however we must exclude the initial chunk from now on)
		for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ChunkIndex++)
		{
			int32 NewChunkID = BlastFractureTool->getChunkId(ChunkIndex);

			// don't try and fracture the initial chunk again
			if (NewChunkID != ChunkID)
			{
				ChunkIDs.Push(NewChunkID);
			}
		}
	}

	bool Replace = CutNumber != 0;
	if (ChunkID != 0)
	{
		Replace = true;
	}

	for (int32 CID : ChunkIDs)
	{
		// 		if (RandomStream.GetFraction() <= FractureSettings.PlaneCutSettings->CutChunkChance)
		{
			BlastFractureTool->cut(CID, FVEC_TO_PHYSX(InNormal), FVEC_TO_PHYSX(InPosition), Noise, Replace, RandomGenerator);
		}
	}

	// ReturnCode zero is a success, if we end with one chunk then the chunk we started with hasn't been split
	return !(BlastFractureTool->getChunkCount() > 2);
}

TArray<int32> UFractureMesh::GetChunkIDs(Nv::Blast::FractureTool* BlastFractureTool) const
{
	TArray<int32> ReturnArray;

	int32 NumChunks = static_cast<int32>(BlastFractureTool->getChunkCount());
	for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
	{
		int32 ChunkID = BlastFractureTool->getChunkId(ChunkIndex);
		ReturnArray.Push(ChunkID);
	}
	return ReturnArray;
}

TArray<int32> UFractureMesh::GetAddedIDs(const TArray<int32>& StartingArray, const TArray<int32>& EndingArray) const
{
	TArray<int32> ReturnArray;

	for (int32 Value : EndingArray)
	{
		if (!StartingArray.Contains(Value))
		{
			ReturnArray.Add(Value);
		}
	}
	return ReturnArray;
}

#endif

#undef LOCTEXT_NAMESPACE
