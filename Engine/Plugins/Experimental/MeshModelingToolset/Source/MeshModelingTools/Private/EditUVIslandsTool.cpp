// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditUVIslandsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "SegmentTypes.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "Selections/MeshConnectedComponents.h"
#include "Transforms/MultiTransformer.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "ToolSetupUtil.h"

#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "UEditUVIslandsTool"



/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UEditUVIslandsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UEditUVIslandsTool* DeformTool = NewObject<UEditUVIslandsTool>(SceneState.ToolManager);
	return DeformTool;
}


/*
* Tool methods
*/

UEditUVIslandsTool::UEditUVIslandsTool()
{
}

void UEditUVIslandsTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// register click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());
	WorldTransform = FTransform3d(DynamicMeshComponent->GetComponentTransform());

	// set materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	// enable secondary triangle buffers. Will default to existing material unless we set override.
	DynamicMeshComponent->EnableSecondaryTriangleBuffers(
		[this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return SelectionMechanic->GetActiveSelection().IsSelectedTriangle(Mesh, &Topology, TriangleID);
	});

	// dynamic mesh configuration settings
	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	FMeshNormals::QuickComputeVertexNormals(*DynamicMeshComponent->GetMesh());
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UEditUVIslandsTool::OnDynamicMeshComponentChanged));

	// set up SelectionMechanic
	SelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	SelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	SelectionMechanic->Setup(this);
	SelectionMechanic->Properties->bSelectEdges = SelectionMechanic->Properties->bSelectVertices = false;
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UEditUVIslandsTool::OnSelectionModifiedEvent);

	// initialize AABBTree
	MeshSpatial.SetMesh(DynamicMeshComponent->GetMesh());
	PrecomputeTopology();

	UVTranslateScale = 1.0 / DynamicMeshComponent->GetMesh()->GetBounds().MaxDim();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;

	// MultiTransformer abstracts the standard and "quick" Gizmo variants
	MultiTransformer = NewObject<UMultiTransformer>(this);
	MultiTransformer->Setup(GetToolManager()->GetPairedGizmoManager(), GetToolManager());
	MultiTransformer->OnTransformStarted.AddUObject(this, &UEditUVIslandsTool::OnMultiTransformerTransformBegin);
	MultiTransformer->OnTransformUpdated.AddUObject(this, &UEditUVIslandsTool::OnMultiTransformerTransformUpdate);
	MultiTransformer->OnTransformCompleted.AddUObject(this, &UEditUVIslandsTool::OnMultiTransformerTransformEnd);
	MultiTransformer->SetGizmoVisibility(false);
	MultiTransformer->SetEnabledGizmoSubElements(
		ETransformGizmoSubElements::TranslateAxisX | ETransformGizmoSubElements::TranslateAxisY
		| ETransformGizmoSubElements::TranslatePlaneXY | ETransformGizmoSubElements::RotateAxisZ
		| ETransformGizmoSubElements::ScaleAxisX | ETransformGizmoSubElements::ScaleAxisY
		| ETransformGizmoSubElements::ScalePlaneXY | ETransformGizmoSubElements::ScaleUniform );
	MultiTransformer->SetOverrideGizmoCoordinateSystem(EToolContextCoordinateSystem::Local);


	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->RestoreProperties(this);
	AddToolPropertySource(MaterialSettings);
	MaterialSettings->GetOnModified().AddLambda([this](UObject*, FProperty*)
	{
		OnMaterialSettingsChanged();
	});
	OnMaterialSettingsChanged();

	GetToolManager()->DisplayMessage(LOCTEXT("UEditUVIslandsToolStartupMessage", "Click on a UV Island to select it, and then use the Gizmo to translate/rotate/scale the UVs"), EToolMessageLevel::UserNotification);
}

void UEditUVIslandsTool::Shutdown(EToolShutdownType ShutdownType)
{
	MaterialSettings->SaveProperties(this);

	MultiTransformer->Shutdown();
	SelectionMechanic->Shutdown();

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("EditUVIslandsToolTransactionName", "Edit UVs"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FConversionToMeshDescriptionOptions ConversionOptions;
				ConversionOptions.bUpdateNormals = ConversionOptions.bUpdatePositions = false;
				ConversionOptions.bUpdateUVs = true;
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, false, ConversionOptions);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}




void UEditUVIslandsTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
	//	TEXT("NextTransformType"),
	//	LOCTEXT("NextTransformType", "Next Transform Type"),
	//	LOCTEXT("NextTransformTypeTooltip", "Cycle to next transform type"),
	//	EModifierKey::None, EKeys::Q,
	//	[this]() { NextTransformTypeAction(); });
}


void UEditUVIslandsTool::OnMaterialSettingsChanged()
{
	MaterialSettings->UpdateMaterials();

	UMaterialInterface* OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	if (OverrideMaterial != nullptr)
	{
		DynamicMeshComponent->SetSecondaryRenderMaterial(OverrideMaterial);
	}
	else
	{
		DynamicMeshComponent->ClearSecondaryRenderMaterial();
	}
}



FDynamicMeshAABBTree3& UEditUVIslandsTool::GetSpatial()
{
	if (bSpatialDirty)
	{
		MeshSpatial.Build();
		bSpatialDirty = false;
	}
	return MeshSpatial;
}







bool UEditUVIslandsTool::HitTest(const FRay& WorldRay, FHitResult& OutHit)
{
	// disable hit test
	return SelectionMechanic->TopologyHitTest(WorldRay, OutHit);
}



FInputRayHit UEditUVIslandsTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (HitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}

void UEditUVIslandsTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	// update selection
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolyMeshSelectionChange", "Selection"));
	SelectionMechanic->BeginChange();
	FVector3d LocalHitPos, LocalHitNormal;
	bool bSelectionModified = SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPos, LocalHitNormal);

	if (bSelectionModified && SelectionMechanic->GetActiveSelection().IsEmpty() == false)
	{
		FFrame3d UseFrame = Topology.GetIslandFrame(
			SelectionMechanic->GetActiveSelection().GetASelectedGroupID(), GetSpatial());
		UseFrame.Transform(WorldTransform);
		MultiTransformer->UpdateGizmoPositionFromWorldFrame(UseFrame, true);
		//MultiTransformer->SetGizmoPositionFromWorldFrame(SelectionMechanic->GetSelectionFrame(true), true);

	}

	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}


void UEditUVIslandsTool::OnSelectionModifiedEvent()
{
	bSelectionStateDirty = true;
}




FInputRayHit UEditUVIslandsTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	// disable this for now
	return FInputRayHit();
	//return UMeshSurfacePointTool::CanBeginClickDragSequence(PressPos);
}



void UEditUVIslandsTool::OnBeginDrag(const FRay& WorldRay)
{
}



void UEditUVIslandsTool::OnUpdateDrag(const FRay& Ray)
{
	check(false);
}

void UEditUVIslandsTool::OnEndDrag(const FRay& Ray)
{
	check(false);
}



void UEditUVIslandsTool::OnMultiTransformerTransformBegin()
{
	SelectionMechanic->ClearHighlight();
	UpdateUVTransformFromSelection( SelectionMechanic->GetActiveSelection() );
	InitialGizmoFrame = MultiTransformer->GetCurrentGizmoFrame();
	InitialGizmoScale = MultiTransformer->GetCurrentGizmoScale();
	BeginChange();
}

void UEditUVIslandsTool::OnMultiTransformerTransformUpdate()
{
	if (MultiTransformer->InGizmoEdit())
	{
		ComputeUpdate_Gizmo();
	}
}

void UEditUVIslandsTool::OnMultiTransformerTransformEnd()
{
	SelectionMechanic->NotifyMeshChanged(false);

	MultiTransformer->ResetScale();

	// close change record
	EndChange();
}



bool UEditUVIslandsTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (ActiveVertexChange == nullptr && MultiTransformer->InGizmoEdit() == false )
	{
		SelectionMechanic->UpdateHighlight(DevicePos.WorldRay);
	}
	return true;
}


void UEditUVIslandsTool::OnEndHover()
{
	SelectionMechanic->ClearHighlight();
}




void UEditUVIslandsTool::UpdateUVTransformFromSelection(const FGroupTopologySelection& Selection)
{
	ActiveIslands.Reset();
	ActiveIslands.SetNum(Selection.SelectedGroupIDs.Num());
	int k = 0;
	for (int32 IslandID : Selection.SelectedGroupIDs)
	{
		FEditIsland& IslandInfo = ActiveIslands[k++];

		FGroupTopologySelection TempSelection;
		TempSelection.SelectedGroupIDs.Add(IslandID);
		IslandInfo.LocalFrame = Topology.GetIslandFrame(IslandID, GetSpatial());
		IslandInfo.Triangles = Topology.GetGroupTriangles(IslandID);

		TSet<int32> UVs;
		for (int32 tid : IslandInfo.Triangles)
		{
			if (Topology.UVOverlay->IsSetTriangle(tid))
			{
				FIndex3i Tri = Topology.UVOverlay->GetTriangle(tid);
				UVs.Add(Tri.A);
				UVs.Add(Tri.B);
				UVs.Add(Tri.C);
			}
		}

		IslandInfo.UVBounds = FAxisAlignedBox2d::Empty();
		for (int32 uvid : UVs)
		{
			IslandInfo.UVs.Add(uvid);
			FVector2f InitialUV = Topology.UVOverlay->GetElement(uvid);
			IslandInfo.InitialPositions.Add(InitialUV);
			IslandInfo.UVBounds.Contain(FVector2d(InitialUV));
		}
		IslandInfo.UVOrigin = IslandInfo.UVBounds.Center();
	}
}





void UEditUVIslandsTool::ComputeUpdate_Gizmo()
{
	if (SelectionMechanic->HasSelection() == false)
	{
		return;
	}

	FFrame3d CurFrame = MultiTransformer->GetCurrentGizmoFrame();
	FVector3d CurScale = MultiTransformer->GetCurrentGizmoScale();
	FVector3d TranslationDelta = CurFrame.Origin - InitialGizmoFrame.Origin;
	FQuaterniond RotateDelta = CurFrame.Rotation - InitialGizmoFrame.Rotation;
	FVector3d CurScaleDelta = CurScale - InitialGizmoScale;
	double DeltaU = UVTranslateScale * InitialGizmoFrame.X().Dot(TranslationDelta);
	double DeltaV = UVTranslateScale * InitialGizmoFrame.Y().Dot(TranslationDelta);
	FVector2d UVTranslate(-DeltaU, -DeltaV);
	double RotateAngleDeg = VectorUtil::PlaneAngleSignedD(InitialGizmoFrame.X(), CurFrame.X(), InitialGizmoFrame.Z());
	FMatrix2d UVRotate = FMatrix2d::RotationDeg(-RotateAngleDeg);
	FVector2d UVScale(1.0/CurScale.X, 1.0/CurScale.Y);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(0);
	bool bHaveTransformation = (TranslationDelta.SquaredLength() > 0.0001 || RotateDelta.SquaredLength() > 0.0001 || CurScaleDelta.SquaredLength() > 0.0001);
	
	for (FEditIsland& Island : ActiveIslands)
	{
		int32 NumUVs = Island.UVs.Num();
		FVector2d OriginTranslate = Island.UVOrigin + UVTranslate;
		for ( int32 k = 0; k < NumUVs; ++k )
		{
			int32 uvid = Island.UVs[k];
			FVector2f InitialUV = Island.InitialPositions[k];
			if (bHaveTransformation)
			{
				FVector2d LocalUV = FVector2d(InitialUV) - Island.UVOrigin;
				FVector2d NewUV = (UVRotate * (UVScale * LocalUV)) + OriginTranslate;
				UVOverlay->SetElement(uvid, FVector2f(NewUV) );
			}
			else
			{
				UVOverlay->SetElement(uvid, InitialUV);
			}
		}
	}

	DynamicMeshComponent->FastNotifyUVsUpdated();
	GetToolManager()->PostInvalidation();
}



void UEditUVIslandsTool::OnTick(float DeltaTime)
{
	MultiTransformer->Tick(DeltaTime);

	if (bSelectionStateDirty)
	{
		// update color highlights
		DynamicMeshComponent->FastNotifySecondaryTrianglesChanged();

		if (SelectionMechanic->HasSelection())
		{
			MultiTransformer->SetGizmoVisibility(true);
		}
		else
		{
			MultiTransformer->SetGizmoVisibility(false);
		}

		bSelectionStateDirty = false;
	}
}




FUVGroupTopology::FUVGroupTopology(const FDynamicMesh3* Mesh, uint32 UVLayerIndex, bool bAutoBuild)
	: FGroupTopology(Mesh, false)
{
	if (Mesh->HasAttributes() && UVLayerIndex < (uint32)Mesh->Attributes()->NumUVLayers())
	{
		UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

		if (bAutoBuild)
		{
			CalculateIslandGroups();
			RebuildTopology();
		}
	}
}


void FUVGroupTopology::CalculateIslandGroups()
{
	if (UVOverlay == nullptr)
	{
		return;
	}

	FMeshConnectedComponents UVComponents(Mesh);
	UVComponents.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1) {
		return UVOverlay->AreTrianglesConnected(Triangle0, Triangle1);
	});

	int32 UVGroupCounter = 1;
	TriIslandGroups.SetNumUninitialized(Mesh->MaxTriangleID());
	for (const FMeshConnectedComponents::FComponent& Component : UVComponents)
	{
		for (int32 tid : Component.Indices)
		{
			TriIslandGroups[tid] = UVGroupCounter;
		}
		UVGroupCounter++;
	}
}


FFrame3d FUVGroupTopology::GetIslandFrame(int32 GroupID, FDynamicMeshAABBTree3& AABBTree)
{
	FFrame3d Frame = GetGroupFrame(GroupID);
	IMeshSpatial::FQueryOptions QueryOptions([&](int32 TriangleID) { return GetGroupID(TriangleID) == GroupID; });
	Frame.Origin = AABBTree.FindNearestPoint(Frame.Origin, QueryOptions);

	const TArray<int32>& Triangles = GetGroupTriangles(GroupID);

	// Accumulate gradients of UV.X over triangles and align frame X with that direction.
	// Probably should weight with a falloff from frame origin?
	FVector3d AccumX = FVector3d::Zero();
	for (int32 TriangleID : Triangles)
	{
		FVector3d A, B, C;
		Mesh->GetTriVertices(TriangleID, A, B, C);
		FVector2f fi, fj, fk;
		UVOverlay->GetTriElements(TriangleID, fi, fj, fk);

		FVector3d GradX = VectorUtil::TriGradient<double>(A, B, C, fi.X, fj.X, fk.X);
		AccumX += GradX.Normalized();
	}
	AccumX.Normalize();
	Frame.AlignAxis(0, AccumX);

	return Frame;
}


void UEditUVIslandsTool::PrecomputeTopology()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	Topology = FUVGroupTopology(Mesh, 0, true);

	// update selection mechanic
	SelectionMechanic->Initialize(DynamicMeshComponent, &Topology,
		[this]() { return &GetSpatial(); },
		[this]() { return GetShiftToggle(); }
		);
}




void UEditUVIslandsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	//DynamicMeshComponent->bExplicitShowWireframe = TransformProps->bShowWireframe;
	DynamicMeshComponent->bExplicitShowWireframe = false;

	SelectionMechanic->Render(RenderAPI);
}




//
// Change Tracking
//


void UEditUVIslandsTool::UpdateChangeFromROI(bool bFinal)
{
	if (ActiveVertexChange == nullptr)
	{
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	//const TSet<int>& ModifiedVertices = LinearDeformer.GetModifiedVertices();
	//ActiveVertexChange->SavePositions(Mesh, ModifiedVertices, !bFinal);
}


void UEditUVIslandsTool::BeginChange()
{
	if (ActiveVertexChange == nullptr)
	{
		ActiveVertexChange = new FMeshVertexChangeBuilder();
		UpdateChangeFromROI(false);
	}
}


void UEditUVIslandsTool::EndChange()
{
	if (ActiveVertexChange != nullptr)
	{
		UpdateChangeFromROI(true);
		GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(ActiveVertexChange->Change), LOCTEXT("UVEditChange", "UV Edit"));
	}

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;
}


void UEditUVIslandsTool::OnDynamicMeshComponentChanged()
{
	SelectionMechanic->NotifyMeshChanged(false);
}




#undef LOCTEXT_NAMESPACE
