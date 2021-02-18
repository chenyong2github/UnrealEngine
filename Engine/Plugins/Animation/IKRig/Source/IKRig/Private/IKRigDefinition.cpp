// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinition.h"
#include "IKRigSolver.h"


void UIKRigDefinition::GetGoalNamesFromSolvers(TArray<FName>& OutGoalNames) const
{
	for (UIKRigSolver* Solver : Solvers)
	{
		if (!Solver)
		{
			continue;
		}
		
		TSet<FName> GoalNames;
		Solver->CollectGoalNames(GoalNames);

		// not using a TSet here because user code relies on indices
		for (FName Name : GoalNames)
		{
			if (!OutGoalNames.Contains(Name))
			{
				OutGoalNames.Add(Name);
			}
		}
	}
}


#if WITH_EDITOR

// add a bone. Return false if it already has conflict name or parent is not found
bool UIKRigDefinition::AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform)
{
	// only add when it doesn't exist
	if (Hierarchy.FindIndexFromBoneArray(InName) == INDEX_NONE)
	{
		// if no parent or we want to have parent BEFORE adding this joint
		if (InParent == NAME_None || Hierarchy.FindIndexFromBoneArray(InParent) != INDEX_NONE)
		{
			Hierarchy.Bones.Add(FIKRigBone(InName, InParent));
			RefPoseTransforms.Add(InGlobalTransform);

			check (Hierarchy.Bones.Num() == RefPoseTransforms.Num());

			Hierarchy.RebuildCacheData();
			return true;
		}
	}

	return false;
}

// ensure it's sorted from parent to children
void UIKRigDefinition::EnsureSortedCorrectly(bool bReSortIfNeeded)
{
	// we want to ensure it's sorted from parent to child
	// we do this twice, we use this data to sort, so after sort, we still have to do this again
	Hierarchy.RebuildCacheData();

	// @TODO: FIX THE CODE, it's confusing
	bool bNeedReSort = false;
	const TArray<FIKRigBone>& Bones = Hierarchy.Bones;
	const TArray<int32>& ParentIndices = Hierarchy.ParentIndices;
	// first check to see if we have violation
	for (int32 Index = 0; Index < ParentIndices.Num(); ++Index)
	{
		// if my parent is ahead, I need to resort
		if (ParentIndices[Index] >= Index)
		{
			bNeedReSort = true;
		}
	}

	// resort if needed is true, then we're good
	// but if resort is false, we want to ensure it's sorted already, and not have to resort
	ensureAlways(bReSortIfNeeded || !bNeedReSort);

	if (bNeedReSort)
	{
		// if so we create a tree and recreate Bones array
		struct FBoneTreeNode
		{
			FIKRigBone Bone;
			FBoneTreeNode* Children;
			FBoneTreeNode* Sibling;

			FBoneTreeNode()
				: Children(nullptr)
				, Sibling(nullptr)
			{}

			FBoneTreeNode(FIKRigBone InBone)
				: Bone(InBone)
				, Children(nullptr)
				, Sibling(nullptr)
			{}

			void AddToBones(TArray<FIKRigBone>& OutBones)
			{
				OutBones.Add(Bone);

				FBoneTreeNode* Iter = Children;
				while (Iter)
				{
					Iter->AddToBones(OutBones);
					Iter = Iter->Sibling;
				}
			}

			void AddChild(FBoneTreeNode* InChild)
			{
				if (Children == nullptr)
				{
					Children = InChild;
				}
				else
				{
					FBoneTreeNode* Iter = Children;
					do
					{
						if (Iter->Sibling == nullptr)
						{
							Iter->Sibling = InChild;
							break;
						}

						Iter = Iter->Sibling;
					} while (true);
				}
			}
		};

		// the assumption is that first one is the root
		TArray<FBoneTreeNode> Tree;
		Tree.AddDefaulted(Bones.Num());

		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			Tree[Index].Bone = Bones[Index];
			if (ParentIndices[Index] != INDEX_NONE)
			{
				Tree[ParentIndices[Index]].AddChild(&Tree[Index]);
			}
		}

		TArray<FIKRigBone> NewBones;
		// BFS for tree
		Tree[0].AddToBones(NewBones);

		// create new transform 
		check(NewBones.Num() == Bones.Num());

		// we need to move transform of old Bones to new Bones, and their indices have changed
		// move transform, very slow move
		TArray<FTransform> NewTransforms;
		NewTransforms.AddUninitialized(Bones.Num());

		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const FName& Name = Bones[Index].Name;
			for (int32 Jndex = 0; Jndex < NewBones.Num(); ++Jndex)
			{
				if (Name == NewBones[Jndex].Name)
				{
					NewTransforms[Jndex] = RefPoseTransforms[Index];
				}
			}
		}

		// override back to it
		Hierarchy.Bones = NewBones;
		RefPoseTransforms = NewTransforms;
		Hierarchy.RebuildCacheData();
	}
}

void UIKRigDefinition::ResetHierarchy()
{
	Hierarchy.Bones.Reset();
	Hierarchy.ParentIndices.Reset();
	Hierarchy.RuntimeNameLookupTable.Reset();
	RefPoseTransforms.Reset();
}

#endif // WITH_EDITOR

void UIKRigDefinition::PostLoad()
{
	Super::PostLoad();

	Hierarchy.RebuildCacheData();
}