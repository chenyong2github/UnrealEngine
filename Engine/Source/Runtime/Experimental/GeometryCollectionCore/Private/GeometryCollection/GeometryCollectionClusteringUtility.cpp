// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "Containers/Set.h"
#include "Async/ParallelFor.h"

void FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(FGeometryCollection* GeometryCollection, const int32 InsertAtIndex, const TArray<int32>& SelectedBones, bool CalcNewLocalTransform, bool Validate)
{
	check(GeometryCollection);


	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	// insert a new node between the selected bones and their shared parent
	int NewBoneIndex = GeometryCollection->AddElements(1, FGeometryCollection::TransformGroup);

	// New Bone Setup takes level/parent from the first of the Selected Bones
	int32 SourceBoneIndex = InsertAtIndex;
	int32 OriginalParentIndex = Parents[SourceBoneIndex];
	BoneNames[NewBoneIndex] = BoneNames[SourceBoneIndex];
	Parents[NewBoneIndex] = Parents[SourceBoneIndex];
	Children[NewBoneIndex] = TSet<int32>(SelectedBones);

	Transforms[NewBoneIndex] = FTransform::Identity;

	// re-parent all the geometry nodes under the new shared bone
	GeometryCollectionAlgo::ParentTransforms(GeometryCollection, NewBoneIndex, SelectedBones);

	UpdateHierarchyLevelOfChildren(GeometryCollection, NewBoneIndex);

	// Parent Bone Fixup of Children - add the new node under the first bone selected
	// #todo: might want to add it to the one closest to the root in the hierarchy
	if (OriginalParentIndex != FGeometryCollection::Invalid)
	{
		Children[OriginalParentIndex].Add(NewBoneIndex);
	}

	// update all the bone names from here on down the tree to the leaves
	if (Parents[NewBoneIndex] != FGeometryCollection::Invalid)
	{
		RecursivelyUpdateChildBoneNames(Parents[NewBoneIndex], Children, BoneNames);
	}
	else
	{
		// #todo: how should we get the appropriate actor's name or invent a name here?
		BoneNames[NewBoneIndex] = "ClusterBone";
		RecursivelyUpdateChildBoneNames(NewBoneIndex, Children, BoneNames);
	}

	//
	// determine original parents of moved nodes so we can update their childrens names
	//
	TArray<int32> ParentsToUpdateNames;
	for (int32 SourceElement : SelectedBones)
	{
		ParentsToUpdateNames.AddUnique(Parents[SourceElement]);
	}
	for (int32 NodeIndex : ParentsToUpdateNames)
	{
		RecursivelyUpdateChildBoneNames(NodeIndex, Children, BoneNames);
	}

	if (Validate)
	{
		ValidateResults(GeometryCollection);
	}
}

void FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);
	bool CalcNewLocalTransform = true;

	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	TArray<int32> ChildBones;
	int32 NumElements = GeometryCollection->NumElements(FGeometryCollection::TransformGroup);
	for (int ChildIndex = 0; ChildIndex < NumElements; ChildIndex++)
	{
		if (Parents[ChildIndex] == FGeometryCollection::Invalid)
			ChildBones.Push(ChildIndex);
	}

	// insert a new Root node
	int RootNoneIndex = GeometryCollection->AddElements(1, FGeometryCollection::TransformGroup);

	if (GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
	{
		TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

		// all bones shifted down one in hierarchy
		for (int ChildIndex = 0; ChildIndex < NumElements; ChildIndex++)
		{
			Levels[ChildIndex] += 1;
		}
		Levels[RootNoneIndex] = 0;
	}

	// New Bone Setup takes level/parent from the first of the Selected Bones
	BoneNames[RootNoneIndex] = "ClusterBone";
	Parents[RootNoneIndex] = FGeometryCollection::Invalid;
	Children[RootNoneIndex] = TSet<int32>(ChildBones);
	SimulationType[RootNoneIndex] = FGeometryCollection::ESimulationTypes::FST_Rigid;
	check(GeometryCollection->IsTransform(RootNoneIndex));

	if (GeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup) &&
		GeometryCollection->HasAttribute("ExplodedTransform", FGeometryCollection::TransformGroup) )
	{

		TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
		TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);

		FVector SumOfOffsets(0, 0, 0);
		for (int32 ChildBoneIndex : ChildBones)
		{
			ExplodedVectors[ChildBoneIndex] = Transforms[ChildBoneIndex].GetLocation();
			ExplodedTransforms[ChildBoneIndex] = Transforms[ChildBoneIndex];
			SumOfOffsets += ExplodedVectors[ChildBoneIndex];
		}
		ExplodedTransforms[RootNoneIndex] = Transforms[RootNoneIndex];
		// This bones offset is the average of all the selected bones
		ExplodedVectors[RootNoneIndex] = SumOfOffsets / ChildBones.Num();
	}

	// Selected Bone Setup
	for (int32 ChildBoneIndex : ChildBones)
	{
		Parents[ChildBoneIndex] = RootNoneIndex;
		GeometryCollection->SimulationType[ChildBoneIndex] = FGeometryCollection::ESimulationTypes::FST_Clustered;

	}

	Transforms[RootNoneIndex] = FTransform::Identity;


	RecursivelyUpdateChildBoneNames(RootNoneIndex, Children, BoneNames);

	ValidateResults(GeometryCollection);
}


void FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingRoot(FGeometryCollection* GeometryCollection, TArray<int32>& SourceElements)
{
	check(GeometryCollection);
	bool CalcNewLocalTransform = true;

	TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	TArray<int32> RootBonesOut;
	GetRootBones(GeometryCollection, RootBonesOut);
	check(RootBonesOut.Num() == 1); // only expecting a single root node
	int32 RootBoneElement = RootBonesOut[0];
	check(Levels[RootBoneElement] == 0);
	check(Parents[RootBoneElement] == FGeometryCollection::Invalid);

	// re-parent all the geometry nodes under the root node
	GeometryCollectionAlgo::ParentTransforms(GeometryCollection, RootBoneElement, SourceElements);

	// update source levels and transforms in our custom attributes
	for (int32 Element : SourceElements)
	{
		if (Element != RootBoneElement)
		{
			Levels[Element] = 1;
		}
	}

	// delete all the redundant transform nodes that we no longer use
	TArray<int32> NodesToDelete;
	for (int Element = 0; Element < GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Element++)
	{
		if (Element != RootBoneElement && GeometryCollection->IsTransform(Element))
		{
			NodesToDelete.Add(Element);
		}
	}

	if (NodesToDelete.Num() > 0)
	{
		NodesToDelete.Sort();
		FManagedArrayCollection::FProcessingParameters Params;
		Params.bDoValidation = false;
		GeometryCollection->RemoveElements(FGeometryCollection::TransformGroup, NodesToDelete, Params);
	}

	// the root bone index could have changed after the above RemoveElements
	RootBonesOut.Empty();
	GetRootBones(GeometryCollection, RootBonesOut);
	RootBoneElement = RootBonesOut[0];

	RecursivelyUpdateChildBoneNames(RootBoneElement, Children, BoneNames);

	ValidateResults(GeometryCollection);
}

void FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingNode(FGeometryCollection* GeometryCollection, const TArray<int32>& SourceElements)
{
	int32 MergeNode = PickBestNodeToMergeTo(GeometryCollection, SourceElements);
	ClusterBonesUnderExistingNode(GeometryCollection, MergeNode, SourceElements);

}

void FGeometryCollectionClusteringUtility::ClusterBonesUnderExistingNode(FGeometryCollection* GeometryCollection, int32 MergeNode, const TArray<int32>& SourceElementsIn)
{
	check(GeometryCollection);
	bool CalcNewLocalTransform = true;

	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
	TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);

	// remove Merge Node if it's in the list - happens due to the way selection works
	TArray<int32> SourceElements;
	for (int32 Element : SourceElementsIn)
	{
		if (Element != MergeNode)
		{
			SourceElements.Push(Element);
		}
	}

	if (MergeNode != FGeometryCollection::Invalid)
	{
		bool IllegalOperation = false;
		for (int32 SourceElement : SourceElements)
		{
			if (NodeExistsOnThisBranch(GeometryCollection, MergeNode, SourceElement))
			{
				IllegalOperation = true;
				break;
			}
		}

		if (!IllegalOperation)
		{
			TArray<int32> ParentsToUpdateNames;
			// determine original parents of moved nodes so we can update their childrens names
			for (int32 SourceElement : SourceElementsIn)
			{
				ParentsToUpdateNames.AddUnique(Parents[SourceElement]);
			}

			ResetSliderTransforms(ExplodedTransforms, Transforms);

			// re-parent all the geometry nodes under existing merge node
			GeometryCollectionAlgo::ParentTransforms(GeometryCollection, MergeNode, SourceElements);

			// update source levels and transforms in our custom attributes
			for (int32 Element : SourceElements)
			{
				ExplodedTransforms[Element] = Transforms[Element];
				ExplodedVectors[Element] = Transforms[Element].GetLocation();
			}

			UpdateHierarchyLevelOfChildren(GeometryCollection, MergeNode);

			RecursivelyUpdateChildBoneNames(MergeNode, Children, BoneNames);

			for (int32 NodeIndex : ParentsToUpdateNames)
			{
				RecursivelyUpdateChildBoneNames(NodeIndex, Children, BoneNames);
			}
		}
	}

	// add common root node if multiple roots found
	if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(GeometryCollection))
	{
		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(GeometryCollection);
	}

	ValidateResults(GeometryCollection);
}

void FGeometryCollectionClusteringUtility::ClusterBonesByContext(FGeometryCollection* GeometryCollection, int32 MergeNode, const TArray<int32>& SourceElementsIn)
{
	if (GeometryCollection->IsTransform(MergeNode))
	{
		ClusterBonesUnderExistingNode(GeometryCollection, MergeNode, SourceElementsIn);
	}
	else
	{
		TArray<int32> SourceElements = SourceElementsIn;
		SourceElements.Push(MergeNode);
		ClusterBonesUnderNewNode(GeometryCollection, MergeNode, SourceElements, true);
	}
}

void FGeometryCollectionClusteringUtility::CollapseHierarchyOneLevel(FGeometryCollection* GeometryCollection, TArray<int32>& SourceElements)
{
	check(GeometryCollection);
	bool CalcNewLocalTransform = true;

	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

	for (int32 SourceElement : SourceElements)
	{
		int32 DeletedNode = SourceElement;
		if (DeletedNode != FGeometryCollection::Invalid)
		{
			int32 NewParentElement = Parents[DeletedNode];

			if (NewParentElement != FGeometryCollection::Invalid)
			{
				for (int32 ChildElement : Children[DeletedNode])
				{
					Children[NewParentElement].Add(ChildElement);
					Levels[ChildElement] -= 1;
					Parents[ChildElement] = NewParentElement;
				}
				Children[DeletedNode].Empty();
			}
		}
	}

	SourceElements.Sort();
	GeometryCollection->RemoveElements(FGeometryCollection::TransformGroup, SourceElements);

	TArray<int32> Roots;
	GetRootBones(GeometryCollection, Roots);
	RecursivelyUpdateChildBoneNames(Roots[0], Children, BoneNames);

	ValidateResults(GeometryCollection);
}


bool FGeometryCollectionClusteringUtility::NodeExistsOnThisBranch(const FGeometryCollection* GeometryCollection, int32 TestNode, int32 TreeElement)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	if (TestNode == TreeElement)
		return true;

	if (Children[TreeElement].Num() > 0)
	{
		for (int32 ChildIndex : Children[TreeElement])
		{
			if (NodeExistsOnThisBranch(GeometryCollection, TestNode, ChildIndex))
				return true;
		}
	}

	return false;

}

void FGeometryCollectionClusteringUtility::RenameBone(FGeometryCollection* GeometryCollection, int32 BoneIndex, const FString& NewName, bool UpdateChildren /* = true */)
{
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	BoneNames[BoneIndex] = NewName;

	if (UpdateChildren)
	{
		FGeometryCollectionClusteringUtility::RecursivelyUpdateChildBoneNames(BoneIndex, Children, BoneNames, true);
	}
}

int32 FGeometryCollectionClusteringUtility::PickBestNodeToMergeTo(const FGeometryCollection* GeometryCollection, const TArray<int32>& SourceElements)
{
	const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	// which of the source elements is the most significant, closest to the root that has children (is a cluster)
	int32 ElementClosestToRoot = -1;
	int32 LevelClosestToRoot = -1;

	for (int32 Element : SourceElements)
	{
		if (Children[Element].Num() > 0 && (Levels[Element] < LevelClosestToRoot || LevelClosestToRoot == -1))
		{
			LevelClosestToRoot = Levels[Element];
			ElementClosestToRoot = Element;
		}
	}

	return ElementClosestToRoot;
}

void FGeometryCollectionClusteringUtility::ResetSliderTransforms(TManagedArray<FTransform>& ExplodedTransforms, TManagedArray<FTransform>& Transforms)
{
	for (int Element = 0; Element < Transforms.Num(); Element++)
	{
		Transforms[Element] = ExplodedTransforms[Element];
	}
}

bool FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;

	// never assume the root bone is always index 0 in the particle group
	int NumRootBones = 0;
	for (int i = 0; i < Parents.Num(); i++)
	{
		if (Parents[i] == FGeometryCollection::Invalid)
		{
			NumRootBones++;
			if (NumRootBones > 1)
			{
				return true;
			}
		}
	}
	return false;
}


void FGeometryCollectionClusteringUtility::GetRootBones(const FGeometryCollection* GeometryCollection, TArray<int32>& RootBonesOut)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;

	// never assume the root bone is always index 0 in the particle group
	for (int i = 0; i < Parents.Num(); i++)
	{
		if (Parents[i] == FGeometryCollection::Invalid)
		{
			RootBonesOut.AddUnique(i);
		}
	}
}

bool FGeometryCollectionClusteringUtility::IsARootBone(const FGeometryCollection* GeometryCollection, int32 InBone)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;

	return (Parents[InBone] == FGeometryCollection::Invalid);
}

void FGeometryCollectionClusteringUtility::GetClusteredBonesWithCommonParent(const FGeometryCollection* GeometryCollection, int32 SourceBone, TArray<int32>& BonesOut)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;

	// then see if this bone as any other bones clustered to it
	if (SimulationType[SourceBone] == FGeometryCollection::ESimulationTypes::FST_Clustered)
	{
		int32 SourceParent = Parents[SourceBone];

		for (int i = 0; i < Parents.Num(); i++)
		{
			if (SourceParent == Parents[i] && (SimulationType[i] == FGeometryCollection::ESimulationTypes::FST_Clustered))
				BonesOut.AddUnique(i);
		}
	}

}

void FGeometryCollectionClusteringUtility::GetChildBonesFromLevel(const FGeometryCollection* GeometryCollection, int32 SourceBone, int32 Level, TArray<int32>& BonesOut)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	if (SourceBone >= 0)
	{
		int32 SourceParent = SourceBone;
		while (Levels[SourceParent] > Level)
		{
			if (Parents[SourceParent] == -1)
				break;

			SourceParent = Parents[SourceParent];
		}

		RecursiveAddAllChildren(Children, SourceParent, BonesOut);
	}

}

void FGeometryCollectionClusteringUtility::RecursiveAddAllChildren(const TManagedArray<TSet<int32>>& Children, int32 SourceBone, TArray<int32>& BonesOut)
{
	BonesOut.AddUnique(SourceBone);
	for (int32 Child : Children[SourceBone])
	{
		RecursiveAddAllChildren(Children, Child, BonesOut);
	}

}

int32 FGeometryCollectionClusteringUtility::GetParentOfBoneAtSpecifiedLevel(const FGeometryCollection* GeometryCollection, int32 SourceBone, int32 Level)
{
	check(GeometryCollection);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

	if (SourceBone >= 0)
	{
		int32 SourceParent = SourceBone;
		while (Levels[SourceParent] > Level)
		{
			if (Parents[SourceParent] == -1)
				break;

			SourceParent = Parents[SourceParent];
		}

		return SourceParent;
	}

	return FGeometryCollection::Invalid;
}

void FGeometryCollectionClusteringUtility::RecursivelyUpdateChildBoneNames(int32 BoneIndex, const TManagedArray<TSet<int32>>& Children, TManagedArray<FString>& BoneNames, bool OverrideBoneNames /*= false*/)
{
	check(BoneIndex < Children.Num());

	if (Children[BoneIndex].Num() > 0)
	{
		const FString& ParentName = BoneNames[BoneIndex];
		int DisplayIndex = 1;
		for (int32 ChildIndex : Children[BoneIndex])
		{
			FString NewName;
			int32 FoundIndex = 0;
			FString ChunkNumberStr( FString::FromInt(DisplayIndex++) );

			// enable this if we don't want to override the child names with parent names
			bool HasExistingName = BoneNames[ChildIndex].FindChar('_', FoundIndex);

			if (!OverrideBoneNames && HasExistingName && FoundIndex > 0)
			{
				FString CurrentName = BoneNames[ChildIndex].Left(FoundIndex);

				int32 FoundNumberIndex = 0;
				bool ParentHasNumbers = ParentName.FindChar('_', FoundNumberIndex);
				if (ParentHasNumbers && FoundNumberIndex > 0)
				{
					FString ParentNumbers = ParentName.Right(ParentName.Len() - FoundNumberIndex);
					NewName = CurrentName + ParentNumbers + ChunkNumberStr;
				}
				else
				{
					NewName = CurrentName + ChunkNumberStr;
				}
			}
			else
			{
				NewName = ParentName + ChunkNumberStr;
			}
			BoneNames[ChildIndex] = NewName;
			RecursivelyUpdateChildBoneNames(ChildIndex, Children, BoneNames, OverrideBoneNames);
		}
	}
}

void FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(FGeometryCollection* GeometryCollection, int32 ParentElement)
{
	if (!GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
	{
		GeometryCollection->AddAttribute<int32>("Level", FGeometryCollection::TransformGroup, FManagedArrayCollection::FConstructionParameters(FName(),  false));
	}
	TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	check(ParentElement < Levels.Num());
	check(ParentElement < Children.Num());

	if (ParentElement != INDEX_NONE)
	{
		RecursivelyUpdateHierarchyLevelOfChildren(Levels, Children, ParentElement);
	}
	else
	{
		TArray<int32> RootBonesOut;
		GetRootBones(GeometryCollection, RootBonesOut);
		for (int32 RootBone : RootBonesOut)
		{
			RecursivelyUpdateHierarchyLevelOfChildren(Levels, Children, RootBone);
		}
	}
}

void FGeometryCollectionClusteringUtility::RecursivelyUpdateHierarchyLevelOfChildren(TManagedArray<int32>& Levels, const TManagedArray<TSet<int32>>& Children, int32 ParentElement)
{
	check(ParentElement < Levels.Num());
	check(ParentElement < Children.Num());

	for (int32 Element : Children[ParentElement])
	{
		Levels[Element] = Levels[ParentElement] + 1;
		RecursivelyUpdateHierarchyLevelOfChildren(Levels, Children, Element);
	}
}

void FGeometryCollectionClusteringUtility::CollapseLevelHierarchy(int8 Level, FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);

	const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);

	TArray<int32> Elements;

	if (Level == -1) // AllLevels
	{

		for (int Element = 0; Element < GeometryCollection->NumElements(FGeometryCollection::TransformAttribute); Element++)
		{
			if (GeometryCollection->IsGeometry(Element))
			{
				Elements.Add(Element);
			}
		}

		if (Elements.Num() > 0)
		{
			ClusterBonesUnderExistingRoot(GeometryCollection, Elements);
		}
	}
	else
	{
		for (int Element = 0; Element < GeometryCollection->NumElements(FGeometryCollection::TransformAttribute); Element++)
		{
			// if matches selected level then re-parent this node to the root
			if (Levels[Element] == Level)
			{
				Elements.Add(Element);
			}
		}
		if (Elements.Num() > 0)
		{
			CollapseHierarchyOneLevel(GeometryCollection, Elements);
		}
	}

}

void FGeometryCollectionClusteringUtility::CollapseSelectedHierarchy(int8 Level, const TArray<int32>& SelectedBones, FGeometryCollection* GeometryCollection)
{
	check(GeometryCollection);
	TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	// can't collapse root node away and doesn't make sense to operate when AllLevels selected
	if (Level > 0)
	{
		TArray<int32> Elements;
		for (int32 Element = 0; Element < SelectedBones.Num(); Element++)
		{
			int32 Index = SelectedBones[Element];

			// if matches selected level then re-parent this node to the root if it's not a leaf node
			if (Levels[Index] == Level && Children[Index].Num() > 0)
			{
				Elements.Add(SelectedBones[Element]);
			}
		}

		if (Elements.Num() > 0)
		{
			CollapseHierarchyOneLevel(GeometryCollection, Elements);
		}
	}

}

void FGeometryCollectionClusteringUtility::ValidateResults(FGeometryCollection* GeometryCollection)
{
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;

	// there should only ever be one root node
	int NumRootNodes = 0;
	for (int i = 0; i < Parents.Num(); i++)
	{
		if (Parents[i] == FGeometryCollection::Invalid)
		{
			NumRootNodes++;
		}
	}
	check(NumRootNodes == 1);

	ensure(GeometryCollection->HasContiguousFaces());
	ensure(GeometryCollection->HasContiguousVertices());
}

void FGeometryCollectionClusteringUtility::ContextBasedClusterSelection(
	FGeometryCollection* GeometryCollection,
	int ViewLevel,
	const TArray<int32>& SelectedComponentBonesIn,
	TArray<int32>& SelectedComponentBonesOut,
	TArray<int32>& HighlightedComponentBonesOut)
{
	HighlightedComponentBonesOut.Empty();
	SelectedComponentBonesOut.Empty();

	for (int32 BoneIndex : SelectedComponentBonesIn)
	{
		TArray <int32> SelectionHighlightedBones;
		if (ViewLevel == -1)
		{
			SelectionHighlightedBones.AddUnique(BoneIndex);
			SelectedComponentBonesOut.AddUnique(BoneIndex);
		}
		else
		{
			// select all children under bone as selected hierarchy level
			int32 ParentBoneIndex = GetParentOfBoneAtSpecifiedLevel(GeometryCollection, BoneIndex, ViewLevel);
			if (ParentBoneIndex != FGeometryCollection::Invalid)
			{
				SelectedComponentBonesOut.AddUnique(ParentBoneIndex);
			}
			else
			{
				SelectedComponentBonesOut.AddUnique(BoneIndex);
			}

			for (int32 Bone : SelectedComponentBonesOut)
			{
				GetChildBonesFromLevel(GeometryCollection, Bone, ViewLevel, SelectionHighlightedBones);
			}
		}

		HighlightedComponentBonesOut.Append(SelectionHighlightedBones);
	}

}

void FGeometryCollectionClusteringUtility::GetLeafBones(FGeometryCollection* GeometryCollection, int BoneIndex, TArray<int32>& LeafBonesOut)
{
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	if (Children[BoneIndex].Num() > 0)
	{
		for (int32 ChildElement : Children[BoneIndex])
		{
			GetLeafBones(GeometryCollection, ChildElement, LeafBonesOut);
		}
	}
	else
	{
		LeafBonesOut.Push(BoneIndex);
	}

}

void FGeometryCollectionClusteringUtility::MoveUpOneHierarchyLevel(FGeometryCollection* GeometryCollection, const TArray<int32>& SelectedBones)
{
	check(GeometryCollection);

	TManagedArray<int32>& Parents = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;

	for (int32 BoneIndex : SelectedBones)
	{
		int32 Parent = Parents[BoneIndex];
		if (Parents[BoneIndex] != FGeometryCollection::Invalid)
		{
			int32 ParentsParent = Parents[Parent];
			if (ParentsParent != FGeometryCollection::Invalid)
			{
				TArray<int32> InBones;
				InBones.Push(BoneIndex);
				GeometryCollectionAlgo::ParentTransforms(GeometryCollection, ParentsParent, InBones);
				UpdateHierarchyLevelOfChildren(GeometryCollection, ParentsParent);
				RecursivelyUpdateChildBoneNames(ParentsParent, Children, BoneNames);
			}
		}
	}
	ValidateResults(GeometryCollection);
}


