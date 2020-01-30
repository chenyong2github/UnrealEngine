// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshProcessingLibrary.h"
#include "MeshProcessingLibraryModule.h"

#include "CoreMinimal.h"
#include "AssetData.h"
#include "Async/ParallelFor.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/StaticMesh.h"
#include "Engine/MeshMerging.h"
#include "Layers/LayersSubsystem.h"
#include "Logging/LogMacros.h"
#include "Math/Box.h"
#include "MeshMergeData.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionAdapter.h"
#include "MeshDescriptionOperations.h"
#include "Misc/ScopedSlowTask.h"
#include "Templates/Tuple.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/StaticMeshComponent.h"

#if WITH_MESH_SIMPLIFIER
#include "MeshSimplifier.h"
#endif // WITH_MESH_SIMPLIFIER

#if WITH_PROXYLOD
#include "ProxyLODVolume.h"
#endif
#include "Subsystems/AssetEditorSubsystem.h"


DEFINE_LOG_CATEGORY(LogMeshProcessingLibrary);

#define LOCTEXT_NAMESPACE "MeshProcessingLibrary"
//#define DEBUG_EXPORT_ENVELOP

namespace MeshProcessingUtils
{
	bool CheckIfInEditorAndPIE(const TCHAR* FunctionName)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogMeshProcessingLibrary, Error, TEXT("%s: You are not on the main thread."), FunctionName);
			return false;
		}
		if (!GIsEditor)
		{
			UE_LOG(LogMeshProcessingLibrary, Error, TEXT("%s: You are not in the Editor."), FunctionName);
			return false;
		}
		if (GEditor->PlayWorld || GIsPlayInEditorWorld)
		{
			UE_LOG(LogMeshProcessingLibrary, Error, TEXT("%s: The Editor is currently in a play mode."), FunctionName);
			return false;
		}
		return true;
	}

	// Same as FMeshMergeHelpers::TransformRawMeshVertexData
	// Transform raw mesh vertex data by the Static Mesh Component's component to world transformation
	void TransformRawMeshVertexData(const FTransform& InTransform, FMeshDescription &OutRawMesh)
	{
		TVertexAttributesRef<FVector> VertexPositions = OutRawMesh.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
		TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
		TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);

		for (const FVertexID VertexID : OutRawMesh.Vertices().GetElementIDs())
		{
			VertexPositions[VertexID] = InTransform.TransformPosition(VertexPositions[VertexID]);
		}

		auto TransformNormal = [&](FVector& Normal)
		{
			FMatrix Matrix = InTransform.ToMatrixWithScale();
			const float DetM = Matrix.Determinant();
			FMatrix AdjointT = Matrix.TransposeAdjoint();
			AdjointT.RemoveScaling();

			Normal = AdjointT.TransformVector(Normal);
			if (DetM < 0.f)
			{
				Normal *= -1.0f;
			}
		};

		for (const FVertexInstanceID VertexInstanceID : OutRawMesh.VertexInstances().GetElementIDs())
		{
			FVector TangentX = VertexInstanceTangents[VertexInstanceID];
			FVector TangentZ = VertexInstanceNormals[VertexInstanceID];
			FVector TangentY = FVector::CrossProduct(TangentZ, TangentX).GetSafeNormal() * VertexInstanceBinormalSigns[VertexInstanceID];

			TransformNormal(TangentX);
			TransformNormal(TangentY);
			TransformNormal(TangentZ);

			VertexInstanceTangents[VertexInstanceID] = TangentX;
			VertexInstanceBinormalSigns[VertexInstanceID] = GetBasisDeterminantSign(TangentX, TangentY, TangentZ);
			VertexInstanceNormals[VertexInstanceID] = TangentZ;
		}

		const bool bIsMirrored = InTransform.GetDeterminant() < 0.f;
		if (bIsMirrored)
		{
			OutRawMesh.ReverseAllPolygonFacing();
		}
	}

	void UpdateUndoBufferSize()
	{
		if (MeshProcessingUtils::CheckIfInEditorAndPIE(TEXT("DefeatureMesh")) && GetDefault< UMeshProcessingEnterpriseSettings >())
		{
			UTransBuffer* TransBuffer = CastChecked<UTransBuffer>(GEditor->Trans);
			int32 OverrideUndoBufferSize = GetDefault< UMeshProcessingEnterpriseSettings >()->OverrideUndoBufferSize * 1024 * 1024;

			if (OverrideUndoBufferSize > 0 && OverrideUndoBufferSize != TransBuffer->MaxMemory)
			{
				TransBuffer->MaxMemory = OverrideUndoBufferSize;
			}

		}
	}

	/**
	 * Roughly (gu)estimate if value of Options->Accuracy could generate huge number of voxels
	 * If so, warn user, choose a more adequate value and proceed
	 */
	float ValidateVoxelSize(float InAccuracy, const TArray<UStaticMeshComponent*>& StaticMeshComponents)
	{
		const uint64 EmpiricalReasonableVoxelCount = 1073741824;

		float OutAccuracy = InAccuracy;

		double TotalVolume = 0;
		for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
		{
			const FVector Scale = StaticMeshComponent->GetComponentTransform().GetScale3D();
			FVector ComponentExtent = StaticMeshComponent->GetStaticMesh()->GetBoundingBox().GetExtent() * Scale;

			// Add volume of component's bounding box
			ComponentExtent = ComponentExtent.ComponentMax(FVector(InAccuracy));
			TotalVolume += (double)ComponentExtent[0] * (double)ComponentExtent[1] * (double)ComponentExtent[2];
		}

		const uint64 MaxGuestimatedVoxelCount = (uint64) (TotalVolume / (InAccuracy*InAccuracy*InAccuracy));

		if (MaxGuestimatedVoxelCount > EmpiricalReasonableVoxelCount)
		{
			// Best guest on accuracy is to be equal to the value to use in order to potentially
			// subdivide the total volume of the selected actors in 'EmpiricalReasonableVoxelCount' cells.
			OutAccuracy = FMath::Pow(TotalVolume / (double)EmpiricalReasonableVoxelCount, 1.0f/3.0f);

			// Inform user accuracy has been modified
			UE_LOG(LogMeshProcessingLibrary, Warning, TEXT("AssemblyJacketing: Voxel precision of %.5f too small. Using %.5f instead."), InAccuracy, OutAccuracy);
		}

		return OutAccuracy;
	}
}

#if WITH_MESH_SIMPLIFIER
void UMeshProcessingLibrary::DefeatureMesh(FMeshDescription& MeshDescription, const UMeshDefeaturingParameterObject& Parameters)
{
	FMeshDescriptionAdapter MeshAdapter(MeshDescription);
	MeshSimplifier::FDefeaturingParameters Params = {
		Parameters.bFillThroughHoles,
		Parameters.bFillBlindHoles,
		Parameters.bRemoveProtrusions,
		Parameters.ThroughHoleMaxDiameter,
		Parameters.FilledHoleMaxDiameter,
		Parameters.FilledHoleMaxDepth,
		Parameters.ProtrusionMaxDiameter,
		Parameters.ProtrusionMaxHeight,
		Parameters.MaxVolumeRatio,
		Parameters.ChordTolerance,
		Parameters.AngleTolerance
	};
	FMeshSimplifier::Defeaturing(&MeshAdapter, Params);
	FElementIDRemappings Remappings;
	MeshDescription.Compact(Remappings);
}

void UMeshProcessingLibrary::DefeatureMesh(UStaticMesh* StaticMesh, int32 LODIndex, const UMeshDefeaturingParameterObject* Parameters)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogMeshProcessingLibrary, Error, TEXT("DefeatureMesh: The StaticMesh is null."));
		return;
	}

	if (!MeshProcessingUtils::CheckIfInEditorAndPIE(TEXT("DefeatureMesh")))
	{
		return;
	}

	MeshProcessingUtils::UpdateUndoBufferSize();

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bStaticMeshIsEdited = false;
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);
	if (MeshDescription == nullptr)
	{
		UE_LOG(LogMeshProcessingLibrary, Error, TEXT("DefeatureMesh: The selected LOD for StaticMesh does not have geometry."));
		return;
	}

	DefeatureMesh(*MeshDescription, *Parameters);


	StaticMesh->CommitMeshDescription(LODIndex);

	// Request re-building of mesh with new collision shapes
	StaticMesh->PostEditChange();

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if (bStaticMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
	}
}
#else // WITH_MESH_SIMPLIFIER
void UMeshProcessingLibrary::DefeatureMesh(UStaticMesh* StaticMesh, int32 LODIndex, const UMeshDefeaturingParameterObject* Parameters){}
void UMeshProcessingLibrary::DefeatureMesh(FMeshDescription& MeshDescription, const UMeshDefeaturingParameterObject& Parameters){}
#endif // WITH_MESH_SIMPLIFIER

#if WITH_PROXYLOD
// See FVoxelizeMeshMerging::ProxyLOD
void UMeshProcessingLibrary::ApplyJacketingOnMeshActors(const TArray<AActor*>& Actors, const UJacketingOptions* Options, TArray<AActor*>& OccludedActorArray, bool bSilent)
{
	if (Actors.Num() == 0)
	{
		UE_LOG(LogMeshProcessingLibrary, Warning, TEXT("AssemblyJacketing: No actors to process. Aborting..."));
		return;
	}

	MeshProcessingUtils::UpdateUndoBufferSize();

	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();
	uint64 LastTime = FPlatformTime::Cycles64();

	TMap<AActor*, int32> ActorOccurences;
	ActorOccurences.Reserve(Actors.Num());
	for (AActor* Actor : Actors)
	{
		ActorOccurences.Add(Actor, 0);
	}

	// Collect all StaticMeshComponent objects
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	for (AActor* Actor : Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}

		int32 ComponentCount = 0;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
			{
				if (StaticMeshComponent->GetStaticMesh() == nullptr)
				{
					continue;
				}
				ComponentCount++;
				StaticMeshComponents.Add(StaticMeshComponent);
			}
		}

		ActorOccurences[Actor] = ComponentCount;
	}

	if (StaticMeshComponents.Num() == 0)
	{
		UE_LOG(LogMeshProcessingLibrary, Warning, TEXT("AssemblyJacketing: No meshes to process. Aborting..."));
		return;
	}

	float Accuracy = MeshProcessingUtils::ValidateVoxelSize(Options->Accuracy, StaticMeshComponents);

	TSharedPtr<FScopedSlowTask> Progress;
	const float Voxelization		= Options->Target == EJacketingTarget::Mesh ? 10.f : 40.f;
	const float GapFilling			= Options->Target == EJacketingTarget::Mesh ? 5.f : 20.f;
	const float MeshTesting			= Options->Target == EJacketingTarget::Mesh ? 10.f : 38.f;
	const float VertexTesting		= Options->Target == EJacketingTarget::Mesh ? 20.f : 0.f;
	const float TriangleDeletion	= Options->Target == EJacketingTarget::Mesh ? 20.f : 0.f;
	const float MeshBuilding		= Options->Target == EJacketingTarget::Mesh ? 33.f : 0.f;
	if (!bSilent)
	{
		Progress = TSharedPtr<FScopedSlowTask>(new FScopedSlowTask(100.0f, LOCTEXT("StartWork", "Occlusion culling ..."), true, *GWarn));
		Progress->MakeDialog(true);
		Progress->EnterProgressFrame(1.f, FText::FromString(TEXT("Analyzing meshes ...")));
	}

	// Geometry input data for voxelizing methods
	TArray<FMeshMergeData> Geometry;
	// Store world space mesh for each static mesh component
	TMap<UStaticMeshComponent*, FMeshDescription*> RawMeshes;
	int32 VertexCount = 0;
	int32 PolygonCount = 0;
	int32 DeletedPolygonCount = 0;

	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		if (StaticMesh == nullptr)
		{
			continue;
		}

		// FMeshMergeData will release the RawMesh...
		FMeshDescription* RawMeshOriginal = StaticMesh->GetMeshDescription(0);
		FMeshDescription* RawMesh = new FMeshDescription();
		*RawMesh = *RawMeshOriginal;
		//Make sure all ID are from 0 to N
		FElementIDRemappings OutRemappings;
		RawMesh->Compact(OutRemappings);

		const FTransform& ComponentToWorldTransform = StaticMeshComponent->GetComponentTransform();

		// Transform raw mesh vertex data by the Static Mesh Component's component to world transformation
		MeshProcessingUtils::TransformRawMeshVertexData(ComponentToWorldTransform, *RawMesh);

		VertexCount += RawMesh->Vertices().Num();
		PolygonCount += RawMesh->Polygons().Num();

		// Stores transformed RawMesh for later use
		RawMeshes.Add(StaticMeshComponent, RawMesh);

		FMeshMergeData MergeData;
		MergeData.bIsClippingMesh = false;
		MergeData.SourceStaticMesh = StaticMesh;
		MergeData.RawMesh = RawMesh;

		Geometry.Add(MergeData);
	}

	if (Geometry.Num() == 0)
	{
		UE_LOG(LogMeshProcessingLibrary, Warning, TEXT("AssemblyJacketing: No geometry to process. Aborting..."));
		return;
	}

	if (Progress.IsValid())
	{
		Progress->EnterProgressFrame(Voxelization, FText::FromString(TEXT("Creating Voxelization ...")) );
	}

	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Processing %i components, %i polygons"), StaticMeshComponents.Num(), PolygonCount);

	TUniquePtr<IProxyLODVolume> Volume(IProxyLODVolume::CreateSDFVolumeFromMeshArray(Geometry, Accuracy));
	if (!Volume.IsValid())
	{
		UE_LOG(LogMeshProcessingLibrary, Error, TEXT("AssemblyJacketing: Voxelization of geometry failed. Aborting process..."));
		return;
	}

	if (Progress.IsValid())
	{
		Progress->EnterProgressFrame(GapFilling, FText::FromString(TEXT("Closing gaps ...")));
	}

	double IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Creation of volume took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	double HoleRadius = 0.5 * Options->MergeDistance;
	IProxyLODVolume::FVector3i VolumeBBoxSize = Volume->GetBBoxSize();

	// Clamp the hole radius.
	const double VoxelSize = Volume->GetVoxelSize();
	int32 MinIndex = VolumeBBoxSize.MinIndex();
	double BBoxMinorAxis = VolumeBBoxSize[MinIndex] * VoxelSize;
	if (HoleRadius > .5 * BBoxMinorAxis)
	{
		HoleRadius = .5 * BBoxMinorAxis;
		UE_LOG(LogMeshProcessingLibrary, Warning, TEXT("AssemblyJacketing: Merge distance %f too large, clamped to %f."), Options->MergeDistance, float(2. * HoleRadius));
	}

	// Used in gap-closing.  This max is to bound a potentially expensive computation.
	// If the gap size requires more dilation steps at the current voxel size,
	// then the dilation (and erosion) will be done with larger voxels.
	const int32 MaxDilationSteps = 7;

	LastTime = FPlatformTime::Cycles64();

	if (HoleRadius > 0.25 * VoxelSize && MaxDilationSteps > 0)
	{
		// performance tuning number.  if more dilations are required for this hole radius, a coarser grid is used.
		Volume->CloseGaps(HoleRadius, MaxDilationSteps);
	}

	TSet< AActor* > OccludedActorSet;
	OccludedActorSet.Reserve(Geometry.Num());

	IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Closure of gaps took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	int32 OccludedComponentCount = 0;
	// Set the maximum distance over which a point is considered outside the volume
	// Set to twice the precision requested
	float MaxDistance = -2.0f * Accuracy;

	float ProcessingStep = MeshTesting / (float)StaticMeshComponents.Num();
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (Progress.IsValid())
		{
			Progress->EnterProgressFrame(ProcessingStep, FText::FromString(TEXT("Checking inclusion of meshes ...")));
		}

		FVector Min, Max;
		StaticMeshComponent->GetLocalBounds(Min, Max);

		bool bComponentInside = true;

		// Check the corners of the component's bounding box
		for (int32 i = 0; i < 8; i++)
		{
			FVector Corner = Min;
			if (i % 2)
			{
				Corner.X += Max.X - Min.X;
			}
			if ((i / 2) % 2)
			{
				Corner.Y += Max.Y - Min.Y;
			}
			if (i / 4)
			{
				Corner.Z += Max.Z - Min.Z;
			}
			const FTransform& ComponentTransform = StaticMeshComponent->GetComponentTransform();
			const FVector WorldCorner = ComponentTransform.TransformPosition(Corner);
			const float Value = Volume->QueryDistance(WorldCorner);

			if (Value > MaxDistance)
			{
				bComponentInside = false;
				break;
			}
		}

		// Component's bounding box intersect with volume, check on vertices
		if (!bComponentInside)
		{
			bComponentInside = true;
			FMeshDescription* RawMesh = RawMeshes[StaticMeshComponent];
			TVertexAttributesConstRef<FVector> VertexPositions = RawMesh->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
			for (FVertexID VertexID : RawMesh->Vertices().GetElementIDs())
			{
				if (Volume->QueryDistance(VertexPositions[VertexID]) > MaxDistance)
				{
					bComponentInside = false;
					break;
				}
			}
		}

		if (bComponentInside)
		{
			DeletedPolygonCount += RawMeshes[StaticMeshComponent]->Polygons().Num();
			OccludedComponentCount++;

			AActor* Actor = StaticMeshComponent->GetOwner();

			// Decrement number of
			int32& ComponentCount = ActorOccurences[Actor];
			--ComponentCount;

			// All static mesh components of an actor are occluded, take action
			if (ComponentCount == 0)
			{
				OccludedActorSet.Add(Actor);
			}
		}
	}

	// Fill up input array and return if target is only Level
	if (Options->Target == EJacketingTarget::Level)
	{
		double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

		int32 ElapsedMin = int32(ElapsedSeconds / 60.0);
		ElapsedSeconds -= 60.0 * (double)ElapsedMin;
		UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: took %d min %.3f s. %d occluded actors out of %d"), ElapsedMin, ElapsedSeconds, OccludedActorSet.Num(), StaticMeshComponents.Num());

		OccludedActorArray = OccludedActorSet.Array();

		return;
	}

	IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Processing of meshes took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	// Proceed with triangle deletion
	if (Options->MergeDistance > VoxelSize)
	{
		// Expand interior narrow band to reach visible vertices discarded because of gap filling
		Volume->ExpandNarrowBand(VoxelSize, Options->MergeDistance);

		// Update MaxDistance to reflect expansion of narrow band
		if (Options->MergeDistance > 2.0f * VoxelSize)
		{
			MaxDistance = -Options->MergeDistance;
		}
	}

	if (Progress.IsValid())
	{
		Progress->EnterProgressFrame(VertexTesting, FText::FromString(TEXT("Checking occlusion of vertices ...")));
	}

	typedef TTuple<UStaticMeshComponent*, const FVector*, bool> FVertexData;
	TArray<FVertexData> VertexDataArray;
	VertexDataArray.Reserve(VertexCount);

	LastTime = FPlatformTime::Cycles64();

	// Mark any vertices of visible meshes which are outside the volume
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (OccludedActorSet.Find(StaticMeshComponent->GetOwner()) != nullptr)
		{
			continue;
		}

		TVertexAttributesConstRef<FVector> VertexPositions = RawMeshes[StaticMeshComponent]->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		for (FVertexID VertexID : RawMeshes[StaticMeshComponent]->Vertices().GetElementIDs())
		{
			VertexDataArray.Emplace(FVertexData(StaticMeshComponent, &VertexPositions[VertexID], false));
		}
	}

	VertexCount = VertexDataArray.Num();

	ParallelFor(VertexDataArray.Num(), [&](int32 Index) {
		FVertexData& VertexData = VertexDataArray[Index];

		if (!VertexData.Get<2>() && Volume->QueryDistance(*VertexData.Get<1>()) > MaxDistance)
		{
			VertexData.Get<2>() = true;
		}
	});

	IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Processing of vertices took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	if (Progress.IsValid())
	{
		Progress->EnterProgressFrame(TriangleDeletion, FText::FromString(TEXT("Deleting triangles ...")));
	}

	// Combining vertex visibility for meshes used in more than one static mesh component
	typedef TPair<UStaticMeshComponent*, TArray<bool>> TVisibilityPair;
	TMap<UStaticMesh*, TVisibilityPair> MeshesToRebuild;
	MeshesToRebuild.Reserve(StaticMeshComponents.Num());
	for (int32 Index = 0; Index < VertexCount; )
	{
		UStaticMeshComponent* StaticMeshComponent = VertexDataArray[Index].Get<0>();

		if (MeshesToRebuild.Find(StaticMeshComponent->GetStaticMesh()) == nullptr)
		{
			TVisibilityPair& VisibilityPair = MeshesToRebuild.Add(StaticMeshComponent->GetStaticMesh(), TVisibilityPair(StaticMeshComponent, TArray<bool>()));
			VisibilityPair.Value.AddZeroed(RawMeshes[StaticMeshComponent]->Vertices().Num());
		}

		TArray<bool>& VertexVisibility = MeshesToRebuild[StaticMeshComponent->GetStaticMesh()].Value;
		for (int32 VertexIndex = 0; Index < VertexCount && StaticMeshComponent == VertexDataArray[Index].Get<0>(); ++VertexIndex, ++Index)
		{
			VertexVisibility[VertexIndex] |= VertexDataArray[Index].Get<2>();
		}
	}

	// Removing occluded triangles from meshes
	TArray<UStaticMesh*> StaticMeshArray;
	MeshesToRebuild.GetKeys(StaticMeshArray);
	ParallelFor(MeshesToRebuild.Num(), [&](int32 Index) {
		UStaticMesh* StaticMesh = StaticMeshArray[Index];
		TVisibilityPair& VisibilityPair = MeshesToRebuild[StaticMesh];
		UStaticMeshComponent* StaticMeshComponent = VisibilityPair.Key;
		TArray<bool>& VertexVisibility = VisibilityPair.Value;

		FMeshDescription* RawMesh = RawMeshes[StaticMeshComponent];

		if (RawMesh->Polygons().Num() == 0 || RawMesh->Vertices().Num() == 0)
		{
			VisibilityPair.Key = nullptr;
			return;
		}

		FMeshDescription NewRawMesh = *RawMesh;

		TArray<FPolygonID> PolygonToRemove;
		for (const FPolygonID& PolygonID : NewRawMesh.Polygons().GetElementIDs())
		{
			TArray<FVertexID> PolygonVertices;
			NewRawMesh.GetPolygonVertices(PolygonID, PolygonVertices);
			bool bPolygonVisible = false;
			for (FVertexID VertexID : PolygonVertices)
			{
				bPolygonVisible |= VertexVisibility[VertexID.GetValue()];
			}
			if (!bPolygonVisible)
			{
				PolygonToRemove.Add(PolygonID);
			}
		}

		// All triangles are visible. This mesh might be instantiated in several locations
		if (PolygonToRemove.Num() == 0)
		{
			VisibilityPair.Key = nullptr;
		}
		// This should never happen. Such situation should have been caught earlier
		else if (PolygonToRemove.Num() == NewRawMesh.Polygons().Num())
		{
			check(false);
		}
		// Update mesh to only contain visible triangles
		else
		{
			TArray<FEdgeID> OrphanedEdges;
			TArray<FVertexInstanceID> OrphanedVertexInstances;
			TArray<FPolygonGroupID> OrphanedPolygonGroups;
			TArray<FVertexID> OrphanedVertices;
			for (FPolygonID PolygonID : PolygonToRemove)
			{
				NewRawMesh.DeletePolygon(PolygonID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
			}
			for (FPolygonGroupID PolygonGroupID : OrphanedPolygonGroups)
			{
				NewRawMesh.DeletePolygonGroup(PolygonGroupID);
			}
			for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstances)
			{
				NewRawMesh.DeleteVertexInstance(VertexInstanceID, &OrphanedVertices);
			}
			for (FEdgeID EdgeID : OrphanedEdges)
			{
				NewRawMesh.DeleteEdge(EdgeID, &OrphanedVertices);
			}
			for (FVertexID VertexID : OrphanedVertices)
			{
				NewRawMesh.DeleteVertex(VertexID);
			}
			//Compact and Remap IDs so we have clean ID from 0 to n since we just erase some polygons
			FElementIDRemappings RemappingInfos;
			NewRawMesh.Compact(RemappingInfos);

			FTransform Transform = StaticMeshComponent->GetComponentTransform().Inverse();

			//Apply the inverse component transform
			MeshProcessingUtils::TransformRawMeshVertexData(Transform, NewRawMesh);

			TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = NewRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
			TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = NewRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);

			bool bHasAllNormals = true;
			bool bHasAllTangents = true;
			for (const FVertexInstanceID VertexInstanceID : NewRawMesh.VertexInstances().GetElementIDs())
			{
				bHasAllNormals &= !VertexInstanceNormals[VertexInstanceID].IsNearlyZero();
				bHasAllTangents &= !VertexInstanceTangents[VertexInstanceID].IsNearlyZero();
			}

			RawMesh->Empty();
			// Update mesh description of static mesh with new geometry
			FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
			//Copy the new mesh in the Original meshDescription hold from the staticmesh
			*MeshDescription = NewRawMesh;

			const FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;

			if (BuildSettings.bRecomputeNormals || !bHasAllNormals)
			{
				FMeshDescriptionOperations::CreateNormals(*MeshDescription,
														  FMeshDescriptionOperations::ETangentOptions::BlendOverlappingNormals,
														  !BuildSettings.bUseMikkTSpace && !bHasAllTangents);
			}

			if ((BuildSettings.bRecomputeTangents || !bHasAllTangents) && BuildSettings.bUseMikkTSpace)
			{
				FMeshDescriptionOperations::CreateMikktTangents(*MeshDescription,
																FMeshDescriptionOperations::ETangentOptions::BlendOverlappingNormals);
			}
			// TODO: Maybe add generation of lightmap UV here.

			//Commit the result so the old FRawMesh is updated
			//StaticMesh->CommitMeshDescription(0);
		}
	});

	for (UStaticMesh* StaticMesh : StaticMeshArray)
	{
		//Commit the result so the old FRawMesh is updated
		StaticMesh->CommitMeshDescription(0);
	}

	IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Deleting triangles took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	// Rebuilding meshes which have been truncated
	ProcessingStep = MeshBuilding / (float)MeshesToRebuild.Num();
	for (auto& MeshToRebuild : MeshesToRebuild)
	{
		UStaticMesh* StaticMesh = MeshToRebuild.Key;

		if (Progress.IsValid())
		{
			Progress->EnterProgressFrame(ProcessingStep, FText::FromString(TEXT("Building meshes ...")));
		}

		if (MeshToRebuild.Value.Key != nullptr)
		{
			FMeshBuildSettings CachedBuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
			BuildSettings.bRecomputeNormals = false;
			BuildSettings.bRecomputeTangents = false;
			StaticMesh->PreEditChange(nullptr);
			StaticMesh->CommitMeshDescription(0);
			StaticMesh->PostEditChange();
			BuildSettings.bRecomputeNormals = CachedBuildSettings.bRecomputeNormals;
			BuildSettings.bRecomputeTangents = CachedBuildSettings.bRecomputeTangents;
		}
	}

	IntermediateTime = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastTime);
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: Rebuilding static meshes took %.3f s."), IntermediateTime);
	LastTime = FPlatformTime::Cycles64();

	if (Progress.IsValid())
	{
		Progress->EnterProgressFrame(1.f, FText::FromString(TEXT("Updating level ...")));
	}

	for (FMeshMergeData& MergeData : Geometry)
	{
		MergeData.ReleaseData();
	}

#if defined(DEBUG_EXPORT_ENVELOP)
	{
		FSoftObjectPath SoftObjectPath(TEXT("/Game/jacketing.jacketing"));
		UStaticMesh* VolumeStaticMesh = Cast<UStaticMesh>(SoftObjectPath.TryLoad());

		if (VolumeStaticMesh)
		{
			FMeshDescription* MeshDescription = VolumeStaticMesh->GetMeshDescription(0);
			Volume->ConvertToRawMesh(*MeshDescription);
			// Update raw mesh with new geometry
			VolumeStaticMesh->PreEditChange(nullptr);
			VolumeStaticMesh->CommitMeshDescription(0);
			VolumeStaticMesh->PostEditChange();
		}
	}
#endif

	// Delete actors
	if (OccludedActorSet.Num() > 0)
	{
		UWorld* World = GEditor->GetEditorWorldContext(false).World();
		ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();

		for (AActor* Actor : OccludedActorSet)
		{
			//To avoid dangling gizmo after actor has been destroyed
			if (Actor->IsSelected())
			{
				GEditor->SelectActor(Actor, false, true);
			}

			LayersSubsystem->DisassociateActorFromLayers(Actor);

			if (!World->DestroyActor(Actor, false, true))
			{
				UE_LOG(LogMeshProcessingLibrary, Error, TEXT("AssemblyJacketing: Cannot delete Actor %s."), *Actor->GetActorLabel());
			}
		}

		World->BroadcastLevelsChanged();
	}

	// Log time spent to perform jacketing process
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int32 ElapsedMin = int32(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG(LogMeshProcessingLibrary, Log, TEXT("AssemblyJacketing: took %d min %.3f s. %d occluded components out of %d, %d polygons deleted out of %d"),
																ElapsedMin, ElapsedSeconds, OccludedComponentCount, StaticMeshComponents.Num(), DeletedPolygonCount, PolygonCount);
}
#else
void UMeshProcessingLibrary::ApplyJacketingOnMeshActors(const TArray<AActor*>&, const UJacketingOptions*, TArray<AActor*>&, bool) {}
#endif // WITH_PROXYLOD


UMeshProcessingEnterpriseSettings::UMeshProcessingEnterpriseSettings()
	: OverrideUndoBufferSize(128)
{
}

#undef LOCTEXT_NAMESPACE
