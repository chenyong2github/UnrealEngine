// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBrushTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "BrushToolIndicator.h"

#define LOCTEXT_NAMESPACE "UBaseBrushTool"



UBrushBaseProperties::UBrushBaseProperties()
{
	BrushSize = 0.25f;
	bSpecifyRadius = false;
	BrushRadius = 0.0f;
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


	// create brush stamp indicator
	Indicators = NewObject<UToolIndicatorSet>(this, "Indicators");
	Indicators->Connect(this);
	Indicators->AddIndicator(MakeBrushIndicator());


	// initialize our properties
	AddToolPropertySource(BrushProperties);
}


void UBaseBrushTool::Shutdown(EToolShutdownType ShutdownType)
{
	Indicators->Disconnect();
}


void UBaseBrushTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	if (PropertySet == BrushProperties)
	{
		RecalculateBrushRadius();
	}
}


IToolIndicator* UBaseBrushTool::MakeBrushIndicator()
{
	UBrushStampSizeIndicator* StampIndicator = NewObject<UBrushStampSizeIndicator>(this, "Brush Circle");
	StampIndicator->bDrawSecondaryLines = true;
	StampIndicator->DepthLayer = 1;
	StampIndicator->BrushRadius = MakeAttributeLambda([this] { return this->LastBrushStamp.Radius; });
	StampIndicator->BrushPosition = MakeAttributeLambda([this] { return this->LastBrushStamp.WorldPosition; });
	StampIndicator->BrushNormal = MakeAttributeLambda([this] { return this->LastBrushStamp.WorldNormal; });
	return StampIndicator;
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


void UBaseBrushTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FHitResult OutHit;
	if (HitTest(DevicePos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties->BrushRadius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
	}
}



void UBaseBrushTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSurfacePointTool::Render(RenderAPI);
	Indicators->Render(RenderAPI);
}

void UBaseBrushTool::Tick(float DeltaTime)
{
	UMeshSurfacePointTool::Tick(DeltaTime);
	Indicators->Tick(DeltaTime);
}



#undef LOCTEXT_NAMESPACE
