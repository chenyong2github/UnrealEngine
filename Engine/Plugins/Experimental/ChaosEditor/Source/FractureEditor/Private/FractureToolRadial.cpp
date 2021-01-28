// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolRadial.h"

#include "FractureToolContext.h"


#define LOCTEXT_NAMESPACE "FractureRadial"


UFractureToolRadial::UFractureToolRadial(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	RadialSettings = NewObject<UFractureRadialSettings>(GetTransientPackage(), UFractureRadialSettings::StaticClass());
	RadialSettings->OwnerTool = this;
}

FText UFractureToolRadial::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolRadial", "Radial Voronoi Fracture")); 
}

FText UFractureToolRadial::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolRadialTooltip", "Radial Voronoi Fracture create a radial distribution of Voronoi cells from a center point (for example, a wrecking ball crashing into a wall).  Click the Fracture Button to commit the fracture to the geometry collection."));
}

FSlateIcon UFractureToolRadial::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Radial");
}

void UFractureToolRadial::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Radial", "Radial", "Radial Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Radial = UICommandInfo;
}

TArray<UObject*> UFractureToolRadial::GetSettingsObjects() const 
{ 
	TArray<UObject*> Settings; 
	Settings.Add(CutterSettings);
	Settings.Add(CollisionSettings);
	Settings.Add(RadialSettings);
	return Settings;
}

void UFractureToolRadial::GenerateVoronoiSites(const FFractureToolContext& Context, TArray<FVector>& Sites)
{
 	float RadialStep = RadialSettings->Radius / RadialSettings->RadialSteps;

	const FVector Center(Context.GetBounds().GetCenter() + RadialSettings->Center);

	FRandomStream RandStream(Context.GetSeed());
	FVector UpVector(RadialSettings->Normal);
	UpVector.Normalize();
	FVector PerpVector(UpVector[2], UpVector[0], UpVector[1]);

	for (int32 ii = 1; ii < RadialSettings->RadialSteps; ++ii)
	{
		FVector PositionVector(PerpVector * RadialStep * ii);

		float AngularStep = 360.f / RadialSettings->AngularSteps;
		PositionVector = PositionVector.RotateAngleAxis(RadialSettings->AngleOffset * ii, UpVector);

		for (int32 kk = 0; kk < RadialSettings->AngularSteps; ++kk)
		{
			PositionVector = PositionVector.RotateAngleAxis(AngularStep , UpVector);
			Sites.Emplace(Center + PositionVector + (RandStream.VRand() * RandStream.FRand() * RadialSettings->Variability));
		}
	}
}

#undef LOCTEXT_NAMESPACE
