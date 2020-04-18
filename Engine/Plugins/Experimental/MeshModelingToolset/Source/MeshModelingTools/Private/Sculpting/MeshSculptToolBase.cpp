// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Sculpting/MeshSculptToolBase.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "ToolSetupUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "PreviewMesh.h"
#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"
#include "Drawing/MeshDebugDrawing.h"



#include "Generators/SphereGenerator.h"



#define LOCTEXT_NAMESPACE "UMeshSculptToolBase"



namespace
{
	const FString VertexSculptIndicatorGizmoType = TEXT("BrushIndicatorGizmoType");
}


void UMeshSculptToolBase::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UMeshSculptToolBase::Setup()
{
	UMeshSurfacePointTool::Setup();

	BrushProperties = NewObject<USculptBrushProperties>(this);
	BrushProperties->RestoreProperties(this);
	BrushProperties->bShowStrength = false;


	// work plane

	GizmoProperties = NewObject<UWorkPlaneProperties>();
	GizmoProperties->RestoreProperties(this);

	// create proxy for plane gizmo, but not gizmo itself, as it only appears in FixedPlane brush mode
	// listen for changes to the proxy and update the plane when that happens
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UMeshSculptToolBase::PlaneTransformChanged);

	//GizmoProperties->WatchProperty(GizmoProperties->Position,
	//	[this](FVector NewPosition) { UpdateGizmoFromProperties(); });
	//GizmoProperties->WatchProperty(GizmoProperties->Rotation,
	//	[this](FQuat NewRotation) { UpdateGizmoFromProperties(); });
	GizmoPositionWatcher.Initialize(
		[this]() { return GizmoProperties->Position; },
		[this](FVector NewPosition) { UpdateGizmoFromProperties(); }, GizmoProperties->Position);
	GizmoRotationWatcher.Initialize(
		[this]() { return GizmoProperties->Rotation; },
		[this](FQuat NewRotation) { UpdateGizmoFromProperties(); }, GizmoProperties->Rotation);



	// display
	ViewProperties = NewObject<UMeshEditingViewProperties>();
	ViewProperties->RestoreProperties(this);

	ViewProperties->WatchProperty(ViewProperties->bShowWireframe,
		[this](bool bNewValue) { UpdateWireframeVisibility(bNewValue); });
	ViewProperties->WatchProperty(ViewProperties->MaterialMode,
		[this](EMeshEditingMaterialModes NewMode) { UpdateMaterialMode(NewMode); });
	ViewProperties->WatchProperty(ViewProperties->bFlatShading,
		[this](bool bNewValue) { UpdateFlatShadingSetting(bNewValue); });
	ViewProperties->WatchProperty(ViewProperties->Color,
		[this](FLinearColor NewColor) { UpdateColorSetting(NewColor); });
	ViewProperties->WatchProperty(ViewProperties->Image,
		[this](UTexture2D* NewImage) { UpdateImageSetting(NewImage); });
}


void UMeshSculptToolBase::Shutdown(EToolShutdownType ShutdownType)
{
	UMeshSurfacePointTool::Shutdown(ShutdownType);

	BrushIndicatorMesh->Disconnect();
	BrushIndicatorMesh = nullptr;

	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	BrushIndicator = nullptr;
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(VertexSculptIndicatorGizmoType);

	BrushProperties->SaveProperties(this);
	if (GizmoProperties)
	{
		GizmoProperties->SaveProperties(this);
	}

	ViewProperties->SaveProperties(this);
}



void UMeshSculptToolBase::OnTick(float DeltaTime)
{
	GizmoPositionWatcher.CheckAndUpdate();
	GizmoRotationWatcher.CheckAndUpdate();

	ActivePressure = GetCurrentDevicePressure();
}


void UMeshSculptToolBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	BrushIndicator->Update((float)GetCurrentBrushRadius(),
		(FVector)HoverStamp.WorldFrame.Origin, (FVector)HoverStamp.WorldFrame.Z(),
		1.0f - BrushProperties->BrushFalloffAmount);
	if (BrushIndicatorMaterial)
	{
		double FixedDimScale = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, HoverStamp.WorldFrame.Origin, 1.5f);
		BrushIndicatorMaterial->SetScalarParameterValue(TEXT("FalloffWidth"), FixedDimScale);
	}

	if (ShowWorkPlane())
	{
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
		FColor GridColor(128, 128, 128, 32);
		float GridThickness = 0.5f;
		float GridLineSpacing = 25.0f;   // @todo should be relative to view
		int NumGridLines = 10;
		FFrame3f DrawFrame(GizmoProperties->Position, GizmoProperties->Rotation);
		MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
	}
}








void UMeshSculptToolBase::UpdateHoverStamp(const FVector3d& WorldPos, const FVector3d& WorldNormal)
{
	HoverStamp.WorldFrame = FFrame3d(WorldPos, WorldNormal);
}



void UMeshSculptToolBase::InitializeBrushSizeRange(const FAxisAlignedBox3d& TargetBounds)
{
	double MaxDimension = TargetBounds.MaxDim();
	BrushRelativeSizeRange = FInterval1d(MaxDimension * 0.01, MaxDimension);
	CalculateBrushRadius();
}


void UMeshSculptToolBase::CalculateBrushRadius()
{
	CurrentBrushRadius = 0.5 * BrushRelativeSizeRange.Interpolate(BrushProperties->BrushSize);
	if (BrushProperties->bSpecifyRadius)
	{
		CurrentBrushRadius = BrushProperties->BrushRadius;
	}
	else
	{
		BrushProperties->BrushRadius = CurrentBrushRadius;
	}
}



void UMeshSculptToolBase::IncreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::DecreaseBrushRadiusAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.025f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::IncreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::DecreaseBrushRadiusSmallStepAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.005f, 0.0f, 1.0f);
	CalculateBrushRadius();
}

void UMeshSculptToolBase::IncreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.1f;
	const float OldValue = BrushProperties->BrushFalloffAmount;
	float NewValue = OldValue + ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}

void UMeshSculptToolBase::DecreaseBrushFalloffAction()
{
	const float ChangeAmount = 0.1f;
	const float OldValue = BrushProperties->BrushFalloffAmount;
	float NewValue = OldValue - ChangeAmount;
	BrushProperties->BrushFalloffAmount = FMath::Clamp(NewValue, 0.f, 1.f);
}







void UMeshSculptToolBase::UpdateWireframeVisibility(bool bNewValue)
{
	GetSculptMeshComponent()->SetEnableWireframeRenderPass(bNewValue);
}

void UMeshSculptToolBase::UpdateFlatShadingSetting(bool bNewValue)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (bNewValue) ? 1.0f : 0.0f);
	}
}

void UMeshSculptToolBase::UpdateColorSetting(FLinearColor NewColor)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetVectorParameterValue(TEXT("Color"), NewColor);
	}
}

void UMeshSculptToolBase::UpdateImageSetting(UTexture2D* NewImage)
{
	if (ActiveOverrideMaterial != nullptr)
	{
		ActiveOverrideMaterial->SetTextureParameterValue(TEXT("ImageTexture"), NewImage);
	}
}



void UMeshSculptToolBase::UpdateMaterialMode(EMeshEditingMaterialModes MaterialMode)
{
	if (MaterialMode == EMeshEditingMaterialModes::ExistingMaterial)
	{
		GetSculptMeshComponent()->ClearOverrideRenderMaterial();
		GetSculptMeshComponent()->bCastDynamicShadow = ComponentTarget->GetOwnerComponent()->bCastDynamicShadow;
		ActiveOverrideMaterial = nullptr;
	}
	else
	{
		if (MaterialMode == EMeshEditingMaterialModes::Custom)
		{
			ActiveOverrideMaterial = ToolSetupUtil::GetCustomImageBasedSculptMaterial(GetToolManager(), ViewProperties->Image);
			if (ViewProperties->Image != nullptr)
			{
				ActiveOverrideMaterial->SetTextureParameterValue(TEXT("ImageTexture"), ViewProperties->Image);
			}
		}
		else
		{
			UMaterialInterface* SculptMaterial = nullptr;
			switch (MaterialMode)
			{
			case EMeshEditingMaterialModes::Diffuse:
				SculptMaterial = ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager());
				break;
			case EMeshEditingMaterialModes::Grey:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::DefaultBasic);
				break;
			case EMeshEditingMaterialModes::Soft:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::DefaultSoft);
				break;
			case EMeshEditingMaterialModes::TangentNormal:
				SculptMaterial = ToolSetupUtil::GetImageBasedSculptMaterial(GetToolManager(), ToolSetupUtil::ImageMaterialType::TangentNormalFromView);
				break;
			}
			if (SculptMaterial != nullptr)
			{
				ActiveOverrideMaterial = UMaterialInstanceDynamic::Create(SculptMaterial, this);
			}
		}

		if (ActiveOverrideMaterial != nullptr)
		{
			GetSculptMeshComponent()->SetOverrideRenderMaterial(ActiveOverrideMaterial);
			ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (ViewProperties->bFlatShading) ? 1.0f : 0.0f);
		}

		GetSculptMeshComponent()->bCastDynamicShadow = false;
	}
}









void UMeshSculptToolBase::InitializeIndicator()
{
	// register and spawn brush indicator gizmo
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(VertexSculptIndicatorGizmoType, NewObject<UBrushStampIndicatorBuilder>());
	BrushIndicator = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UBrushStampIndicator>(VertexSculptIndicatorGizmoType, FString(), this);
	BrushIndicatorMesh = MakeDefaultIndicatorSphereMesh(this, TargetWorld);
	BrushIndicator->AttachedComponent = BrushIndicatorMesh->GetRootComponent();
	BrushIndicator->LineThickness = 1.0;
	BrushIndicator->bDrawIndicatorLines = true;
	BrushIndicator->bDrawRadiusCircle = false;
	BrushIndicator->bDrawFalloffCircle = true;
	BrushIndicator->LineColor = FLinearColor(0.9f, 0.4f, 0.4f);
}


UPreviewMesh* UMeshSculptToolBase::MakeDefaultIndicatorSphereMesh(UObject* Parent, UWorld* World, int Resolution /*= 32*/)
{
	UPreviewMesh* SphereMesh = NewObject<UPreviewMesh>(Parent);
	SphereMesh->CreateInWorld(World, FTransform::Identity);
	FSphereGenerator SphereGen;
	SphereGen.NumPhi = SphereGen.NumTheta = Resolution;
	SphereGen.Generate();
	FDynamicMesh3 Mesh(&SphereGen);
	SphereMesh->UpdatePreview(&Mesh);

	BrushIndicatorMaterial = ToolSetupUtil::GetDefaultBrushVolumeMaterial(GetToolManager());
	if (BrushIndicatorMaterial)
	{
		SphereMesh->SetMaterial(BrushIndicatorMaterial);
	}

	return SphereMesh;
}




void UMeshSculptToolBase::UpdateWorkPlane()
{
	bool bGizmoVisible = ShowWorkPlane() && (GizmoProperties->bShowGizmo);
	UpdateFixedPlaneGizmoVisibility(bGizmoVisible);
	GizmoProperties->bPropertySetEnabled = ShowWorkPlane();

	if (PendingWorkPlaneUpdate != EPendingWorkPlaneUpdate::NoUpdatePending)
	{
		SetFixedSculptPlaneFromWorldPos((FVector)HoverStamp.WorldFrame.Origin, (FVector)HoverStamp.WorldFrame.Z(), PendingWorkPlaneUpdate);
		PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::NoUpdatePending;
	}
}


void UMeshSculptToolBase::SetFixedSculptPlaneFromWorldPos(const FVector& Position, const FVector& Normal, EPendingWorkPlaneUpdate UpdateType)
{
	if (UpdateType == EPendingWorkPlaneUpdate::MoveToHitPositionNormal)
	{
		UpdateFixedSculptPlanePosition(Position);
		FFrame3d CurFrame(FVector::ZeroVector, GizmoProperties->Rotation);
		CurFrame.AlignAxis(2, (FVector3d)Normal);
		UpdateFixedSculptPlaneRotation((FQuat)CurFrame.Rotation);
	}
	else if (UpdateType == EPendingWorkPlaneUpdate::MoveToHitPositionViewAligned)
	{
		UpdateFixedSculptPlanePosition(Position);
		FFrame3d CurFrame(FVector::ZeroVector, GizmoProperties->Rotation);
		CurFrame.AlignAxis(2, -(FVector3d)CameraState.Forward());
		UpdateFixedSculptPlaneRotation((FQuat)CurFrame.Rotation);
	}
	else
	{
		UpdateFixedSculptPlanePosition(Position);
	}

	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
	}
}

void UMeshSculptToolBase::PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	UpdateFixedSculptPlaneRotation(Transform.GetRotation());
	UpdateFixedSculptPlanePosition(Transform.GetLocation());
}

void UMeshSculptToolBase::UpdateFixedSculptPlanePosition(const FVector& Position)
{
	GizmoProperties->Position = Position;
	GizmoPositionWatcher.SilentUpdate();
}

void UMeshSculptToolBase::UpdateFixedSculptPlaneRotation(const FQuat& Rotation)
{
	GizmoProperties->Rotation = Rotation;
	GizmoPositionWatcher.SilentUpdate();
}

void UMeshSculptToolBase::UpdateGizmoFromProperties()
{
	if (PlaneTransformGizmo != nullptr)
	{
		PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
	}
}

void UMeshSculptToolBase::UpdateFixedPlaneGizmoVisibility(bool bVisible)
{
	if (bVisible == false)
	{
		if (PlaneTransformGizmo != nullptr)
		{
			GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(PlaneTransformGizmo);
			PlaneTransformGizmo = nullptr;
		}
	}
	else
	{
		if (PlaneTransformGizmo == nullptr)
		{
			PlaneTransformGizmo = GetToolManager()->GetPairedGizmoManager()->CreateCustomTransformGizmo(
				ETransformGizmoSubElements::StandardTranslateRotate, this);
			PlaneTransformGizmo->bUseContextCoordinateSystem = false;
			PlaneTransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;
			PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy, GetToolManager());
			PlaneTransformGizmo->SetNewGizmoTransform(FTransform(GizmoProperties->Rotation, GizmoProperties->Position));
		}

		PlaneTransformGizmo->bSnapToWorldGrid = GizmoProperties->bSnapToGrid;
	}
}







void UMeshSculptToolBase::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::IncreaseBrushSize,
		TEXT("SculptIncreaseRadius"),
		LOCTEXT("SculptIncreaseRadius", "Increase Sculpt Radius"),
		LOCTEXT("SculptIncreaseRadiusTooltip", "Increase radius of sculpting brush"),
		EModifierKey::None, EKeys::RightBracket,
		[this]() { IncreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::DecreaseBrushSize,
		TEXT("SculptDecreaseRadius"),
		LOCTEXT("SculptDecreaseRadius", "Decrease Sculpt Radius"),
		LOCTEXT("SculptDecreaseRadiusTooltip", "Decrease radius of sculpting brush"),
		EModifierKey::None, EKeys::LeftBracket,
		[this]() { DecreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 12,
		TEXT("BrushIncreaseFalloff"),
		LOCTEXT("BrushIncreaseFalloff", "Increase Brush Falloff"),
		LOCTEXT("BrushIncreaseFalloffTooltip", "Press this key to increase brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::RightBracket,
		[this]() { IncreaseBrushFalloffAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 13,
		TEXT("BrushDecreaseFalloff"),
		LOCTEXT("BrushDecreaseFalloff", "Decrease Brush Falloff"),
		LOCTEXT("BrushDecreaseFalloffTooltip", "Press this key to decrease brush falloff by a fixed increment."),
		EModifierKey::Shift | EModifierKey::Control, EKeys::LeftBracket,
		[this]() { DecreaseBrushFalloffAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("NextBrushMode"),
		LOCTEXT("SculptNextBrushMode", "Next Brush Type"),
		LOCTEXT("SculptNextBrushModeTooltip", "Cycle to next Brush Type"),
		EModifierKey::None, EKeys::A,
		[this]() { NextBrushModeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("PreviousBrushMode"),
		LOCTEXT("SculptPreviousBrushMode", "Previous Brush Type"),
		LOCTEXT("SculptPreviousBrushModeTooltip", "Cycle to previous Brush Type"),
		EModifierKey::None, EKeys::Q,
		[this]() { PreviousBrushModeAction(); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 60,
		TEXT("SculptIncreaseSpeed"),
		LOCTEXT("SculptIncreaseSpeed", "Increase Speed"),
		LOCTEXT("SculptIncreaseSpeedTooltip", "Increase Brush Speed"),
		EModifierKey::None, EKeys::E,
		[this]() { IncreaseBrushSpeedAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 61,
		TEXT("SculptDecreaseSpeed"),
		LOCTEXT("SculptDecreaseSpeed", "Decrease Speed"),
		LOCTEXT("SculptDecreaseSpeedTooltip", "Decrease Brush Speed"),
		EModifierKey::None, EKeys::W,
		[this]() { DecreaseBrushSpeedAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 50,
		TEXT("SculptIncreaseSize"),
		LOCTEXT("SculptIncreaseSize", "Increase Size"),
		LOCTEXT("SculptIncreaseSizeTooltip", "Increase Brush Size"),
		EModifierKey::None, EKeys::D,
		[this]() { IncreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 51,
		TEXT("SculptDecreaseSize"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::None, EKeys::S,
		[this]() { DecreaseBrushRadiusAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 52,
		TEXT("SculptIncreaseSizeSmallStep"),
		LOCTEXT("SculptIncreaseSize", "Increase Size"),
		LOCTEXT("SculptIncreaseSizeTooltip", "Increase Brush Size"),
		EModifierKey::Shift, EKeys::D,
		[this]() { IncreaseBrushRadiusSmallStepAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 53,
		TEXT("SculptDecreaseSizeSmallStemp"),
		LOCTEXT("SculptDecreaseSize", "Decrease Size"),
		LOCTEXT("SculptDecreaseSizeTooltip", "Decrease Brush Size"),
		EModifierKey::Shift, EKeys::S,
		[this]() { DecreaseBrushRadiusSmallStepAction(); });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::ToggleWireframe,
		TEXT("ToggleWireframe"),
		LOCTEXT("ToggleWireframe", "Toggle Wireframe"),
		LOCTEXT("ToggleWireframeTooltip", "Toggle visibility of wireframe overlay"),
		EModifierKey::Alt, EKeys::W,
		[this]() { ViewProperties->bShowWireframe = !ViewProperties->bShowWireframe; });



	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 100,
		TEXT("SetSculptWorkSurfacePosNormal"),
		LOCTEXT("SetSculptWorkSurfacePosNormal", "Reorient Work Surface"),
		LOCTEXT("SetSculptWorkSurfacePosNormalTooltip", "Move the Sculpting Work Plane/Surface to Position and Normal of World hit point under cursor"),
		EModifierKey::Shift, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPositionNormal; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 101,
		TEXT("SetSculptWorkSurfacePos"),
		LOCTEXT("SetSculptWorkSurfacePos", "Reposition Work Surface"),
		LOCTEXT("SetSculptWorkSurfacePosTooltip", "Move the Sculpting Work Plane/Surface to World hit point under cursor (keep current Orientation)"),
		EModifierKey::None, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPosition; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 102,
		TEXT("SetSculptWorkSurfaceView"),
		LOCTEXT("SetSculptWorkSurfaceView", "View-Align Work Surface"),
		LOCTEXT("SetSculptWorkSurfaceViewTooltip", "Move the Sculpting Work Plane/Surface to World hit point under cursor and align to View"),
		EModifierKey::Control | EModifierKey::Shift, EKeys::T,
		[this]() { PendingWorkPlaneUpdate = EPendingWorkPlaneUpdate::MoveToHitPositionViewAligned; });
}


#undef LOCTEXT_NAMESPACE

