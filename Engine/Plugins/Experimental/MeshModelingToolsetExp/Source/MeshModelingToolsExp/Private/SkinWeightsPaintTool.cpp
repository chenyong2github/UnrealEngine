// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinWeightsPaintTool.h"
#include "Engine/SkeletalMesh.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "SceneManagement.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalDebugRendering.h"
#include "Math/UnrealMathUtility.h"
#include "Components/SkeletalMeshComponent.h"

#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ModelingToolTargetUtil.h"

#include "MeshDescription.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "Parameterization/MeshLocalParam.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkinWeightsPaintTool)


#define LOCTEXT_NAMESPACE "USkinWeightsPaintTool"

// Any weight below this value is ignored, since it won't be representable in a uint8.
constexpr float MinimumWeightThreshold = 1.0f / 255.0f;

USkinWeightsPaintToolProperties::USkinWeightsPaintToolProperties(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
{
	ColorRamp.Add(FLinearColor::Blue);
	ColorRamp.Add(FLinearColor::Green);
	ColorRamp.Add(FLinearColor(FColor::Orange));
	ColorRamp.Add(FLinearColor::Red);

	MinColor = FLinearColor::Black;
	MaxColor = FLinearColor::White;
}

void FSkinToolDeformer::Initialize(
	const USkeletalMeshComponent* SkeletalMeshComponent,
	const FMeshDescription* Mesh)
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

		// store map of bone names to bone indices
		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		BoneNameToIndexMap.Add(BoneName, BoneIndex);
	}

	// store reference pose vertex positions
	const TArrayView<const UE::Math::TVector<float>> VertexPositions = Mesh->GetVertexPositions().GetRawArray();
	RefPoseVertexPositions = VertexPositions;

	// force all vertices to be updated initially
	VerticesWithModifiedWeights.Reserve(RefPoseVertexPositions.Num());
	for (int32 VertexID=0; VertexID<RefPoseVertexPositions.Num(); ++VertexID)
	{
		VerticesWithModifiedWeights.Add(VertexID);
	}
}

void FSkinToolDeformer::UpdateVertexDeformation(
	PerBoneWeightMap& Weights,
	UPreviewMesh* PreviewMesh,
	UE::Geometry::TDynamicVerticesOctree3<FDynamicMesh3>& OctreeToUpdate)
{
	if (VerticesWithModifiedWeights.IsEmpty())
	{
		return;
	}
	
	// update vertex positions
	PreviewMesh->EditMesh([this, &Weights](FDynamicMesh3& Mesh)
	{
		const TArray<FTransform>& CurrentBoneTransforms = Component->GetComponentSpaceTransforms();

		// accumulate new skinned position for each vertex that has had it's weight's modified
		for (const int32 VertexID : VerticesWithModifiedWeights)
		{
			FVector VertexNewPosition = FVector::ZeroVector;
			for (TTuple<FName, TArray<float>> Pair : Weights)
			{
				const float VertexWeight = Pair.Value[VertexID];
				if (VertexWeight <= MinimumWeightThreshold)
				{
					continue;
				}
				const FName BoneName = Pair.Key;
				const int32 BoneIndex = BoneNameToIndexMap[BoneName];
				const FVector& RefPoseVertexPosition = RefPoseVertexPositions[VertexID];
				const FTransform& InvRefPoseTransform = InvCSRefPoseTransforms[BoneIndex];
				const FVector& BoneLocalPositionInRefPose = InvRefPoseTransform.TransformPosition(RefPoseVertexPosition);
				const FTransform& CurrentTransform = CurrentBoneTransforms[BoneIndex];
				VertexNewPosition += CurrentTransform.TransformPosition(BoneLocalPositionInRefPose) * VertexWeight;
			}
			 
			Mesh.SetVertex(VertexID, VertexNewPosition, false);
		}
	});

	// update acceleration structure
	OctreeToUpdate.RemoveVertices(VerticesWithModifiedWeights);
	OctreeToUpdate.InsertVertices(VerticesWithModifiedWeights);

	// empty queue of vertices to update
	VerticesWithModifiedWeights.Reset();
}

void FSkinToolDeformer::SetVerticesNeedUpdated(TArray<int32> VertexIndices)
{
	for (int32 VertexID : VertexIndices)
	{
		VerticesWithModifiedWeights.Add(VertexID);
	}
}

void FSkinToolDeformer::SetVertexNeedsUpdated(int32 VertexIndex)
{
	VerticesWithModifiedWeights.Add(VertexIndex);
}

void FSkinToolWeights::InitializeSkinWeights(const FReferenceSkeleton& RefSkeleton, FMeshDescription* EditedMesh)
{
	const FSkeletalMeshConstAttributes MeshAttribs(*EditedMesh);
	const FSkinWeightsVertexAttributesConstRef VertexSkinWeights = MeshAttribs.GetVertexSkinWeights();
	const int32 NumVertices = EditedMesh->Vertices().Num();

	// Create a map of all bones to their per-vertex weights.
	CurrentWeightsMap.Reset();
	for (const FMeshBoneInfo& BoneInfo : RefSkeleton.GetRefBoneInfo())
	{
		CurrentWeightsMap.Add(BoneInfo.Name, {}).AddZeroed(NumVertices);
	}

	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		const FVertexID VertexID(VertexIndex);

		for (UE::AnimationCore::FBoneWeight BoneWeight: VertexSkinWeights.Get(VertexID))
		{
			FName BoneName = RefSkeleton.GetBoneName(static_cast<int32>(BoneWeight.GetBoneIndex()));
			const float Weight = BoneWeight.GetWeight();

			if (Weight >= MinimumWeightThreshold)
			{
				// If the source mesh has a bone that we don't recognize, we ignore it. It's
				// weight will get cleared when the new weights are updated back to the 
				// source mesh.
				TArray<float>* PerVertexWeights = CurrentWeightsMap.Find(BoneName);

				if (PerVertexWeights)
				{
					PerVertexWeights->GetData()[VertexIndex] = Weight;
				}
			}
		}
	}
	
	// maintain duplicate weight map
	PreStrokeWeightsMap = CurrentWeightsMap;

	// maintain relax-per stroke map
	MaxFalloffPerVertexThisStroke.SetNumZeroed(NumVertices);
}

TArray<float>* FSkinToolWeights::GetWeightsForBone(const FName BoneName)
{
	return CurrentWeightsMap.Find(BoneName);
}

void FSkinToolWeights::EditVertexWeightAndNormalize(
	const FName BoneToHoldConstant,
	const int32 VertexId,
	const float NewWeightValue,
	FMultiBoneWeightEdits& WeightEdits)
{
	// calculate the sum of all the weights on this vertex (not including the one we currently applied)
	TArray<FName> NamesOfBonesAffectingVertex;
	TArray<float> ValuesToNormalize;
	float Total = 0.0f;
	for (const TTuple<FName, TArray<float>>& Pair : CurrentWeightsMap)
	{
		const FName BoneName = Pair.Key;
		if (BoneName == BoneToHoldConstant)
		{
			continue;
		}
		
		const float WeightOnThisBone = CurrentWeightsMap[BoneName][VertexId];
		if (WeightOnThisBone < MinimumWeightThreshold)
		{
			continue;
		}
		
		NamesOfBonesAffectingVertex.Add(BoneName);
		ValuesToNormalize.Add(WeightOnThisBone);
		Total += WeightOnThisBone;
	}

	// if user applied FULL weight to this vertex OR there's no other weights of any significance,
	// then simply set everything else to zero and return
	if (NewWeightValue >= (1.0f - MinimumWeightThreshold) || Total <= MinimumWeightThreshold)
	{
		// set all other influences to 0.0f
		for (int32 i=0; i<ValuesToNormalize.Num(); ++i)
		{
			const FName BoneName = NamesOfBonesAffectingVertex[i];
			const float OldWeight = ValuesToNormalize[i];
			constexpr float NewWeight = 0.0f;
			WeightEdits.MergeSingleEdit(BoneName, VertexId, OldWeight, NewWeight);
		}

		// set current bone value to 1.0f
		WeightEdits.MergeSingleEdit(
			BoneToHoldConstant,
			VertexId,
			PreStrokeWeightsMap[BoneToHoldConstant][VertexId],
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
		const FName BoneName = NamesOfBonesAffectingVertex[i];
		const float OldWeight = ValuesToNormalize[i];
		const float NewWeight = NormalizedValue;
		WeightEdits.MergeSingleEdit(BoneName, VertexId, OldWeight, NewWeight);
	}

	// record current bone edit
	WeightEdits.MergeSingleEdit(
		BoneToHoldConstant,
		VertexId,
		PreStrokeWeightsMap[BoneToHoldConstant][VertexId],
		NewWeightValue);
}

void FSkinToolWeights::ApplyCurrentWeightsToMeshDescription(
	const FReferenceSkeleton& RefSkeleton,
	FMeshDescription* EditedMesh)
{
	using namespace UE::AnimationCore;
	
	FSkeletalMeshAttributes MeshAttribs(*EditedMesh);
	FSkinWeightsVertexAttributesRef VertexSkinWeights = MeshAttribs.GetVertexSkinWeights();

	TMap<FBoneIndexType, const TArray<float>*> BoneIndexWeightMap;
	for (const TTuple<FName, TArray<float>>& BoneNameAndWeights : CurrentWeightsMap)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneNameAndWeights.Key);
		if (BoneIndex != INDEX_NONE)
		{
			BoneIndexWeightMap.Add(static_cast<FBoneIndexType>(BoneIndex), &BoneNameAndWeights.Value);
		}
	}

	FBoneWeightsSettings Settings;
	Settings.SetNormalizeType(EBoneWeightNormalizeType::None);

	TArray<FBoneWeight> SourceBoneWeights;
	SourceBoneWeights.Reserve(MaxInlineBoneWeightCount);

	const int32 NumVertices = EditedMesh->Vertices().Num();
	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		SourceBoneWeights.Reset();

		for (const TTuple<unsigned short, const TArray<float>*>& BoneIndexWeight : BoneIndexWeightMap)
		{
			SourceBoneWeights.Add(FBoneWeight(BoneIndexWeight.Key, (*BoneIndexWeight.Value)[VertexIndex]));
		}

		VertexSkinWeights.Set(FVertexID(VertexIndex), FBoneWeights::Create(SourceBoneWeights, Settings));
	}
}

void FSkinToolWeights::ResetAfterStroke()
{
	PreStrokeWeightsMap = CurrentWeightsMap;

	for (int32 i=0; i<MaxFalloffPerVertexThisStroke.Num(); ++i)
	{
		MaxFalloffPerVertexThisStroke[i] = 0.f;
	}
}

float FSkinToolWeights::SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStength)
{
	float& MaxFalloffThisStroke = MaxFalloffPerVertexThisStroke[VertexID];
	if (MaxFalloffThisStroke < CurrentStength)
	{
		MaxFalloffThisStroke = CurrentStength;
	}
	return MaxFalloffThisStroke;
}

void FMeshSkinWeightsChange::Apply(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);
	
	for (TTuple<FName, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
	{
		Tool->ExternalUpdateWeights(Pair.Key, Pair.Value.NewWeights);
	}
}

void FMeshSkinWeightsChange::Revert(UObject* Object)
{
	USkinWeightsPaintTool* Tool = CastChecked<USkinWeightsPaintTool>(Object);

	for (TTuple<FName, FSingleBoneWeightEdits>& Pair : AllWeightEdits.PerBoneWeightEdits)
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
	UDynamicMeshBrushTool::Setup();

	const IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	check(TargetComponent);
	const USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(TargetComponent->GetOwnerComponent());
	check(Component && Component->GetSkeletalMeshAsset())

	// initialize the bone browser
	// TODO replace this and synchronize selection & rendering with skeletal mesh editor
	USkeletalMesh& SkeletalMesh = *Component->GetSkeletalMeshAsset();
	BoneContainer.InitializeTo(Component->RequiredBones, Component->GetCurveFilterSettings(0)/* Always use the highest LOD */, SkeletalMesh);

	// create a mesh description for editing (this must be done before calling UpdateBonePositionInfos) 
	EditedMesh = MakeUnique<FMeshDescription>();
	*EditedMesh = *UE::ToolTarget::GetMeshDescription(Target);

	// Update the skeleton drawing information from the original bind pose
	MaxDrawRadius = Component->Bounds.SphereRadius * 0.0025f;
	UpdateBonePositionInfos(MaxDrawRadius);

	// initialize the tool properties
	BrushProperties->RestoreProperties(this); // hides strength and falloff
	ToolProps = NewObject<USkinWeightsPaintToolProperties>(this);
	ToolProps->RestoreProperties(this);
	ToolProps->SkeletalMesh = Component->GetSkeletalMeshAsset();
	ToolProps->CurrentBone.Initialize(BoneContainer);
	AddToolPropertySource(ToolProps);
	// attach callback to be informed when tool properties are modified
	ToolProps->GetOnModified().AddUObject(this, &USkinWeightsPaintTool::OnToolPropertiesModified);

	// default to the root bone as current bone
	PendingCurrentBone = CurrentBone = ToolProps->SkeletalMesh->GetRefSkeleton().GetBoneName(0);
	ToolProps->CurrentBone.BoneName = PendingCurrentBone.GetValue();

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

	// build octree for brush
	VerticesOctree.Initialize(PreviewMesh->GetMesh(), true);

	// record data needed to compute deformations
	Deformer.Initialize(Component, EditedMesh.Get());
	
	// copy weight maps
	const FReferenceSkeleton& RefSkeleton = ToolProps->SkeletalMesh->GetRefSkeleton();
	Weights.InitializeSkinWeights(RefSkeleton, EditedMesh.Get());
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

	// sparsely updates vertex positions (only when needed)
	Deformer.UpdateVertexDeformation(Weights.CurrentWeightsMap, PreviewMesh, VerticesOctree);
}

void USkinWeightsPaintTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UDynamicMeshBrushTool::Render(RenderAPI);

	// FIXME: Make selective.
	//RenderBonePositions(RenderAPI->GetPrimitiveDrawInterface());
}

bool USkinWeightsPaintTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	return UDynamicMeshBrushTool::HitTest(Ray, OutHit);
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

	if (ToolProps->FalloffMode == EWeightBrushFalloffMode::Volume)
	{
		IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
		FTransform3d Transform(TargetComponent->GetWorldTransform());
		FVector3d StampPosLocal = Transform.InverseTransformPosition((FVector3d)Stamp.WorldPosition);

		float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
		FAxisAlignedBox3d QueryBox(StampPosLocal, CurrentBrushRadius);
		VerticesOctree.RangeQuery(QueryBox,
			[&](int32 VertexID) { return FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal) < RadiusSqr; },
			VertexROI);

		for (int32 VertexID : VertexROI)
		{
			VertexSqDistances.Add(FVector3d::DistSquared(Mesh->GetVertex(VertexID), StampPosLocal));
		}
	}
	else
	{
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

		// get coordinate frame from stamp
		FFrame3d SeedFrame = GetFrameFromStamp(Stamp);
		
		const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

		// get triangle under the stamp
		// we pass it's vertices to the ExpMap generator to act as seed nodes to start the search from
		// this is necessary because the stamp may be in a large face center, from from any vertex on the mesh.
		FDynamicMeshAABBTree3 Spatial(Mesh, true);
		double NearDistSqr;
		int32 SeedTID = Spatial.FindNearestTriangle(SeedFrame.Origin, NearDistSqr);
		//FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(*Mesh, SeedTID, SeedFrame.Origin);
		FIndex3i TriVerts = Mesh->GetTriangle(SeedTID);

		// create the ExpMap generator, computes vertex polar coordinates in a plane tangent to the surface
		TMeshLocalParam<FDynamicMesh3> Param(Mesh);
		Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
		Param.ComputeToMaxDistance(SeedFrame, TriVerts, Stamp.Radius);

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
}

FVector4f USkinWeightsPaintTool::WeightToColor(float Value)
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
	if (!Weights.CurrentWeightsMap.Contains(CurrentBone))
	{
		return;
	}
	
	TArray<float>& SkinWeightsData = *Weights.CurrentWeightsMap.Find(CurrentBone);

	// update mesh with new value colors
	PreviewMesh->EditMesh([&](FDynamicMesh3& Mesh)
	{
		UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(Mesh);
		UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
		for (int32 ElementId : ColorOverlay->ElementIndicesItr())
		{
			const int32 VertexId = ColorOverlay->GetParentVertex(ElementId);	
			const int32 SrcVertexId = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexId);
			const float Value = SkinWeightsData.IsValidIndex(SrcVertexId) ? SkinWeightsData[SrcVertexId] : 0.f;
			const FVector4f Color(WeightToColor(Value));
			ColorOverlay->SetElement(ElementId, Color);
		}
	});
}

double USkinWeightsPaintTool::CalculateBrushFalloff(double Distance) const
{
	double f = FMathd::Clamp(1.0 - BrushProperties->BrushFalloffAmount, 0.0, 1.0);
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
	// get the vertices under the brush, and their squared distances to the brush center
	// when using "Volume" brush, distances are straight line
	// when using "Surface" brush, distances are geodesics
	TArray<int32> VerticesInStamp;
	TArray<float> VertexSqDistances;
	CalculateVertexROI(Stamp, VerticesInStamp, VertexSqDistances);

	// gather sparse set of modifications made from this stamp, these edits are merged throughout
	// the lifetime of a single brush stroke in the "ActiveChange" allowing for undo/redo
	FMultiBoneWeightEdits WeightEditsFromStamp;

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

	// store weight edits from all stamps made during a single stroke (1 transaction per stroke)
	for (const TTuple<FName, FSingleBoneWeightEdits>& BoneWeightEdits : WeightEditsFromStamp.PerBoneWeightEdits)
	{
		ActiveChange->AddBoneWeightEdit(BoneWeightEdits.Value);
	}

	// apply weights to weight map
	WeightEditsFromStamp.ApplyEditsToWeightMap(Weights.CurrentWeightsMap);

	// weights have been modified, so update deformations
	Deformer.SetVerticesNeedUpdated(VerticesInStamp);

	// update vertex colors
	PreviewMesh->DeferredEditMesh([&](FDynamicMesh3& Mesh)
	{
		TArray<int> ElementIds;
		UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors();
		const TArray<float>& VertexWeights = Weights.CurrentWeightsMap[CurrentBone];
		const int32 NumVerticesInStamp = VerticesInStamp.Num();
		for (int32 Index = 0; Index < NumVerticesInStamp; ++Index)
		{
			const int32 VertexId = VerticesInStamp[Index];
			FVector4f NewColor(WeightToColor(VertexWeights[VertexId]));
			ColorOverlay->GetVertexElements(VertexId, ElementIds);
			for (const int32 ElementId : ElementIds)
			{
				ColorOverlay->SetElement(ElementId, NewColor);
			}
			ElementIds.Reset();
		}
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}

void USkinWeightsPaintTool::RelaxWeightOnVertices(
	TArray<int32> VerticesInStamp,
	TArray<float> VertexSqDistances,
	FMultiBoneWeightEdits& AllBoneWeightEditsFromStamp)
{
	const FDynamicMesh3* CurrentMesh = PreviewMesh->GetMesh();
	const UE::Geometry::FNonManifoldMappingSupport NonManifoldMappingSupport(*CurrentMesh);

	auto NormalizeWeights = [](TMap<FName, float>& InOutWeights)
	{
		float TotalWeight = 0.f;
		for (const TTuple<FName, float>& Weight : InOutWeights)
		{
			TotalWeight += Weight.Value;
		}

		for (TTuple<FName, float>& Weight : InOutWeights)
		{
			Weight.Value /= TotalWeight;
		}
	};

	// for each vertex in the stamp...
	TArray<int32> AllNeighborVertices;
	TMap<FName, TArray<float>> WeightsOnAllNeighbors;
	TMap<FName, float> FinalWeights;
	for (int32 Index = 0; Index < VerticesInStamp.Num(); ++Index)
	{
		const int32 VertexId = VerticesInStamp[Index];
		const int32 SrcVertexId = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexId);

		// get list of all neighboring vertices, AND this vertex
		AllNeighborVertices.Reset();
		AllNeighborVertices.Add(VertexId);
		for (const int32 NeighborVertexId : CurrentMesh->VtxVerticesItr(SrcVertexId))
		{
			AllNeighborVertices.Add(NeighborVertexId);
		}

		// get all weights above a given threshold across ALL neighbors (including self)
		WeightsOnAllNeighbors.Reset();
		for (const int32 VertexID : AllNeighborVertices)
		{
			for (TTuple<FName, TArray<float>> Pair : Weights.CurrentWeightsMap)
			{
				const float NeighborWeightValue = Pair.Value[VertexID];
				if (NeighborWeightValue > MinimumWeightThreshold)
				{
					const FName BoneName = Pair.Key;
					TArray<float>& BoneWeights = WeightsOnAllNeighbors.FindOrAdd(BoneName);
					BoneWeights.Add(NeighborWeightValue);
				}
			}
		}

		// calculate single average weight of each bone on all the neighbors
		FinalWeights.Reset();
		for (const TTuple<FName, TArray<float>>& NeighborWeights : WeightsOnAllNeighbors)
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
		const float UseFalloff = Weights.SetCurrentFalloffAndGetMaxFalloffThisStroke(VertexId, CurrentFalloff);
		const float UseStrength = BrushProperties->BrushStrength * UseFalloff;
		for (TTuple<FName, float>& FinalWeight : FinalWeights)
		{
			FName BoneName = FinalWeight.Key;
			float NewWeight = FinalWeight.Value;
			float OldWeight = Weights.PreStrokeWeightsMap[BoneName][VertexId];
			FinalWeight.Value = FMath::Lerp(OldWeight, NewWeight, UseStrength);
		}

		// normalize again
		NormalizeWeights(FinalWeights);

		// apply weight edits
		for (const TTuple<FName, float>& FinalWeight : FinalWeights)
		{
			// record an edit for this vertex, for this bone
			FName BoneName = FinalWeight.Key;
			float NewWeight = FinalWeight.Value;
			float OldWeight = Weights.PreStrokeWeightsMap[BoneName][VertexId];
			AllBoneWeightEditsFromStamp.MergeSingleEdit(BoneName, VertexId, OldWeight, NewWeight);
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
	TArray<float>& PreWeightData = *Weights.PreStrokeWeightsMap.Find(CurrentBone);

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
	
	// spin through the vertices in the stamp and store new weight values in NewValuesFromStap
	// afterwards, these values are normalized while taking into consideration the users desired changes
	const int32 NumVerticesInStamp = VerticesInStamp.Num();
	for (int32 Index = 0; Index < NumVerticesInStamp; ++Index)
	{
		const int32 VertexId = VerticesInStamp[Index];
		const int32 SrcVertexId = NonManifoldMappingSupport.GetOriginalNonManifoldVertexID(VertexId);
		const float CurrentFalloff = static_cast<float>(CalculateBrushFalloff(FMath::Sqrt(VertexSqDistances[Index])));
		const float UseFalloff = Weights.SetCurrentFalloffAndGetMaxFalloffThisStroke(SrcVertexId, CurrentFalloff);
		const float ValueBeforeStroke = PreWeightData[SrcVertexId];

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
			VertexId,
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
		const FReferenceSkeleton& RefSkeleton = ToolProps->SkeletalMesh->GetRefSkeleton();
		Weights.ApplyCurrentWeightsToMeshDescription(RefSkeleton, EditedMesh.Get());

		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("SkinWeightsPaintTool", "Paint Skin Weights"));
		UE::ToolTarget::CommitMeshDescriptionUpdate(Target, EditedMesh.Get());
		GetToolManager()->EndUndoTransaction();
	}
}

void USkinWeightsPaintTool::UpdateBonePositionInfos(float MinRadius)
{
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
	const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
	const TArray<FTransform>& BonePoses = RefSkeleton.GetRefBonePose();

	BonePositionInfos.Reset();

	// Exclude virtual bones.
	for (int BoneIndex = 0; BoneIndex < RefSkeleton.GetRawBoneNum(); BoneIndex++)
	{
		FTransform Xform = BonePoses[BoneIndex];
		int32 ParentBoneIndex = BoneInfos[BoneIndex].ParentIndex;

		while (ParentBoneIndex != INDEX_NONE)
		{
			Xform = Xform * BonePoses[ParentBoneIndex];
			ParentBoneIndex = BoneInfos[ParentBoneIndex].ParentIndex;
		}

		BonePositionInfos.Add({ BoneInfos[BoneIndex].Name, BoneInfos[BoneIndex].ParentIndex, Xform.GetLocation(), -1.0f });
	}

	// Populate the children.
	for (int BoneIndex = 0; BoneIndex < BonePositionInfos.Num(); BoneIndex++)
	{
		FBonePositionInfo& BoneInfo = BonePositionInfos[BoneIndex];
		if (BoneInfo.ParentBoneIndex != INDEX_NONE)
		{
			BonePositionInfos[BoneInfo.ParentBoneIndex].ChildBones.Add(BoneInfo.BoneName, BoneIndex);
		}
	}

	bool bComputedRadius = true;
	while (bComputedRadius)
	{
		bComputedRadius = false;

		for (int BoneIndex = 0; BoneIndex < BonePositionInfos.Num(); BoneIndex++)
		{
			FBonePositionInfo& BoneInfo = BonePositionInfos[BoneIndex];
			if (BoneInfo.Radius > 0.0f)
			{
				continue;
			}

			if (BoneInfo.ParentBoneIndex == INDEX_NONE)
			{
				if (BoneInfo.ChildBones.Num())
				{
					int32 Count = 0;
					float RadiusSum = 0.0f;
					for (const auto& CB : BoneInfo.ChildBones)
					{
						const FBonePositionInfo& ChildBoneInfo = BonePositionInfos[CB.Value];
						if (ChildBoneInfo.Radius > 0.0f)
						{
							RadiusSum += ChildBoneInfo.Radius;
							Count++;
						}
					}
					if (BoneInfo.ChildBones.Num() == Count)
					{
						BoneInfo.Radius = RadiusSum / float(Count);
						bComputedRadius = true;
					}
				}
				else
				{
					// No children either? Take the whole mesh.
					BoneInfo.Radius = EditedMesh->GetBounds().SphereRadius;
				}
			}
			else 
			{
				BoneInfo.Radius = FVector::Dist(BoneInfo.Position, BonePositionInfos[BoneInfo.ParentBoneIndex].Position) / 2.0f;
				bComputedRadius = true;
			}

			if (bComputedRadius)
			{
				BoneInfo.Radius = FMath::Max(BoneInfo.Radius, MinRadius);
			}
		}
	}
}


void USkinWeightsPaintTool::RenderBonePositions(FPrimitiveDrawInterface* PDI)
{
	static const int32 NumSphereSides = 10;
	static const int32 NumConeSides = 4;

	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	FTransform WorldTransform = TargetComponent->GetWorldTransform();

	for (const FBonePositionInfo& BoneInfo : BonePositionInfos)
	{
		FLinearColor BoneColor;
		FVector Start, End;

		End = BoneInfo.Position;
		End = WorldTransform.TransformPosition(End);

		if (BoneInfo.ParentBoneIndex != INDEX_NONE)
		{
			Start = BonePositionInfos[BoneInfo.ParentBoneIndex].Position;
			Start = WorldTransform.TransformPosition(Start);
			BoneColor = FLinearColor::White;
		}
		else
		{
			// Root bone.
			BoneColor = FLinearColor::Red;
		}
		BoneColor.A = 0.10f;

		if (BoneInfo.BoneName == CurrentBone)
		{
			BoneColor = FLinearColor(1.0f, 0.34f, 0.0f, 0.75f);
		}

		const float BoneLength = (End - Start).Size();
		// clamp by bound, we don't want too long or big
		const float Radius = FMath::Clamp(BoneLength * 0.05f, 0.1f, MaxDrawRadius);

		// Render Sphere for bone end point and a cone between it and its parent.
		DrawWireSphere(PDI, End, BoneColor, Radius, NumSphereSides, SDPG_Foreground, 0.0f, 1.0f);

		if (BoneInfo.ParentBoneIndex != INDEX_NONE)
		{
			// Calc cone size 
			const FVector EndToStart = (Start - End);
			const float ConeLength = EndToStart.Size();
			const float Angle = FMath::RadiansToDegrees(FMath::Atan(Radius / ConeLength));

			TArray<FVector> Verts;
			DrawWireCone(PDI, Verts, FRotationMatrix::MakeFromX(EndToStart) * FTranslationMatrix(End), ConeLength, Angle, NumConeSides, BoneColor, SDPG_Foreground, 0.0f, 1.0f);
		}

		// SkeletalDebugRendering::DrawWireBone(PDI, Start, End, BoneColor, SDPG_Foreground, Radius);
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


void USkinWeightsPaintTool::ExternalUpdateWeights(const FName& BoneName, const TMap<int32, float>& NewValues)
{
	TArray<float>* PostSkinWeightValues = Weights.CurrentWeightsMap.Find(BoneName);
	TArray<float>* PreSkinWeightValues = Weights.PreStrokeWeightsMap.Find(BoneName);
	if (PostSkinWeightValues == nullptr)
	{
		return;
	}

	for (const TTuple<int, float>& IV : NewValues)
	{
		PostSkinWeightValues->GetData()[IV.Key] = IV.Value;
		PreSkinWeightValues->GetData()[IV.Key] = IV.Value;
		Deformer.SetVertexNeedsUpdated(IV.Key);
	}

	if (BoneName == CurrentBone)
	{
		UpdateCurrentBoneVertexColors();
	}
}

void USkinWeightsPaintTool::OnToolPropertiesModified(UObject* ModifiedObject, FProperty* ModifiedProperty)
{
	// bone changed?
	const bool bCurrentBoneModified = ModifiedProperty->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(FBoneReference, BoneName);
	if (bCurrentBoneModified)
	{
		PendingCurrentBone = ToolProps->CurrentBone.BoneName;
	}
	
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


USkeleton* USkinWeightsPaintToolProperties::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;
	return SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
}


#undef LOCTEXT_NAMESPACE

