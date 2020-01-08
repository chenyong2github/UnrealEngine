// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolUniform.h"

#include "FractureEditorStyle.h"
#include "FractureEditorCommands.h"

#define LOCTEXT_NAMESPACE "FractureUniform"

void UFractureUniformSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureUniformSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}


UFractureToolUniform::UFractureToolUniform(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	Settings = NewObject<UFractureUniformSettings>(GetTransientPackage(), UFractureUniformSettings::StaticClass());
	Settings->OwnerTool = this;
}

FText UFractureToolUniform::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolUniform", "Uniform Voronoi")); 
}

FText UFractureToolUniform::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolUniformTooltip", "Uniform Voronoi Fracture")); 
}

FSlateIcon UFractureToolUniform::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Uniform");
}

void UFractureToolUniform::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Uniform", "Uniform", "Uniform Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Uniform = UICommandInfo;
}

TArray<UObject*> UFractureToolUniform::GetSettingsObjects() const 
{ 
	TArray<UObject*> AllSettings; 
	AllSettings.Add(GetMutableDefault<UFractureCommonSettings>());
	AllSettings.Add(Settings);
	return AllSettings;
}

void UFractureToolUniform::GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites)
{
	FRandomStream RandStream(Context.RandomSeed);

	const FVector Extent(Context.Bounds.Max - Context.Bounds.Min);

	const int32 SiteCount = RandStream.RandRange(Settings->NumberVoronoiSitesMin, Settings->NumberVoronoiSitesMax);

	Sites.Reserve(Sites.Num() + SiteCount);
	for (int32 ii = 0; ii < SiteCount; ++ii)
	{
		Sites.Emplace(Context.Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent );
	}
}

#undef LOCTEXT_NAMESPACE
