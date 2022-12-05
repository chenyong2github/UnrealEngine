// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEngineEdit.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "Algo/RemoveIf.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"

#include "PlanarCut.h"


 
void FFractureEngineEdit::DeleteBranch(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection)
{
	const FManagedArrayCollection& InCollection = (const FManagedArrayCollection&)GeometryCollection;
	GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

	TArray<int32> BoneIndicies = InBoneSelection;
	TransformSelectionFacade.RemoveRootNodes(BoneIndicies);
	TransformSelectionFacade.Sanitize(BoneIndicies);

	TArray<int32> NodesForDeletion;
	const TManagedArray<TSet<int32>>& Children = InCollection.GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);

	for (int32 BoneIdx : BoneIndicies)
	{
		FGeometryCollectionClusteringUtility::RecursiveAddAllChildren(Children, BoneIdx, NodesForDeletion);
	}

	NodesForDeletion.Sort();
	GeometryCollection.RemoveElements(FGeometryCollection::TransformGroup, NodesForDeletion);

	FGeometryCollectionClusteringUtility::RemoveDanglingClusters(&GeometryCollection);

	// Proximity is invalidated
	if (GeometryCollection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		GeometryCollection.RemoveAttribute("Proximity", FGeometryCollection::GeometryGroup);
	}
}


void FFractureEngineEdit::SetVisibilityInCollection(FManagedArrayCollection& InCollection, const TArray<int32>& InBoneSelection, bool bVisible)
{
	GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

	const TManagedArray<int32>& TransformToGeometryIndex = InCollection.GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::GeometryGroup);
	const TManagedArray<int32>& FaceStart = InCollection.GetAttribute<int32>("FaceStart", FGeometryCollection::GeometryGroup);
	const TManagedArray<int32>& FaceCount = InCollection.GetAttribute<int32>("FaceCount", FGeometryCollection::GeometryGroup);
	TManagedArray<bool>* Visible = InCollection.FindAttribute<bool>("Visible", FGeometryCollection::GeometryGroup);

	TArray<int32> BoneIndicies = InBoneSelection;
	TransformSelectionFacade.ConvertSelectionToRigidNodes(BoneIndicies);

	for (int32 Index : BoneIndicies)
	{
		// Iterate the faces in the geometry of this rigid node and set invisible.
		if (TransformToGeometryIndex[Index] > INDEX_NONE)
		{
			int32 CurrFace = FaceStart[TransformToGeometryIndex[Index]];
			for (int32 FaceOffset = 0; FaceOffset < FaceCount[TransformToGeometryIndex[Index]]; ++FaceOffset)
			{
				(*Visible)[CurrFace + FaceOffset] = bVisible;
			}
		}
	}
}


void FFractureEngineEdit::Merge(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection)
{
	const FManagedArrayCollection& InCollection = (const FManagedArrayCollection&)GeometryCollection;
	GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

	TArray<int32> BoneIndicies = InBoneSelection;
	TransformSelectionFacade.Sanitize(BoneIndicies);

	const TArray<int32>& NodesForMerge = InBoneSelection;

	constexpr bool bBooleanUnion = false;
	MergeAllSelectedBones(GeometryCollection, NodesForMerge, bBooleanUnion);

	// Proximity is invalidated
	if (GeometryCollection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
	{
		GeometryCollection.RemoveAttribute("Proximity", FGeometryCollection::GeometryGroup);
	}
}


