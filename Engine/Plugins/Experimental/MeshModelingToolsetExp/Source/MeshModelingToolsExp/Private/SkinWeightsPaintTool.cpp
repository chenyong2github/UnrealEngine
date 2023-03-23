// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinWeightsPaintTool.h"
#include "Engine/SkeletalMesh.h"
#include "InteractiveToolManager.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalDebugRendering.h"
#include "Math/UnrealMathUtility.h"
#include "Components/SkeletalMeshComponent.h"

#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ModelingToolTargetUtil.h"
#include "ToolBuilderUtil.h"

#include "MeshDescription.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "Parameterization/MeshLocalParam.h"
#include "Spatial/FastWinding.h"
#include "Async/Async.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkinWeightsPaintTool)

#define LOCTEXT_NAMESPACE "USkinWeightsPaintTool"

using namespace SkinPaintTool;

// thread pool to use for async operations
static EAsyncExecution SkinPaintToolAsyncExecTarget = EAsyncExecution::ThreadPool;

// any weight below this value is ignored, since it won't be representable in unsigned 16-bit precision
constexpr float MinimumWeightThreshold = 1.0f / 65535.0f;

USkinWeightsPaintToolProperties::USkinWeightsPaintToolProperties(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
{
	ColorRamp.Add(FLinearColor::Blue);
	ColorRamp.Add(FLinearColor::Yellow);

	MinColor = FLinearColor::Black;
	MaxColor = FLinearColor::White;
}

void FMultiBoneWeightEdits::MergeSingleEdit(
	const int32 BoneIndex,
	const int32 VertexID,
	const float OldWeight,
	const float NewWeight)
{
	FSingleBoneWeightEdits& BoneWeightEdit = PerBoneWeightEdits.FindOrAdd(BoneIndex);
	BoneWeightEdit.BoneIndex = BoneIndex;
	BoneWeightEdit.NewWeights.Add(VertexID, NewWeight);
	BoneWeightEdit.OldWeights.FindOrAdd(VertexID, OldWeight);
}

void FMultiBoneWeightEdits::MergeEdits(const FSingleBoneWeightEdits& BoneWeightEdits)
{
	// make sure bone has an entry in the map of weight edits
	const int32 BoneIndex = BoneWeightEdits.BoneIndex;
	PerBoneWeightEdits.FindOrAdd(BoneIndex);
	PerBoneWeightEdits[BoneIndex].BoneIndex = BoneIndex;
	
	for (const TTuple<int32, float>& NewWeight : BoneWeightEdits.NewWeights)
	{
		int32 VertexIndex = NewWeight.Key;
		PerBoneWeightEdits[BoneIndex].NewWeights.Add(VertexIndex, NewWeight.Value);
		PerBoneWeightEdits[BoneIndex].OldWeights.FindOrAdd(VertexIndex, BoneWeightEdits.OldWeights[VertexIndex]);
	}
}

float FMultiBoneWeightEdits::GetVertexDeltaFromEdits(const int32 BoneIndex, const int32 VertexIndex)
{
	PerBoneWeightEdits.FindOrAdd(BoneIndex);
	if (const float* NewVertexWeight = PerBoneWeightEdits[BoneIndex].NewWeights.Find(VertexIndex))
	{
		return *NewVertexWeight - PerBoneWeightEdits[BoneIndex].OldWeights[VertexIndex];
	}

	return 0.0f;
}

void FSkinToolDeformer::Initialize(const USkeletalMeshComponent* SkeletalMeshComponent, const FMeshDescription* Mesh)
{
	// get all bone transforms in the reference pose store a copy in component space
	Component = SkeletalMeshComponent;
	const FReferenceSkeleton& RefSkeleton = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform> &LocalSpaceBoneTransforms = RefSkeleton.GetRefBonePose();
	const int32 NumBones = LocalSpaceBoneTransforms.Num();
	InvCSRefPoseTransforms.SetNumUninitialized(NumBones);
	for (int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
	{
		const int32 ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FTransform& LocalTransform = LocalSpaceBoneTransforms[BoneIndex];
		if (ParentBoneIndex != INDEX_NONE)
		{
			InvCSRefPoseTransforms[BoneIndex] = LocalTransform * InvCSRefPoseTransforms[ParentBoneIndex];
		}
		else
		{
			InvCSRefPoseTransforms[BoneIndex] = LocalTransform;
		}
	}
	
	for (int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
	{
		// pre-invert the transforms so we don't have to at runtime
		InvCSRefPoseTransforms[BoneIndex] = InvCSRefPoseTransforms[BoneIndex].Inverse();

		// store map of bone indices to bone names
		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		BoneNames.Add(BoneName);
		BoneNameToIndexMap.Add(BoneName, BoneIndex);
	}

	// store reference pose vertex positions
	const TArrayView<const UE::Math::TVector<float>> VertexPositions = Mesh->GetVertexPositions().GetRawArray();
	RefPoseVertexPositions = VertexPositions;

	// force all vertices to be updated initially
	VerticesWithModifiedWeights.Empty(RefPoseVertexPositions.Num());
	for (int32 VertexID=0; VertexID<RefPoseVertexPositions.Num(); ++VertexID)
	{
		VerticesWithModifiedWeights.Add(VertexID);
	}
}

void FSkinToolDeformer::UpdateVertexDeformation(USkinWeightsPaintTool* Tool)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateDeformationTotal);
	
	if (VerticesWithModifiedWeights.IsEmpty())
	{
		return;
	}
	
	// update vertex positions
	UPreviewMesh* PreviewMesh = Tool->PreviewMesh;
	const TArray<VertexWeights>& CurrentWeights = Tool->Weights.CurrentWeights;
	PreviewMesh->DeferredEditMesh([this, &CurrentWeights](FDynamicMesh3& Mesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateDeformation);
		const TArray<FTransform>& CurrentBoneTransforms = Component->GetComponentSpaceTransforms();
		const TArray<int32> VertexIndices = VerticesWithModifiedWeights.Array();
		
		ParallelFor( VerticesWithModifiedWeights.Num(), [this, &VertexIndices, &Mesh, &CurrentBoneTransforms, &CurrentWeights](int32 Index)
		{
			const int32 VertexID = VertexIndices[Index];
			FVector VertexNewPosition = FVector::ZeroVector;
			const VertexWeights& VertexPerBoneData = CurrentWeights[VertexID];
			for (const FVertexBoneWeight& VertexData : VertexPerBoneData)
			{
				const FTransform& CurrentTransform = CurrentBoneTransforms[VertexData.BoneIndex];
				VertexNewPosition += CurrentTransform.TransformPosition(VertexData.VertexInBoneSpace) * VertexData.Weight;
			}
			
			Mesh.SetVertex(VertexID, VertexNewPosition, false);
		});
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::Positions, false);

	// update vertex acceleration structure
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateVertexOctree);
		Tool->VerticesOctree.RemoveVertices(VerticesWithModifiedWeights);
		Tool->VerticesOctree.InsertVertices(VerticesWithModifiedWeights);
	}

	// update triangle acceleration structure
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateTriangleOctree);

		// ensure previous async update is finished before queuing the next one...
		Tool->TriangleOctreeFuture.Wait();

		TArray<int32>& TrianglesToReinsert = Tool->TriangleToReinsert;
		const UE::Geometry::FAxisAlignedBox3d QueryBox(Tool->StampLocalPos, Tool->CurrentBrushRadius);
		Tool->TrianglesOctree.RangeQuery(QueryBox, TrianglesToReinsert);
		UE::Geometry::FDynamicMeshOctree3& OctreeToUpdate = Tool->TrianglesOctree;
		Tool->TriangleOctreeFuture = Async(SkinPaintToolAsyncExecTarget, [&OctreeToUpdate, &TrianglesToReinsert]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::TriangleOctreeReinsert);	
			OctreeToUpdate.ReinsertTriangles(TrianglesToReinsert);
		});
	}

	// empty queue of vertices to update
	VerticesWithModifiedWeights.Reset();
}

void FSkinToolDeformer::SetVertexNeedsUpdated(int32 VertexIndex)
{
	VerticesWithModifiedWeights.Add(VertexIndex);
}

void FSkinToolWeights::InitializeSkinWeights(
	const USkeletalMeshComponent* SkeletalMeshComponent,
	FMeshDescription* Mesh)
{
	// initialize deformer data
	Deformer.Initialize(SkeletalMeshComponent, Mesh);

	// initialize current weights (using compact format: num_verts * max_influences)
	const FSkeletalMeshConstAttributes MeshAttribs(*Mesh);
	const FSkinWeightsVertexAttributesConstRef VertexSkinWeights = MeshAttribs.GetVertexSkinWeights();
	const int32 NumVertices = Mesh->Vertices().Num();
	CurrentWeights.SetNum(NumVertices);
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		const FVertexID VertexID(VertexIndex);
		int32 InfluenceIndex = 0;
		for (UE::AnimationCore::FBoneWeight BoneWeight: VertexSkinWeights.Get(VertexID))
		{
			check(InfluenceIndex < MAX_TOTAL_INFLUENCES);
			const int32 BoneIndex = BoneWeight.GetBoneIndex();
			const float Weight = BoneWeight.GetWeight();
			const FVector& RefPoseVertexPosition = Deformer.RefPoseVertexPositions[VertexIndex];
			const FTransform& InvRefPoseTransform = Deformer.InvCSRefPoseTransforms[BoneIndex];
			const FVector& BoneLocalPositionInRefPose = InvRefPoseTransform.TransformPosition(RefPoseVertexPosition);
			CurrentWeights[VertexIndex].Emplace(BoneIndex, BoneLocalPositionInRefPose, Weight);
			++InfluenceIndex;
		}
	}
	
	// maintain duplicate weight map
	PreStrokeWeights = CurrentWeights;

	// maintain relax-per stroke map
	MaxFalloffPerVertexThisStroke.SetNumZeroed(NumVertices);
}

void FSkinToolWeights::EditVertexWeightAndNormalize(
	const FName BoneToHoldConstant,
	const int32 VertexID,
	const float NewWeightValue,
	FMultiBoneWeightEdits& WeightEdits)
{
	const int32 BoneToHoldIndex = Deformer.BoneNameToIndexMap[BoneToHoldConstant];
	
	// calculate the sum of all the weights on this vertex (not including the one we currently applied)
	TArray<int32> BonesAffectingVertex;
	TArray<float> ValuesToNormalize;
	float Total = 0.0f;
	const VertexWeights& VertexData = CurrentWeights[VertexID];
	for (const FVertexBoneWeight& VertexBoneData : VertexData)
	{
		if (VertexBoneData.BoneIndex == BoneToHoldIndex)
		{
			continue;
		}
		
		if (VertexBoneData.Weight < MinimumWeightThreshold)
		{
			continue;
		}
		
		BonesAffectingVertex.Add(VertexBoneData.BoneIndex);
		ValuesToNormalize.Add(VertexBoneData.Weight);
		Total += VertexBoneData.Weight;
	}

	// if user applied FULL weight to this vertex OR there's no other weights of any significance,
	// then simply set everything else to zero and return
	if (NewWeightValue >= (1.0f - MinimumWeightThreshold) || Total <= MinimumWeightThreshold)
	{
		// set all other influences to 0.0f
		for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
		{
			const int32 BoneIndex = BonesAffectingVertex[i];
			const float OldWeight = ValuesToNormalize[i];
			constexpr float NewWeight = 0.0f;
			WeightEdits.MergeSingleEdit(BoneIndex, VertexID, OldWeight, NewWeight);
		}

		// set current bone value to 1.0f
		const float PrevWeight = GetWeightOfBoneOnVertex(BoneToHoldIndex, VertexID, PreStrokeWeights);
		WeightEdits.MergeSingleEdit(
			BoneToHoldIndex,
			VertexID,
			PrevWeight,
			1.0f);
		
		return;
	}

	// calculate amount we have to spread across the other bones affecting this vertex
	const float AvailableTotal = 1.0f - NewWeightValue;

	// normalize weights into available space not set by current bone
	for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
	{
		float NormalizedValue = 0.f;
		if (AvailableTotal > MinimumWeightThreshold && Total > KINDA_SMALL_NUMBER)
		{
			NormalizedValue = (ValuesToNormalize[i] / Total) * AvailableTotal;	
		}
		const int32 BoneIndex = BonesAffectingVertex[i];
		const float OldWeight = ValuesToNormalize[i];
		const float NewWeight = NormalizedValue;
		WeightEdits.MergeSingleEdit(BoneIndex, VertexID, OldWeight, NewWeight);
	}

	// record current bone edit
	const float PrevWeight = GetWeightOfBoneOnVertex(BoneToHoldIndex, VertexID, PreStrokeWeights);
	WeightEdits.MergeSingleEdit(
		BoneToHoldIndex,
		VertexID,
		PrevWeight,
		NewWeightValue);
}

void FSkinToolWeights::ApplyCurrentWeightsToMeshDescription(FMeshDescription* EditedMesh)
{
	FSkeletalMeshAttributes MeshAttribs(*EditedMesh);
	FSkinWeightsVertexAttributesRef VertexSkinWeights = MeshAttribs.GetVertexSkinWeights();
	
	UE::AnimationCore::FBoneWeightsSettings Settings;
	Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::None);

	TArray<UE::AnimationCore::FBoneWeight> SourceBoneWeights;
	SourceBoneWeights.Reserve(UE::AnimationCore::MaxInlineBoneWeightCount);

	const int32 NumVertices = EditedMesh->Vertices().Num();
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		SourceBoneWeights.Reset();

		const VertexWeights& VertexWeights = CurrentWeights[VertexIndex];
		for (const FVertexBoneWeight& SingleBoneWeight : VertexWeights)
		{
			SourceBoneWeights.Add(UE::AnimationCore::FBoneWeight(SingleBoneWeight.BoneIndex, SingleBoneWeight.Weight));
		}

		VertexSkinWeights.Set(FVertexID(VertexIndex), UE::AnimationCore::FBoneWeights::Create(SourceBoneWeights, Settings));
	}
}

float FSkinToolWeights::GetWeightOfBoneOnVertex(
	const int32 BoneIndex,
	const int32 VertexID,
	const TArray<VertexWeights>& InVertexWeights)
{
	const VertexWeights& VertexWeights = InVertexWeights[VertexID];
	for (const FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		if (BoneWeight.BoneIndex == BoneIndex)
		{
			return BoneWeight.Weight;
		}
	}

	return 0.f;
}

void FSkinToolWeights::SetWeightOfBoneOnVertex(
	const int32 BoneIndex,
	const int32 VertexID,
	const float Weight,
	TArray<VertexWeights>& InOutVertexWeights)
{
	Deformer.SetVertexNeedsUpdated(VertexID);
	
	// incoming weights are assumed to be normalized already, so set it directly
	VertexWeights& VertexWeights = InOutVertexWeights[VertexID];
	for (FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		if (BoneWeight.BoneIndex == BoneIndex)
		{
			BoneWeight.Weight = Weight;
			return;
		}
	}

	// bone not already an influence on this vertex, so we need to add it..

	// if vertex has room for more influences, then simply add it
	if (VertexWeights.Num() < UE::AnimationCore::MaxInlineBoneWeightCount)
	{
		// add a new influence to this vertex
		const FVector PosLocalToBone = Deformer.InvCSRefPoseTransforms[BoneIndex].TransformPosition(Deformer.RefPoseVertexPositions[VertexID]);
		VertexWeights.Emplace(BoneIndex, PosLocalToBone, Weight);
		return;
	}

	//
	// uh oh, we're out of room for more influences on this vertex, so lets kick the smallest influence to make room
	//

	// find the smallest influence
	float SmallestInfluence = TNumericLimits<float>::Max();
	int32 SmallestInfluenceIndex = INDEX_NONE;
	for (int32 InfluenceIndex=0; InfluenceIndex<VertexWeights.Num(); ++InfluenceIndex)
	{
		const FVertexBoneWeight& BoneWeight = VertexWeights[InfluenceIndex];
		if (BoneWeight.Weight <= SmallestInfluence)
		{
			SmallestInfluence = BoneWeight.Weight;
			SmallestInfluenceIndex = InfluenceIndex;
		}
	}

	// replace smallest influence
	FVertexBoneWeight& BoneWeightToReplace = VertexWeights[SmallestInfluenceIndex];
	BoneWeightToReplace.Weight = Weight;
	BoneWeightToReplace.BoneIndex = BoneIndex;
	BoneWeightToReplace.VertexInBoneSpace = Deformer.InvCSRefPoseTransforms[BoneIndex].TransformPosition(Deformer.RefPoseVertexPositions[VertexID]);

	// now we need to re-normalize because the stamp does not handle maximum influences
	float TotalWeight = 0.f;
	for (const FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		TotalWeight += BoneWeight.Weight;
	}
	for (FVertexBoneWeight& BoneWeight : VertexWeights)
	{
		BoneWeight.Weight /= TotalWeight;
	}
}

void FSkinToolWeights::ResetAfterStroke()
{
	PreStrokeWeights = CurrentWeights;

	for (int32 i=0; i<MaxFalloffPerVertexThisStroke.Num(); ++i)
	{
		MaxFalloffPerVertexThisStroke[i] = 0.f;
	}
}

float FSkinToolWeights::SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStrength)
{
	float& MaxFalloffThisStroke = MaxFalloffPerVertexThisStroke[VertexID];
	if (MaxFalloffThisStroke < CurrentStrength)
	{
		MaxFalloffThisStroke = CurrentStrength;
	}
	return MaxFalloffThisStroke;
}

void FSkinToolWeights::ApplyEditsToWeightMap(
	const FMultiBoneWeightEdits& Edits, 
	TArray<VertexWeights>& InOutWeights)
{
	for (const TTuple<BoneIndex, FSingleBoneWeightEdits>& BoneWeightEdits : Edits.PerBoneWeightEdits)
	{
		const FSingleBoneWeightEdits& WeightEdits = BoneWeightEdits.Value;
		const int32 BoneIndex = WeightEdits.BoneIndex;
		for (const TTuple<int32, float>& NewWeight : WeightEdits.NewWeights)
		{
			const int32 VertexID = NewWeight.Key;
			const float Weight = NewWeight.Value;
			SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, InOutWeights);
		}
	}
}

void FMeshSkinWeightsChange::Apply(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);
	
	for (TTuple<int32, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
	{
		Tool->ExternalUpdateWeights(Pair.Key, Pair.Value.NewWeights);
	}
}

void FMeshSkinWeightsChange::Revert(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);

	for (TTuple<int32, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
	{
		Tool->ExternalUpdateWeights(Pair.Key, Pair.Value.OldWeights);
	}
}

void FMeshSkinWeightsChange::AddBoneWeightEdit(const FSingleBoneWeightEdits& BoneWeightEdit)
{
	AllWeightEdits.MergeEdits(BoneWeightEdit);
}

/*
 * ToolBuilder
 */

UMeshSurfacePointTool* USkinWeightsPaintToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<USkinWeightsPaintTool>(SceneState.ToolManager);
}

void USkinWeightsPaintTool::Setup()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::Setup);
	
	UDynamicMeshBrushTool::Setup();

	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	check(TargetComponent);
	const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(TargetComponent->GetOwnerComponent());
	check(Component && Component->GetSkeletalMeshAsset())

	// create a mesh description for editing (this must be done before calling UpdateBonePositionInfos) 
	EditedMesh = MakeUnique<FMeshDescription>();
	*EditedMesh = *UE::ToolTarget::GetMeshDescription(Target);

	// initialize the tool properties
	BrushProperties->RestoreProperties(this); // hides strength and falloff
	
	ToolProps = NewObject<USkinWeightsPaintToolProperties>(this);
	ToolProps->RestoreProperties(this);
	ToolProps->SkeletalMesh = Component->GetSkeletalMeshAsset();
	AddToolPropertySource(ToolProps);
	// attach callback to be informed when tool properties are modified
	ToolProps->GetOnModified().AddUObject(this, &USkinWeightsPaintTool::OnToolPropertiesModified);

	// default to the root bone as current bone
	PendingCurrentBone = CurrentBone = ToolProps->SkeletalMesh->GetRefSkeleton().GetBoneName(0);

	// configure preview mesh
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);
	PreviewMesh->EnableWireframe(true);
	PreviewMesh->SetShadowsEnabled(false);
	// enable vtx colors on preview mesh
	PreviewMesh->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->DisablePrimaryColors();
		Mesh.Attributes()->EnablePrimaryColors();
		// Create an overlay that has no split elements, init with zero value.
		Mesh.Attributes()->PrimaryColors()->CreateFromPredicate([](int ParentVID, int TriIDA, int TriIDB){return true;}, 0.f);
	});
	UMaterialInterface* VtxColorMaterial = GetToolManager()->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	if (VtxColorMaterial != nullptr)
	{
		PreviewMesh->SetOverrideRenderMaterial(VtxColorMaterial);
	}

	// build octree for vertices
	VerticesOctree.Initialize(PreviewMesh->GetMesh(), true);

	// build octree for triangles
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::InitTriangleOctree);
		
		TriangleOctreeFuture = Async(SkinPaintToolAsyncExecTarget, [&]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::InitTriangleOctreeRun);
			TrianglesOctree.Initialize(PreviewMesh->GetMesh());
		});
	}

	// initialize weight maps and deformation data
	Weights.InitializeSkinWeights(Component, EditedMesh.Get());
	bVisibleWeightsValid = false;

	RecalculateBrushRadius();
	
	// inform user of tool keys
	// TODO talk with UX team about viewport overlay to show hotkeys
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartSkinWeightsPaint", "Paint per-bone skin weights. [ and ] change brush size, Ctrl to Erase/Subtract, Shift to Smooth"),
		EToolMessageLevel::UserNotification);
}

void USkinWeightsPaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	UDynamicMeshBrushTool::RegisterActions(ActionSet);
}

void USkinWeightsPaintTool::OnTick(float DeltaTime)
{
	if (bStampPending)
	{
		ApplyStamp(LastStamp);
		bStampPending = false;
	}

	if (PendingCurrentBone.IsSet())
	{
		UpdateCurrentBone(*PendingCurrentBone);
		PendingCurrentBone.Reset();
	}

	if (bVisibleWeightsValid == false || ToolProps->bColorModeChanged)
	{
		UpdateCurrentBoneVertexColors();
		bVisibleWeightsValid = true;
		ToolProps->bColorModeChanged = false;
	}

	// sparsely updates vertex positions (only on vertices with modified weights)
	Weights.Deformer.UpdateVertexDeformation(this);
}

bool USkinWeightsPaintTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	// do not query the triangle octree until all async ops are finished
	TriangleOctreeFuture.Wait();
	
	// put ray in local space of skeletal mesh component
	// currently no way to transform skeletal meshes in the editor,
	// but at some point in the future we may add the ability to move parts around
	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	const FTransform3d CurTargetTransform(TargetComponent->GetWorldTransform());
	FRay3d LocalRay(
		CurTargetTransform.InverseTransformPosition((FVector3d)Ray.Origin),
		CurTargetTransform.InverseTransformVector((FVector3d)Ray.Direction));
	UE::Geometry::Normalize(LocalRay.Direction);
	
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

	FViewCameraState StateOut;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
	FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition((FVector3d)StateOut.Position));
	const int32 TriID = TrianglesOctree.FindNearestHitObject(
		LocalRay,
		[this, Mesh, &LocalEyePosition](int TriangleID)
	{
		FVector3d Normal, Centroid;
		double Area;
		Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
		return Normal.Dot((Centroid - LocalEyePosition)) < 0;
	});
	
	if (TriID != IndexConstants::InvalidID)
	{	
		FastTriWinding::FTriangle3d Triangle;
		Mesh->GetTriVertices(TriID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		UE::Geometry::FIntrRay3Triangle3d Query(LocalRay, Triangle);
		Query.Find();

		StampLocalPos = LocalRay.PointAt(Query.RayParameter);
		TriangleUnderStamp = TriID;

		OutHit.FaceIndex = TriID;
		OutHit.Distance = Query.RayParameter;
		OutHit.Normal = CurTargetTransform.TransformVector(Mesh->GetTriNormal(TriID));
		OutHit.ImpactPoint = CurTargetTransform.TransformPosition(StampLocalPos);
		return true;
	}
	
	return false;
}

void USkinWeightsPaintTool::OnBeginDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnBeginDrag(WorldRay);

	if (IsInBrushStroke())
	{
		bInvertStroke = GetCtrlToggle();
		bSmoothStroke = GetShiftToggle();
		BeginChange();
		StartStamp = UBaseBrushTool::LastBrushStamp;
		LastStamp = StartStamp;
		bStampPending = true;
	}
}

void USkinWeightsPaintTool::OnUpdateDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnUpdateDrag(WorldRay);
	if (IsInBrushStroke())
	{
		LastStamp = UBaseBrushTool::LastBrushStamp;
		bStampPending = true;
	}
}

void USkinWeightsPaintTool::OnEndDrag(const FRay& Ray)
{
	UDynamicMeshBrushTool::OnEndDrag(Ray);

	bInvertStroke = false;
	bSmoothStroke = false;
	bStampPending = false;

	Weights.ResetAfterStroke();

	// close change record
	TUniquePtr<FMeshSkinWeightsChange> Change = EndChange();

	GetToolManager()->BeginUndoTransaction(LOCTEXT("BoneWeightValuesChange", "Paint"));

	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("BoneWeightValuesChange", "Paint"));

	GetToolManager()->EndUndoTransaction();
}

bool USkinWeightsPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UDynamicMeshBrushTool::OnUpdateHover(DevicePos);
	return true;
}

void USkinWeightsPaintTool::CalculateVertexROI(
	const FBrushStampData& Stamp,
	TArray<int32>& VertexROI,
	TArray<float>& VertexSqDistances) const
{
	using namespace UE::Geometry;

	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::CalculateVertexROI);
	
	if (ToolProps->FalloffMode == EWeightBrushFalloffMode::Volume)
	{
		const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
		const FTransform3d Transform(TargetComponent->GetWorldTransform());
		const FVector3d StampPosLocal = Transform.InverseTransformPosition((FVector3d)Stamp.WorldPosition);
		const float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
		const FAxisAlignedBox3d QueryBox(StampPosLocal, CurrentBrushRadius);
		VerticesOctree.RangeQuery(QueryBox,
			[&](int32 VertexID) { return FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal) < RadiusSqr; },
			VertexROI);

		for (const int32 VertexID : VertexROI)
		{
			VertexSqDistances.Add(FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal));
		}
		
		return;
	}

	if (ToolProps->FalloffMode == EWeightBrushFalloffMode::Surface)
	{
		// get coordinate frame from stamp
		auto GetFrameFromStamp = [](const FBrushStampData& InStamp) -> FFrame3d
		{
			const FVector3d Origin = InStamp.WorldPosition;
			const FVector3d Normal = InStamp.WorldNormal;
			FVector3d NonCollinear = Normal;
			// get a guaranteed non collinear vector to the normal
			// doesn't matter where in the plane, stamp is radially symmetric
			do 
			{
				NonCollinear.X = FMath::RandRange(-1.0f, 1.0f);
				NonCollinear.Y = FMath::RandRange(-1.0f, 1.0f);
				NonCollinear.Z = FMath::RandRange(-1.0f, 1.0f);
				NonCollinear.Normalize();
					
			} while (FMath::Abs(NonCollinear.Dot(Normal)) > 0.8f);

			const FVector3d Plane = Normal.Cross(NonCollinear);
			const FVector3d Cross = Plane.Cross(Normal);
			return FFrame3d(Origin, Cross, Plane, Normal);
				
		};
		const FFrame3d SeedFrame = GetFrameFromStamp(Stamp);
			
		// create the ExpMap generator, computes vertex polar coordinates in a plane tangent to the surface
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
		TMeshLocalParam<FDynamicMesh3> Param(Mesh);
		Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
		const FIndex3i TriVerts = Mesh->GetTriangle(TriangleUnderStamp);
		Param.ComputeToMaxDistance(SeedFrame, TriVerts, Stamp.Radius);
		{
			// store vertices under the brush and their distances from the stamp
			const float StampRadSq = FMath::Pow(Stamp.Radius, 2);
			for (int32 VertexID : Mesh->VertexIndicesItr())
			{
				if (!Param.HasUV(VertexID))
				{
					continue;
				}
				
				FVector2d UV = Param.GetUV(VertexID);
				const float DistSq = UV.SizeSquared();
				if (DistSq >= StampRadSq)
				{
					continue;
				}
				
				VertexSqDistances.Add(DistSq);
				VertexROI.Add(VertexID);
			}
		}
		
		return;
	}
	
	checkNoEntry();
}

FVector4f USkinWeightsPaintTool::WeightToColor(float Value) const
{
	// optional greyscale mode
	if (ToolProps->ColorMode == EWeightColorMode::Greyscale)
	{
		return FLinearColor::LerpUsingHSV(FLinearColor::Black, FLinearColor::White, Value);	
	}
	
	// early out zero weights to min color
	if (Value <= MinimumWeightThreshold)
	{
		return ToolProps->MinColor;
	}

	// early out full weights to max color
	if (FMath::IsNearlyEqual(Value, 1.0f))
	{
		return ToolProps->MaxColor;
	}

	// get user-specified color ramp for intermediate colors
	const TArray<FLinearColor>& Colors = ToolProps->ColorRamp;
	
	// revert back to simple Lerp(min,max) if user supplied color ramp doesn't have enough colors
	if (Colors.Num() < 2)
	{
		const FLinearColor FinalColor = FLinearColor::LerpUsingHSV(ToolProps->MinColor, ToolProps->MaxColor, Value);
		return UE::Geometry::ToVector4<float>(FinalColor);
	}

	// otherwise, interpolate within two nearest ramp colors
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	const float PerColorRange = 1.0f / (Colors.Num() - 1);
	const int ColorIndex = static_cast<int>(Value / PerColorRange);
	const float RangeStart = ColorIndex * PerColorRange;
	const float RangeEnd = (ColorIndex + 1) * PerColorRange;
	const float Param = (Value - RangeStart) / (RangeEnd - RangeStart);
	const FLinearColor& StartColor = Colors[ColorIndex];
	const FLinearColor& EndColor = Colors[ColorIndex+1];
	const FLinearColor FinalColor = FLinearColor::LerpUsingHSV(StartColor, EndColor, Param);
	return UE::Geometry::ToVector4<float>(FinalColor);
}


void USkinWeightsPaintTool::UpdateCurrentBoneVertexColors()
{
	const int32 CurrentBoneIndex = Weights.Deformer.BoneNameToIndexMap[CurrentBone];
	
	// update mesh with new value colors
	PreviewMesh->DeferredEditMesh([this, &CurrentBoneIndex](FDynamicMesh3& Mesh)
	{
		const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(Mesh);
		UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
		for (const int32 ElementId : ColorOverlay->ElementIndicesItr())
		{
			const int32 VertexID = ColorOverlay->GetParentVertex(ElementId);	
			const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
			const float Value = Weights.GetWeightOfBoneOnVertex(CurrentBoneIndex, SrcVertexID, Weights.CurrentWeights);
			const FVector4f Color(WeightToColor(Value));
			ColorOverlay->SetElement(ElementId, Color);
		}
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}

double USkinWeightsPaintTool::CalculateBrushFalloff(double Distance) const
{
	const double f = FMathd::Clamp(1.0 - BrushProperties->BrushFalloffAmount, 0.0, 1.0);
	double d = Distance / CurrentBrushRadius;
	double w = 1;
	if (d > f)
	{
		d = FMathd::Clamp((d - f) / (1.0 - f), 0.0, 1.0);
		w = (1.0 - d * d);
		w = w * w * w;
	}
	return w;
}

void USkinWeightsPaintTool::ApplyStamp(const FBrushStampData& Stamp)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::ApplyStamp);
	
	// get the vertices under the brush, and their squared distances to the brush center
	// when using "Volume" brush, distances are straight line
	// when using "Surface" brush, distances are geodesics
	TArray<int32> VerticesInStamp;
	TArray<float> VertexSqDistances;
	CalculateVertexROI(Stamp, VerticesInStamp, VertexSqDistances);

	// gather sparse set of modifications made from this stamp, these edits are merged throughout
	// the lifetime of a single brush stroke in the "ActiveChange" allowing for undo/redo
	FMultiBoneWeightEdits WeightEditsFromStamp;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::EditWeightOfVerticesInStamp);
		// generate a weight edit from this stamp (includes modifications caused by normalization)
		if (bSmoothStroke || ToolProps->BrushMode == EBrushBehaviorMode::Relax)
		{
			// use mesh topology to iteratively smooth weights across neighboring vertices
			RelaxWeightOnVertices(VerticesInStamp, VertexSqDistances, WeightEditsFromStamp);
		}
		else
		{
			// edit weight; either by "Add", "Remove", "Replace", "Multiply"
			EditWeightOfVerticesInStamp(
				ToolProps->BrushMode,
				VerticesInStamp,
				VertexSqDistances,
				WeightEditsFromStamp);
		}
	}
	
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::ApplyWeightEditsToActiveChange);
		// store weight edits from all stamps made during a single stroke (1 transaction per stroke)
		for (const TTuple<int32, FSingleBoneWeightEdits>& BoneWeightEdits : WeightEditsFromStamp.PerBoneWeightEdits)
		{
			ActiveChange->AddBoneWeightEdit(BoneWeightEdits.Value);
		}
	}
	
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::ApplyWeightEditsToCurrentWeights);
		// apply weights to current weights
		Weights.ApplyEditsToWeightMap(WeightEditsFromStamp, Weights.CurrentWeights);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkinTool::UpdateVertexColors);
		// update vertex colors
		PreviewMesh->DeferredEditMesh([&](FDynamicMesh3& Mesh)
		{
			TArray<int> ElementIds;
			UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
			const int32 BoneIndex = Weights.Deformer.BoneNameToIndexMap[CurrentBone];
			const int32 NumVerticesInStamp = VerticesInStamp.Num();
			for (int32 Index = 0; Index < NumVerticesInStamp; ++Index)
			{
				const int32 VertexID = VerticesInStamp[Index];
				const float Weight = Weights.GetWeightOfBoneOnVertex(BoneIndex, VertexID, Weights.CurrentWeights);
				FVector4f NewColor(WeightToColor(Weight));
				ColorOverlay->GetVertexElements(VertexID, ElementIds);
				for (const int32 ElementId : ElementIds)
				{
					ColorOverlay->SetElement(ElementId, NewColor);
				}
				ElementIds.Reset();
			}
		}, false);
		PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
	}
}

void USkinWeightsPaintTool::RelaxWeightOnVertices(
	TArray<int32> VerticesInStamp,
	TArray<float> VertexSqDistances,
	FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp)
{
	const FDynamicMesh3* CurrentMesh = PreviewMesh->GetMesh();
	const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(*CurrentMesh);

	auto NormalizeWeights = [](TMap<BoneIndex, float>& InOutWeights)
	{
		float TotalWeight = 0.f;
		for (const TTuple<BoneIndex, float>& Weight : InOutWeights)
		{
			TotalWeight += Weight.Value;
		}
		for (TTuple<BoneIndex, float>& Weight : InOutWeights)
		{
			Weight.Value /= TotalWeight;
		}
	};

	// for each vertex in the stamp...
	constexpr int32 AvgNumNeighbors = 8;
	using VertexNeighborWeights = TArray<float, TInlineAllocator<AvgNumNeighbors>>;
	TArray<int32> AllNeighborVertices;
	TMap<BoneIndex, VertexNeighborWeights> WeightsOnAllNeighbors;
	TMap<BoneIndex, float> FinalWeights;
	for (int32 Index = 0; Index < VerticesInStamp.Num(); ++Index)
	{
		const int32 VertexID = VerticesInStamp[Index];
		const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);

		// get list of all neighboring vertices, AND this vertex
		AllNeighborVertices.Reset();
		AllNeighborVertices.Add(VertexID);
		for (const int32 NeighborVertexID : CurrentMesh->VtxVerticesItr(SrcVertexID))
		{
			AllNeighborVertices.Add(NeighborVertexID);
		}

		// get all weights above a given threshold across ALL neighbors (including self)
		WeightsOnAllNeighbors.Reset();
		for (const int32 NeighborVertexID : AllNeighborVertices)
		{
			for (const FVertexBoneWeight& BoneWeight : Weights.PreStrokeWeights[NeighborVertexID])
			{
				if (BoneWeight.Weight > MinimumWeightThreshold)
				{
					VertexNeighborWeights& BoneWeights = WeightsOnAllNeighbors.FindOrAdd(BoneWeight.BoneIndex);
					BoneWeights.Add(BoneWeight.Weight);
				}
			}
		}

		// calculate single average weight of each bone on all the neighbors
		FinalWeights.Reset();
		for (const TTuple<BoneIndex, VertexNeighborWeights>& NeighborWeights : WeightsOnAllNeighbors)
		{
			float TotalWeightOnThisBone = 0.f;
			for (const float& Value : NeighborWeights.Value)
			{
				TotalWeightOnThisBone += Value;
			}
			FinalWeights.Add(NeighborWeights.Key, TotalWeightOnThisBone / NeighborWeights.Value.Num());
		}

		// normalize the weights
		NormalizeWeights(FinalWeights);

		// lerp weights from previous values, to fully relaxed values by brush strength scaled by falloff
		const float CurrentFalloff = static_cast<float>(CalculateBrushFalloff(FMath::Sqrt(VertexSqDistances[Index])));
		const float UseFalloff = Weights.SetCurrentFalloffAndGetMaxFalloffThisStroke(VertexID, CurrentFalloff);
		const float UseStrength = BrushProperties->BrushStrength * UseFalloff;
		for (TTuple<BoneIndex, float>& FinalWeight : FinalWeights)
		{
			const int32 BoneIndex = FinalWeight.Key;
			float NewWeight = FinalWeight.Value;
			float OldWeight = Weights.GetWeightOfBoneOnVertex(BoneIndex, VertexID, Weights.PreStrokeWeights);
			FinalWeight.Value = FMath::Lerp(OldWeight, NewWeight, UseStrength);
		}

		// normalize again
		NormalizeWeights(FinalWeights);

		// apply weight edits
		for (const TTuple<BoneIndex, float>& FinalWeight : FinalWeights)
		{
			// record an edit for this vertex, for this bone
			const int32 BoneIndex = FinalWeight.Key;
			const float NewWeight = FinalWeight.Value;
			const float OldWeight = Weights.GetWeightOfBoneOnVertex(BoneIndex, VertexID, Weights.PreStrokeWeights);
			AllBoneWeightEditsFromStamp.MergeSingleEdit(BoneIndex, VertexID, OldWeight, NewWeight);
		}
	}
}

void USkinWeightsPaintTool::EditWeightOfVerticesInStamp(
	EBrushBehaviorMode EditMode,
	const TArray<int32>& VerticesInStamp,
	const TArray<float>& VertexSqDistances,
	FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp)
{
	const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(*PreviewMesh->GetMesh());

	// invert brush strength differently depending on brush mode
	float UseStrength = BrushProperties->BrushStrength;
	switch (EditMode)
	{
	case EBrushBehaviorMode::Add:
		{
			UseStrength *= bInvertStroke ? -1.0f : 1.0f;
			break;
		}
	case EBrushBehaviorMode::Replace:
		{
			UseStrength = bInvertStroke ? 1.0f - UseStrength : UseStrength;
			break;
		}
	case EBrushBehaviorMode::Multiply:
		{
			UseStrength = bInvertStroke ? 1.0f + UseStrength : UseStrength;
			break;
		}		
	default:
		checkNoEntry();
	}
	
	// spin through the vertices in the stamp and store new weight values in NewValuesFromStamp
	// afterwards, these values are normalized while taking into consideration the user's desired changes
	const int32 CurrentBoneIndex = Weights.Deformer.BoneNameToIndexMap[CurrentBone];
	const int32 NumVerticesInStamp = VerticesInStamp.Num();
	for (int32 Index = 0; Index < NumVerticesInStamp; ++Index)
	{
		const int32 VertexID = VerticesInStamp[Index];
		const int32 SrcVertexID = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexID);
		const float CurrentFalloff = static_cast<float>(CalculateBrushFalloff(FMath::Sqrt(VertexSqDistances[Index])));
		const float UseFalloff = Weights.SetCurrentFalloffAndGetMaxFalloffThisStroke(SrcVertexID, CurrentFalloff);
		const float ValueBeforeStroke = Weights.GetWeightOfBoneOnVertex(CurrentBoneIndex, VertexID, Weights.PreStrokeWeights);

		// calculate new weight value
		float NewValueAfterStamp = ValueBeforeStroke;
		switch (EditMode)
		{
		case EBrushBehaviorMode::Add:
			{
				NewValueAfterStamp = ValueBeforeStroke + (UseStrength * UseFalloff);
				break;
			}
		case EBrushBehaviorMode::Replace:
			{
				NewValueAfterStamp = FMath::Lerp(ValueBeforeStroke, UseStrength, UseFalloff);
				break;
			}
		case EBrushBehaviorMode::Multiply:
			{
				const float DeltaFromThisStamp = ((ValueBeforeStroke * UseStrength) - ValueBeforeStroke) * UseFalloff;
				NewValueAfterStamp = ValueBeforeStroke + DeltaFromThisStamp;
				break;
			}
		default:
			checkNoEntry();
		}

		// normalize the values across all bones affecting the vertices in the stamp, and record the bone edits
		// normalization is done while holding all weights on the current bone constant so that user edits are not overwritten
		NewValueAfterStamp = FMath::Clamp(NewValueAfterStamp, 0.0f, 1.0f);
		Weights.EditVertexWeightAndNormalize(
			CurrentBone,
			VertexID,
			NewValueAfterStamp,
			AllBoneWeightEditsFromStamp);
	}
}

void USkinWeightsPaintTool::UpdateCurrentBone(const FName& BoneName)
{
	CurrentBone = BoneName;
	bVisibleWeightsValid = false;
}

void USkinWeightsPaintTool::OnShutdown(EToolShutdownType ShutdownType)
{
	BrushProperties->SaveProperties(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		// apply the weights to the mesh description
		Weights.ApplyCurrentWeightsToMeshDescription(EditedMesh.Get());

		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("SkinWeightsPaintTool", "Paint Skin Weights"));
		UE::ToolTarget::CommitMeshDescriptionUpdate(Target, EditedMesh.Get());
		GetToolManager()->EndUndoTransaction();
	}
}

void USkinWeightsPaintTool::BeginChange()
{
	ActiveChange = MakeUnique<FMeshSkinWeightsChange>();
}


TUniquePtr<FMeshSkinWeightsChange> USkinWeightsPaintTool::EndChange()
{
	return MoveTemp(ActiveChange);
}


void USkinWeightsPaintTool::ExternalUpdateWeights(const int32 BoneIndex, const TMap<int32, float>& NewValues)
{
	for (const TTuple<int32, float>& Pair : NewValues)
	{
		const int32 VertexID = Pair.Key;
		const float Weight = Pair.Value;
		Weights.SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, Weights.CurrentWeights);
		Weights.SetWeightOfBoneOnVertex(BoneIndex, VertexID, Weight, Weights.PreStrokeWeights);
	}

	const FName BoneName = Weights.Deformer.BoneNames[BoneIndex];
	if (BoneName == CurrentBone)
	{
		UpdateCurrentBoneVertexColors();
	}
}

void USkinWeightsPaintTool::HandleSkeletalMeshModified(const TArray<FName>& InBoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	const FName BoneName = InBoneNames.IsEmpty() ? NAME_None : InBoneNames[0];

	switch (InNotifyType)
	{
	case ESkeletalMeshNotifyType::BonesAdded:
		break;
	case ESkeletalMeshNotifyType::BonesRemoved:
		break;
	case ESkeletalMeshNotifyType::BonesMoved:
		break;
	case ESkeletalMeshNotifyType::BonesSelected:
		if (BoneName != NAME_None)
		{
			PendingCurrentBone = BoneName;
		}
		break;
	}
}

void USkinWeightsPaintTool::OnToolPropertiesModified(UObject* ModifiedObject, FProperty* ModifiedProperty)
{	
	// invalidate vertex color cache when weight color properties are modified
	const bool bColorModeModified = ModifiedProperty->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, ColorMode);
	const bool bColorRampModified = ModifiedProperty->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, ColorRamp);
	const bool bMinColorModified = ModifiedProperty->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, MinColor);
	const bool bMaxColorModified = ModifiedProperty->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(USkinWeightsPaintToolProperties, MaxColor);
	if (bColorModeModified || bColorRampModified || bMinColorModified || bMaxColorModified)
	{
		bVisibleWeightsValid = false;
	}
}


#undef LOCTEXT_NAMESPACE