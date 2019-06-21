// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "FractureToolCluster.h"

#include "FractureEditorStyle.h"

#define LOCTEXT_NAMESPACE "FractureClustered"

void UFractureClusterSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UFractureClusterSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (OwnerTool != nullptr)
	{
		OwnerTool->PostEditChangeChainProperty(PropertyChangedEvent);
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}



UFractureToolCluster::UFractureToolCluster(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	Settings = NewObject<UFractureClusterSettings>(GetTransientPackage(), UFractureClusterSettings::StaticClass());
	Settings->OwnerTool = this;
}

FText UFractureToolCluster::GetDisplayText() const
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolCluster", "Cluster Voronoi")); 
}

FText UFractureToolCluster::GetTooltipText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "FractureToolClusterTooltip", "Cluster Voronoi Fracture")); 
}

FSlateIcon UFractureToolCluster::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Clustered");
}

void UFractureToolCluster::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Clustered", "Clustered", "Clustered Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Clustered = UICommandInfo;
}

TArray<UObject*> UFractureToolCluster::GetSettingsObjects() const 
{ 
	TArray<UObject*> ReturnSettings; 
	ReturnSettings.Add(GetMutableDefault<UFractureClusterSettings>());
	return ReturnSettings;
}

void UFractureToolCluster::GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites)
{
 	const UFractureCommonSettings* LocalCommonSettings = GetDefault<UFractureCommonSettings>();

	FRandomStream RandStream(Context.RandomSeed);
	const int32 SiteCount = RandStream.RandRange(Settings->NumberClustersMin, Settings->NumberClustersMax);

	const FVector Extent(Context.Bounds.Max - Context.Bounds.Min);

	TArray<FVector> CenterSites;

	Sites.Reserve(Sites.Num() + SiteCount);
	for (int32 ii = 0; ii < Settings->NumberClustersMax; ++ii)
	{
		CenterSites.Emplace(Context.Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
	}

	for (int32 kk = 0, nk = CenterSites.Num(); kk < nk; ++kk)
	{
		const int32 SubSiteCount = RandStream.RandRange(Settings->SitesPerClusterMin, Settings->SitesPerClusterMax);

		for (int32 ii = 0; ii < SubSiteCount; ++ii)
		{
			FVector V(RandStream.VRand());
			V.Normalize();
			V *= Settings->ClusterRadius + (RandStream.FRandRange(Settings->ClusterRadiusPercentageMin, Settings->ClusterRadiusPercentageMax) * Context.Bounds.GetExtent().GetAbsMax());
			V += CenterSites[kk];
			Sites.Emplace(V);
		}
	}
}

#undef LOCTEXT_NAMESPACE