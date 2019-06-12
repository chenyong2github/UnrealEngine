// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AutoClusterFracture.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/Selection.h"
// #include "Framework/Commands/UIAction.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "EditorSupportDelegates.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
// #include "FractureCommon.h"

#define LOCTEXT_NAMESPACE "ClusterMeshCommand"

DEFINE_LOG_CATEGORY(LogAutoClusterCommand);


void UAutoClusterFractureCommand::ClusterChildBonesOfASingleMesh(UGeometryCollectionComponent* GeometryCollectionComponent, EFractureAutoClusterMode ClusterMode, int32 ClusterSiteCount)
{
	int8 FractureLevel = 1;

	ClusterSelectedBones(FractureLevel, ClusterMode, ClusterSiteCount, GeometryCollectionComponent);
}


FBox UAutoClusterFractureCommand::GetChildVolume(const TManagedArray<TSet<int32>>& Children, const TArray<FTransform>& Transforms, const TArray<int32>& TransformToGeometry, const TManagedArray<FBox>& BoundingBoxes, int32 Element)
{
	FBox ReturnBounds;
	ReturnBounds.Init();

	int32 GeometryIndex = TransformToGeometry[Element];
	if (GeometryIndex > -1)
	{
		const FBox& BoneBounds = BoundingBoxes[GeometryIndex];
		ReturnBounds += BoneBounds.TransformBy(Transforms[Element]);
	}
	
	for(int32 ChildElement : Children[Element] )
	{ 
		ReturnBounds += GetChildVolume(Children, Transforms, TransformToGeometry, BoundingBoxes, ChildElement);
	}

	return ReturnBounds;

}


void UAutoClusterFractureCommand::ClusterSelectedBones(int FractureLevel, EFractureAutoClusterMode AutoClusterGroupMode, int NumClusters, UGeometryCollectionComponent* GeometryCollectionComponent)
{
	check(GeometryCollectionComponent);

	if (FractureLevel > 0)
	{
		FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				TManagedArray<int32>& Level = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

				TArray<FTransform> Transforms;
				GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transforms);

				TArray<int32> TransformToGeometry;
				GeometryCollectionAlgo::BuildTransformGroupToGeometryGroupMap(*GeometryCollection, TransformToGeometry);

				const TManagedArray<FBox>& BoundingBoxes = GeometryCollection->BoundingBox;

				TMap<int32, FVector> BoneLocationMap;
				TMultiMap<float, int32> VolumeToElement;
				TMap<int32, int32> BoneToGroup;
				TMap<int32, FBox> WorldBounds;
				for (int32 Element = 0, NumElement = Level.Num(); Element < NumElement; ++Element)
				{
					if (Level[Element] == FractureLevel)
					{
						FBox BoneBounds = GetChildVolume(GeometryCollection->Children, Transforms, TransformToGeometry, BoundingBoxes, Element);
						VolumeToElement.Add(BoneBounds.GetVolume(), Element);
						BoneLocationMap.Add(Element, BoneBounds.GetCenter());
						BoneToGroup.Add(Element, -1);
						WorldBounds.Add(Element, BoneBounds);
					}
				}

				if (BoneToGroup.Num() < NumClusters)
				{
					return;
				}

				if (2 <= GeometryCollection->NumElements(FGeometryCollection::GeometryGroup))
				{
					FGeometryCollectionProximityUtility::UpdateProximity(GeometryCollection);
				}

				// bin elements by bconnectivity
				int32 GroupCount = 0;
				for (auto &Element : BoneToGroup)
				{
					if (Element.Value < 0)
					{
						if (AutoClusterGroupMode == EFractureAutoClusterMode::Proximity)
						{
							if (GeometryCollection->HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
							{
								TManagedArray<TSet<int32>>& Proximity = GeometryCollection->GetAttribute<TSet<int32>>("Proximity", FGeometryCollection::GeometryGroup);
								FloodProximity(FractureLevel, GroupCount++, Element.Key, BoneToGroup, TransformToGeometry, GeometryCollection->TransformIndex, Level, Proximity);
							}
						}
						else if (AutoClusterGroupMode == EFractureAutoClusterMode::BoundingBox)
						{
							FloodFill(FractureLevel, GroupCount++, Element.Key, BoneToGroup, Level, WorldBounds);
						}
						else
						{
							FloodFill(FractureLevel, GroupCount++, Element.Key, BoneToGroup, Level, WorldBounds, 0.2f);
						}
					}
				}

				// sort ALL the elements by volume.  largest to smallest
				VolumeToElement.KeySort([](float A, float B)
				{
					return A > B; // sort keys in reverse
				});

				// Bin them into arrays per group.  Sorted by volume largest to smallest
				TArray<TArray<int32>> GroupElementsByVolume;
				GroupElementsByVolume.AddZeroed(GroupCount);
				TArray<float> GroupVolumes;
				GroupVolumes.AddZeroed(GroupCount);
				float TotalVolume = 0.0f;
				for (auto& Entry : VolumeToElement)
				{
					int32 BoneIndex = Entry.Value;
					int32 BoneGroup = BoneToGroup[BoneIndex];
					GroupElementsByVolume[BoneGroup].Add(BoneIndex);
					GroupVolumes[BoneGroup] += Entry.Key;
					TotalVolume += Entry.Key;
				}

				NumClusters = FMath::Max(GroupCount, NumClusters);

				TArray<TArray<TTuple<int32,FVector>>> LargestVolumeBoneLocationsByGroup;
				LargestVolumeBoneLocationsByGroup.AddZeroed(GroupCount);
				TArray<int32> LocationsPerGroup;
				LocationsPerGroup.AddZeroed(GroupCount);

				// Make sure there is at least one location for every group
				int32 SitesGenerated = 0;
				for (int32 ii = 0, ni = GroupElementsByVolume.Num(); ii < ni; ++ii)
				{
					LocationsPerGroup[ii] = 1;
					SitesGenerated++;
				}

				// if we have more to distribute, do it by volume.
				if (SitesGenerated < NumClusters)
				{
					NumClusters -= SitesGenerated;

					for (int32 ii = 0, ni = GroupElementsByVolume.Num(); ii < ni; ++ii)
					{
						float PercentOfWhole = (GroupVolumes[ii] / TotalVolume);
						int32 NumClustersInGroup = FMath::RoundToInt(PercentOfWhole * (float)NumClusters);
						LocationsPerGroup[ii] += NumClustersInGroup;
						SitesGenerated += NumClustersInGroup;
					}
				}

				for (int32 ii = 0, ni = GroupElementsByVolume.Num() ; ii < ni ; ++ii)
				{
					TArray<int32>& GroupElements = GroupElementsByVolume[ii];
					// Make sure we take into account if we've allocated more locations than actual items in the group.
					for (int32 kk = 0, nk = FMath::Min(LocationsPerGroup[ii], GroupElements.Num()) ; kk < nk ; ++kk)
					{
						int32 Value = GroupElements[kk];
						if (AutoClusterGroupMode == EFractureAutoClusterMode::Distance)
						{
							Value = GroupElements[GroupElements.Num() * ((float)kk /(float)nk)];
						}

						const FVector &Location = BoneLocationMap[Value];
						LargestVolumeBoneLocationsByGroup[ii].Add(MakeTuple(Value, Location));
					}
				}

				for (int32 ii = 0; ii < GroupCount; ++ii)
				{
					TArray<TArray<int32>> SiteToBone;
					TArray<int32> BoneToSite;
					if (AutoClusterGroupMode == EFractureAutoClusterMode::Distance)
					{
						ClusterToNearestSiteInGroup(FractureLevel, GeometryCollectionComponent, BoneLocationMap, LargestVolumeBoneLocationsByGroup[ii], BoneToGroup, ii, SiteToBone, BoneToSite, WorldBounds);
					}
					else
					{
						ClusterToNearestSiteInGroup(FractureLevel, GeometryCollectionComponent, BoneLocationMap, LargestVolumeBoneLocationsByGroup[ii], BoneToGroup, ii, SiteToBone, BoneToSite);
					}

					for (int32 SiteIndex = 0, NumSites = LargestVolumeBoneLocationsByGroup[ii].Num(); SiteIndex < NumSites; ++SiteIndex)
					{
						if (SiteToBone[SiteIndex].Num() > 0)
						{
							FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(GeometryCollection, SiteToBone[SiteIndex][0], SiteToBone[SiteIndex], false, false);
						}
					}
				}

				FGeometryCollectionClusteringUtility::ValidateResults(GeometryCollection);

				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
				FEditorSupportDelegates::RedrawAllViewports.Broadcast();
			}
		}
	}
}

void UAutoClusterFractureCommand::ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int, FVector>& Locations, const TArray<TTuple<int32,FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite)
{
	FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
	if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			SiteToBone.AddDefaulted(Sites.Num());
			BoneToSite.AddZeroed(GeometryCollection->Parent.Num());
			for (const auto& location : Locations)
			{
				if (BoneToGroup[location.Key] == Group)
				{
					int NearestSite = FindNearestSitetoBone(location.Value, Sites);
					if (NearestSite >= 0)
					{
						SiteToBone[NearestSite].Push(location.Key);
						BoneToSite[location.Key] = NearestSite;
					}
				}
			}
		}
	}
}


void UAutoClusterFractureCommand::ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int, FVector>& Locations, const TArray<TTuple<int32, FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite, TMap<int32, FBox>& WorldBounds)
{
	FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
	if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			SiteToBone.AddDefaulted(Sites.Num());
			BoneToSite.AddZeroed(GeometryCollection->Parent.Num());
			for (const auto& location : Locations)
			{
				if (BoneToGroup[location.Key] == Group)
				{
					int NearestSite = FindNearestSitetoBounds(WorldBounds[location.Key], Sites, WorldBounds);
					if (NearestSite >= 0)
					{
						SiteToBone[NearestSite].Push(location.Key);
						BoneToSite[location.Key] = NearestSite;
					}
				}
			}
		}
	}
}

int UAutoClusterFractureCommand::FindNearestSitetoBone(const FVector& BoneLocation, const TArray<TTuple<int32,FVector>>& Sites)
{
	// brute force search
	int ClosestSite = -1;
	float ClosestDistSqr = FLT_MAX;
	for (int SiteIndex = 0, NumSites = Sites.Num(); SiteIndex < NumSites; ++SiteIndex)
	{
		const FVector& SiteLocation = Sites[SiteIndex].Value;
		float DistanceSqr = FVector::DistSquared(SiteLocation, BoneLocation);
		if (DistanceSqr < ClosestDistSqr)
		{
			ClosestDistSqr = DistanceSqr;
			ClosestSite = SiteIndex;
		}
	}

	return ClosestSite;
}

int UAutoClusterFractureCommand::FindNearestSitetoBounds(const FBox& Bounds, const TArray<TTuple<int32, FVector>>& Sites, TMap<int32, FBox>& WorldBounds)
{
	// brute force search
	int ClosestSite = -1;
	float ClosestDistSqr = FLT_MAX;
	for (int SiteIndex = 0, NumSites = Sites.Num(); SiteIndex < NumSites; ++SiteIndex)
	{
		const int32 SiteKey = Sites[SiteIndex].Key;
		const FBox& SiteBounds = WorldBounds[SiteKey];
		float DistanceSqr = GetClosestDistance(Bounds, SiteBounds);
		if (DistanceSqr < ClosestDistSqr)
		{
			ClosestDistSqr = DistanceSqr;
			ClosestSite = SiteIndex;
		}
	}

	return ClosestSite;

}

void UAutoClusterFractureCommand::FloodFill(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &BoneToGroup, const TManagedArray<int32>& Levels, const TMap<int32,FBox>& BoundingBoxes, float ExpandBounds)
{
	if (Levels[BoneIndex] != FractureLevel)
	{
		return;
	}

	if (BoneToGroup[BoneIndex] > -1)
	{
		return;
	}

	BoneToGroup[BoneIndex] = CurrentGroup;

	FBox CurrentBoneBounds = BoundingBoxes[BoneIndex].ExpandBy(BoundingBoxes[BoneIndex].GetSize() * ExpandBounds);

	for (auto &BoneGroup : BoneToGroup)
	{
		if (BoneGroup.Value < 0 && BoneGroup.Key != BoneIndex) //ungrouped
		{
			FBox BoneBounds = BoundingBoxes[BoneGroup.Key];
			if (CurrentBoneBounds.Intersect(BoneBounds))
			{
				FloodFill(FractureLevel, CurrentGroup, BoneGroup.Key, BoneToGroup, Levels, BoundingBoxes, ExpandBounds);
			}
		}
	}
}

void UAutoClusterFractureCommand::FloodProximity(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &ElementToGroup, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<int32>& Levels, const TManagedArray<TSet<int32>>& Proximity)
{
	if (Levels[BoneIndex] != FractureLevel)
	{
		return;
	}

	if (ElementToGroup[BoneIndex] > -1)
	{
		return;
	}

	ElementToGroup[BoneIndex] = CurrentGroup;

	int32 GeometryIndex = TransformToGeometry[BoneIndex];

	if (GeometryIndex < 0)
	{
		return;
	}


	check(GeometryIndex < Proximity.Num());
	const TSet<int32> &ProximityToThis = Proximity[GeometryIndex];

	for (int32 ProxInGeometry : ProximityToThis)
	{
		int32 ProxInTransform = GeometryToTransform[ProxInGeometry];
		if (Levels[ProxInTransform] != FractureLevel)
		{
			continue;
		}

		check(ElementToGroup.Contains(ProxInTransform));
		const int32 BoneGroup = ElementToGroup[ProxInTransform];
		if (BoneGroup < 0 && ProxInTransform != BoneIndex) //ungrouped
		{
			FloodProximity(FractureLevel, CurrentGroup, ProxInTransform, ElementToGroup, TransformToGeometry, GeometryToTransform, Levels, Proximity);
		}
	}
}

bool UAutoClusterFractureCommand::HasPath(int32 TransformIndexStart, int32 TransformIndexGoal, const TArray<int32>& BoneToSite, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<TSet<int32>>& Proximity)
{
	if (TransformIndexStart == TransformIndexGoal)
	{
		return true;
	}

	int32  GeometryStart = TransformToGeometry[TransformIndexStart];
	int32  GeometryGoal = TransformToGeometry[TransformIndexGoal];

	TArray<int32> VisitedGeometry;
	VisitedGeometry.AddZeroed(Proximity.Num());

	VisitedGeometry[GeometryStart] = 1;

	TSet<int32> FrontierGeometry = Proximity[TransformToGeometry[TransformIndexStart]];

	while (FrontierGeometry.Num())
	{
		int32 CurrentGeometry = FrontierGeometry.Array()[0];
		FrontierGeometry.Remove(CurrentGeometry);

		if (CurrentGeometry == GeometryGoal)
			return true;

		if (VisitedGeometry[CurrentGeometry] > 0)
		{
			continue;
		}

		VisitedGeometry[CurrentGeometry] = 1;

		for (int32 NextGeometry : Proximity[CurrentGeometry])
		{
			int32 NextTransform = GeometryToTransform[NextGeometry];
			if (!VisitedGeometry[NextGeometry])
			{
				if (BoneToSite[TransformIndexGoal] == BoneToSite[NextTransform]) // only follow if same site
				{
					FrontierGeometry.Add(NextGeometry);
				}
			}
		}
	}
	return false;
}

float UAutoClusterFractureCommand::GetClosestDistance(const FBox& A, const FBox& B)
{
	float Dist[8] = {
		B.ComputeSquaredDistanceToPoint(FVector(A.Min.X, A.Min.Y, A.Min.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Min.X, A.Max.Y, A.Min.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Max.X, A.Min.Y, A.Min.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Max.X, A.Max.Y, A.Min.Z)),

		B.ComputeSquaredDistanceToPoint(FVector(A.Min.X, A.Min.Y, A.Max.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Min.X, A.Max.Y, A.Max.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Max.X, A.Min.Y, A.Max.Z)),
		B.ComputeSquaredDistanceToPoint(FVector(A.Max.X, A.Max.Y, A.Max.Z)) 
	};

	float Distance = FLT_MAX;

	for (int32 ii = 0; ii < 8; ++ii)
	{
		if (Dist[ii] < Distance)
		{
			Distance = Dist[ii];
		}
	}

	return Distance;
}


#undef LOCTEXT_NAMESPACE
