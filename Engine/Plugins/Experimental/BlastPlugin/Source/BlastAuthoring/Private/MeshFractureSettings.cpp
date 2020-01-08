// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshFractureSettings.h"
#include "UnrealEdMisc.h"

#include "Engine/Selection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"


float UMeshFractureSettings::ExplodedViewExpansion = 0.0f;


static TArray<AActor*> GetSelectedActors()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> Actors;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			Actors.Add(Actor);
		}
	}
	return Actors;
}

UCommonFractureSettings::UCommonFractureSettings() : ViewMode(EMeshFractureLevel::AllLevels)
	, ShowBoneColors(true), DeleteSourceMesh(true), AutoClusterGroupMode(EMeshAutoClusterMode::BoundingBox), FractureMode(EMeshFractureMode::Uniform), RemoveIslands(true), RandomSeed(-1), ChanceToFracture(1.0), bGroupFracture(true), RetainUnfracturedMeshes(true), bCancelOnBadGeo(false), bThreadedFracture(false), bHealHoles(false)
{

}

UUniformFractureSettings::UUniformFractureSettings() : NumberVoronoiSitesMin(10), NumberVoronoiSitesMax(10)
{

}

UClusterFractureSettings::UClusterFractureSettings() : NumberClustersMin(8), NumberClustersMax(8), SitesPerClusterMin(2), SitesPerClusterMax(30), ClusterRadiusPercentageMin(0.1), ClusterRadiusPercentageMax(0.2), ClusterRadius(0.0f)
{

}

URadialFractureSettings::URadialFractureSettings() : Center(FVector(0,0,0)), Normal(FVector(0, 0, 1)), Radius(50.0f), AngularSteps(5), RadialSteps(5), AngleOffset(0.0f), Variability(0.0f)
{

}

USlicingFractureSettings::USlicingFractureSettings() : SlicesX(3), SlicesY(3), SlicesZ(3), SliceAngleVariation(0.0f), SliceOffsetVariation(0.0f), Amplitude(0.02), Frequency(5.0), OctaveNumber(2), SurfaceResolution(50)
{

}

UPlaneCut::UPlaneCut() : Position(FVector(0,0,0)), Normal(FVector(0, 0, 1))
{

}

UPlaneCutFractureSettings::UPlaneCutFractureSettings() : NumberOfCuts(3), CutChunkChance(1.0), Amplitude(0.0), Frequency(5.0), OctaveNumber(2), SurfaceResolution(50)
{

}

UCutoutFractureSettings::UCutoutFractureSettings() : Transform(FTransform::Identity), Scale(FVector2D(-1, -1)), IsRelativeTransform(true), SnapThreshold(4.0f), SegmentationErrorThreshold(0.001f)
{

}

UBrickFractureSettings::UBrickFractureSettings() : Forward(EMeshFractureBrickProjection::X), Up(EMeshFractureBrickProjection::Z), BrickLength(80.0f), /*BrickWidth(40.0f),*/ BrickHeight(30.0f), Amplitude(0.02), Frequency(5.0), OctaveNumber(2), SurfaceResolution(50)
{

}


UMeshFractureSettings::UMeshFractureSettings()
{
	CommonSettings = NewObject<UCommonFractureSettings>(GetTransientPackage(), TEXT("CommonSettings"));
	CommonSettings->AddToRoot();

	UniformSettings = NewObject<UUniformFractureSettings>(GetTransientPackage(), TEXT("UniformSettings"));
	UniformSettings->AddToRoot();

	ClusterSettings = NewObject<UClusterFractureSettings>(GetTransientPackage(), TEXT("ClusterSettings"));
	ClusterSettings->AddToRoot();

	RadialSettings = NewObject<URadialFractureSettings>(GetTransientPackage(), TEXT("RadialSettings"));
	RadialSettings->AddToRoot();

	SlicingSettings = NewObject<USlicingFractureSettings>(GetTransientPackage(), TEXT("SlicingSettings"));
	SlicingSettings->AddToRoot();

	PlaneCutSettings = NewObject<UPlaneCutFractureSettings>(GetTransientPackage(), TEXT("PlaneCutSettings"));
	PlaneCutSettings->AddToRoot();

	CutoutSettings = NewObject<UCutoutFractureSettings>(GetTransientPackage(), TEXT("CutoutSettings"));
	CutoutSettings->AddToRoot();

	BrickSettings = NewObject<UBrickFractureSettings>(GetTransientPackage(), TEXT("BrickSettings"));
	BrickSettings->AddToRoot();
}

UMeshFractureSettings::~UMeshFractureSettings()
{
	if (CommonSettings)
	{
		CommonSettings->RemoveFromRoot();
		CommonSettings = nullptr;
	}

	if (UniformSettings)
	{
		UniformSettings->RemoveFromRoot();
		UniformSettings = nullptr;
	}

	if (ClusterSettings)
	{
		ClusterSettings->RemoveFromRoot();
		ClusterSettings = nullptr;
	}

	if (RadialSettings)
	{
		RadialSettings->RemoveFromRoot();
		RadialSettings = nullptr;
	}

	if (SlicingSettings)
	{
		SlicingSettings->RemoveFromRoot();
		SlicingSettings = nullptr;
	}

	if (PlaneCutSettings)
	{
		PlaneCutSettings->RemoveFromRoot();
		PlaneCutSettings = nullptr;
	}

	if (CutoutSettings)
	{
		CutoutSettings->RemoveFromRoot();
		CutoutSettings = nullptr;
	}
}
