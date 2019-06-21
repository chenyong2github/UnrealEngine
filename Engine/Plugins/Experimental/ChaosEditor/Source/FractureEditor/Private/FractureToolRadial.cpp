// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureToolRadial.h"

#include "FractureEditorStyle.h"

#define LOCTEXT_NAMESPACE "FractureRadial"

void UFractureRadialSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureRadialSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}


UFractureToolRadial::UFractureToolRadial(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	GetMutableDefault<UFractureRadialSettings>()->OwnerTool = this;
}


FText UFractureToolRadial::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolRadial", "Radial Voronoi")); 
}

FText UFractureToolRadial::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolRadialTooltip", "Radial Voronoi Fracture")); 
}

FSlateIcon UFractureToolRadial::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Radial");
}

void UFractureToolRadial::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, 
					UICommandInfo,
					"Radial", 
					"Radial",
					"Radial Voronoi Fracture",
					EUserInterfaceActionType::ToggleButton, 
					FInputChord() 
	);

	BindingContext->Radial = UICommandInfo;

}

TArray<UObject*> UFractureToolRadial::GetSettingsObjects() const 
{ 
	TArray<UObject*> Settings; 
	Settings.Add(GetMutableDefault<UFractureRadialSettings>());
	return Settings;
}

void UFractureToolRadial::GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites)
{
 	const UFractureRadialSettings* FractureSettings = GetMutableDefault<UFractureRadialSettings>();

	float RadialStep = FractureSettings->Radius / FractureSettings->RadialSteps;

	const FVector Center(Context.Bounds.GetCenter());

	FRandomStream RandStream(Context.RandomSeed);

	for (int32 ii = 1; ii < FractureSettings->RadialSteps; ++ii)
	{
		FVector PositionVector(RadialStep * ii,0.0,0.0);
		float AngularStep = 360.f / FractureSettings->AngularSteps;
		PositionVector = PositionVector.RotateAngleAxis(FractureSettings->AngleOffset * ii, FractureSettings->Normal);

		for (int32 kk = 0; kk < FractureSettings->AngularSteps; ++kk)
		{
			PositionVector = PositionVector.RotateAngleAxis(AngularStep , FractureSettings->Normal);
			Sites.Emplace(Center + PositionVector + (RandStream.VRand() * RandStream.FRand() * FractureSettings->Variability));
		}
	}
}

#undef LOCTEXT_NAMESPACE
