// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBrushTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"

#define LOCTEXT_NAMESPACE "UBaseBrushTool"



UBrushBaseProperties::UBrushBaseProperties()
{
	BrushSize = 0.25f;
	bSpecifyRadius = false;
	BrushRadius = 0.0f;
}

void UBrushBaseProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UBrushBaseProperties* PropertyCache = GetPropertyCache<UBrushBaseProperties>();
	PropertyCache->BrushSize = this->BrushSize;
	PropertyCache->bSpecifyRadius = this->bSpecifyRadius;
	PropertyCache->BrushRadius = this->BrushRadius;
}

void UBrushBaseProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UBrushBaseProperties* PropertyCache = GetPropertyCache<UBrushBaseProperties>();
	this->BrushSize = PropertyCache->BrushSize;
	this->bSpecifyRadius = PropertyCache->bSpecifyRadius;
	this->BrushRadius = PropertyCache->BrushRadius;
}




UBaseBrushTool::UBaseBrushTool()
{
}


void UBaseBrushTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	double MaxDimension = EstimateMaximumTargetDimension();
	BrushRelativeSizeRange = FInterval1d(MaxDimension*0.01, MaxDimension);
	BrushProperties = NewObject<UBrushBaseProperties>(this, TEXT("Brush"));
	RecalculateBrushRadius();

	// initialize our properties
	AddToolPropertySource(BrushProperties);

	SetupBrushStampIndicator();
}


void UBaseBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	ShutdownBrushStampIndicator();
}


void UBaseBrushTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	if (PropertySet == BrushProperties)
	{
		RecalculateBrushRadius();
	}
}



void UBaseBrushTool::IncreaseBrushSizeAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize + 0.025f, 0.0f, 1.0f);
	RecalculateBrushRadius();
}

void UBaseBrushTool::DecreaseBrushSizeAction()
{
	BrushProperties->BrushSize = FMath::Clamp(BrushProperties->BrushSize - 0.025f, 0.0f, 1.0f);
	RecalculateBrushRadius();
}


void UBaseBrushTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::IncreaseBrushSize,
		TEXT("BrushIncreaseSize"), 
		LOCTEXT("BrushIncreaseSize", "Increase Brush Size"),
		LOCTEXT("BrushIncreaseSizeTooltip", "Increase size of brush"),
		EModifierKey::None, EKeys::RightBracket,
		[this]() { IncreaseBrushSizeAction(); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::DecreaseBrushSize,
		TEXT("BrushDecreaseSize"), 
		LOCTEXT("BrushDecreaseSize", "Decrease Brush Size"),
		LOCTEXT("BrushDecreaseSizeTooltip", "Decrease size of brush"),
		EModifierKey::None, EKeys::LeftBracket,
		[this]() { DecreaseBrushSizeAction(); });
}




void UBaseBrushTool::RecalculateBrushRadius()
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


void UBaseBrushTool::OnBeginDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
	}
	bInBrushStroke = true;
}

void UBaseBrushTool::OnUpdateDrag(const FRay& Ray)
{
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
	}
}

void UBaseBrushTool::OnEndDrag(const FRay& Ray)
{
	bInBrushStroke = false;
}


bool UBaseBrushTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FHitResult OutHit;
	if (HitTest(DevicePos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
	}
	return true;
}



void UBaseBrushTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);

	UpdateBrushStampIndicator();
}

void UBaseBrushTool::Tick(float DeltaTime)
{
	UMeshSurfacePointTool::Tick(DeltaTime);
}




const FString BaseBrushIndicatorGizmoType = TEXT("BrushIndicatorGizmoType");

void UBaseBrushTool::SetupBrushStampIndicator()
{
	// register and spawn brush indicator gizmo
	GetToolManager()->GetPairedGizmoManager()->RegisterGizmoType(BaseBrushIndicatorGizmoType, NewObject<UBrushStampIndicatorBuilder>());
	BrushStampIndicator = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UBrushStampIndicator>(BaseBrushIndicatorGizmoType, FString(), this);
}

void UBaseBrushTool::UpdateBrushStampIndicator()
{
	BrushStampIndicator->Update(LastBrushStamp.Radius, LastBrushStamp.WorldPosition, LastBrushStamp.WorldNormal);
}

void UBaseBrushTool::ShutdownBrushStampIndicator()
{
	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);
	BrushStampIndicator = nullptr;
	GetToolManager()->GetPairedGizmoManager()->DeregisterGizmoType(BaseBrushIndicatorGizmoType);
}


#undef LOCTEXT_NAMESPACE
