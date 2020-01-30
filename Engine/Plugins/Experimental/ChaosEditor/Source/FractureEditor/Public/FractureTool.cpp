// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureTool.h"

#if WITH_EDITOR

UFractureCommonSettings::UFractureCommonSettings(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
	, RandomSeed(-1)
	, ChanceToFracture(1.0)
	, bGroupFracture(true)
	, bDrawSites(false)
	, bDrawDiagram(true)
	, Amplitude(0.0f)
	, Frequency(0.1f)
	, OctaveNumber(4)
	, SurfaceResolution(10)
{}

void UFractureCommonSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureCommonSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent); 
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

const TSharedPtr<FUICommandInfo>& UFractureTool::GetUICommandInfo() const
{
	return UICommandInfo;
}

#endif

