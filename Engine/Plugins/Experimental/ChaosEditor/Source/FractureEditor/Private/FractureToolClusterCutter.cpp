// Copyright Epic Games, Inc. All Rights Reserved.


#include "FractureToolClusterCutter.h"

#include "FractureToolContext.h"

#define LOCTEXT_NAMESPACE "FractureClustered"


UFractureToolClusterCutter::UFractureToolClusterCutter(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	ClusterSettings = NewObject<UFractureClusterCutterSettings>(GetTransientPackage(), UFractureClusterCutterSettings::StaticClass());
	ClusterSettings->OwnerTool = this;
}

FText UFractureToolClusterCutter::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolCluster", "Cluster Voronoi Fracture"));
}

FText UFractureToolClusterCutter::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolClusterTooltip", "Cluster Voronoi Fracture creates additional points around a base Voronoi pattern, creating more variation.  Click the Fracture Button to commit the fracture to the geometry collection."));
}

FSlateIcon UFractureToolClusterCutter::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Clustered");
}

void UFractureToolClusterCutter::RegisterUICommand( FFractureEditorCommands* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Clustered", "Clustered", "Clustered Voronoi Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Clustered = UICommandInfo;
}

TArray<UObject*> UFractureToolClusterCutter::GetSettingsObjects() const
{
	TArray<UObject*> ReturnSettings;
	ReturnSettings.Add(CutterSettings);
	ReturnSettings.Add(CollisionSettings);
	ReturnSettings.Add(ClusterSettings);
	return ReturnSettings;
}

void UFractureToolClusterCutter::GenerateVoronoiSites(const FFractureToolContext& Context, TArray<FVector>& Sites)
{
 	FRandomStream RandStream(Context.GetSeed());
	const int32 SiteCount = RandStream.RandRange(ClusterSettings->NumberClustersMin, ClusterSettings->NumberClustersMax);

	const FVector Extent(Context.GetBounds().Max - Context.GetBounds().Min);

	TArray<FVector> CenterSites;

	Sites.Reserve(Sites.Num() + SiteCount);
	for (int32 ii = 0; ii < SiteCount; ++ii)
	{
		CenterSites.Emplace(Context.GetBounds().Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
	}

	for (int32 kk = 0, nk = CenterSites.Num(); kk < nk; ++kk)
	{
		const int32 SubSiteCount = RandStream.RandRange(ClusterSettings->SitesPerClusterMin, ClusterSettings->SitesPerClusterMax);

		for (int32 ii = 0; ii < SubSiteCount; ++ii)
		{
			FVector V(RandStream.VRand());
			V.Normalize();
			V *= ClusterSettings->ClusterRadius + (RandStream.FRandRange(ClusterSettings->ClusterRadiusPercentageMin, ClusterSettings->ClusterRadiusPercentageMax) * Context.GetBounds().GetExtent().GetAbsMax());
			V += CenterSites[kk];
			Sites.Emplace(V);
		}
	}
}

#undef LOCTEXT_NAMESPACE