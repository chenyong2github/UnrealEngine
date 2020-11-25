// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IKRigDefinition.cpp: Composite classes that contains sequence for each section
=============================================================================*/

#include "IKRigDefinition.h"
#include "IKRigSolverDefinition.h"

UIKRigDefinition::UIKRigDefinition()
{
	// we create constraint definition for them
	//ConstraintDefinitions = NewObject<UIKRigConstraintDefinition>(this, TEXT("ConstraintDefinition"));
}

#if WITH_EDITOR
void UIKRigDefinition::UpdateGoal()
{
	// collect goals from the solver definitions
	// each solver definition collects their tasks to goals
	// but the IKRigDefinition will maintain the default value and so on
	Sanitize();

	TArray<FName> ListOfGoals;
	for (UIKRigSolverDefinition* SolverDef : SolverDefinitions)
	{
		if (SolverDef)
		{
			SolverDef->CollectGoals(ListOfGoals);
		}
	}

	// we want to update goal list properly

	// first if it's not used any more, remove
	TArray<FName> GoalsToRemove;

	// first we go through IKGoals and see if we should remove unused ones
	for (auto Iter = IKGoals.CreateConstIterator(); Iter; ++Iter)
	{
		// if it's not used any more
		if (!ListOfGoals.Contains(Iter->Key))
		{
			GoalsToRemove.Add(Iter->Key);
		}
	}

	for (const FName& Goal : GoalsToRemove)
	{
		IKGoals.Remove(Goal);
	}

	// now add new ones if it doesn't exist
	for (int32 Index=0; Index<ListOfGoals.Num(); ++Index)
	{
		const FName& Goal = ListOfGoals[Index];
		if (!IKGoals.Contains(Goal))
		{
			// if we don't have it, add to new one
			FIKRigGoal NewGoal(Goal);
			IKGoals.Add(Goal, NewGoal);
		}
	}

	// this doesn't keep the old list alive
	// unsure if that's what we want yet. Sometimes you may lose
	// your used variable, but that's something we can find it out 
	// from UX
}

void UIKRigDefinition::Sanitize()
{
	// sanitize to clean up
	SolverDefinitions.RemoveAll([this](const UIKRigSolverDefinition* IKDef) { return IKDef == nullptr; });
}

/////////////////////////////////////////////////////////
//// Hierarchy modifiers
// why this is here? 
// delegate and user instancing 
// can this be in controller?
// 
/////////////////////////////////////////////////////////

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
			ReferencePose.GlobalTransforms.Add(InGlobalTransform);

			check (Hierarchy.Bones.Num() == ReferencePose.GetNum());

			Hierarchy.RebuildCacheData();
			BoneAddedDelegate.Broadcast(InName);
			return true;
		}
	}

	return false;
}

// remove a bone. Returns false if it is not found
// if it has a children, it will remove all the children with it
bool UIKRigDefinition::RemoveBone(const FName& InName)
{
	struct FRemoveChild
	{
		static void RemoveChild_Recursively(const FIKRigHierarchy& InHierarchy, TArray<FIKRigBone>& InOutBones, TArray<FTransform>& InOutTransform, const FName& InName)
		{
			// ensure I have this bone
			int32 Index = InHierarchy.FindIndexFromBoneArray(InName);
			if (Index != INDEX_NONE)
			{
				// find children
				TArray<int32> ChildIndices = InHierarchy.FindIndicesByParentName(InName);
				// iterate back from the forward
				for (int32 ChildIndex = ChildIndices.Num() - 1; ChildIndex >= 0; --ChildIndex)
				{
					// remove children's first, since children is always behind of parent, this should work
					RemoveChild_Recursively(InHierarchy, InOutBones, InOutTransform, InOutBones[ChildIndices[ChildIndex]].Name);
					// do not shrink yet and remove all children first
					// since children is behind of parent, this works
				}

				// now remove parent (this)
				InOutBones.RemoveAt(Index, 1, false);
				InOutTransform.RemoveAt(Index, 1, false);
			}
		}
	};

	// ensure I have this bone
	int32 Index = Hierarchy.FindIndexFromBoneArray(InName);
	if (Index != INDEX_NONE)
	{
		// remove all children
		FRemoveChild::RemoveChild_Recursively(Hierarchy, Hierarchy.Bones, ReferencePose.GlobalTransforms, InName);

		// shrink now
		Hierarchy.Bones.Shrink();
		ReferencePose.GlobalTransforms.Shrink();
		Hierarchy.RebuildCacheData();
		BoneRemovedDelegate.Broadcast(InName);
		return true;
	}

	return false;
}

// rename just change the name
// return false if not found
bool UIKRigDefinition::RenameBone(const FName& InOldName, const FName& InNewName)
{
	// ensure we have old name and do not have new name
	int32 Index = Hierarchy.FindIndexFromBoneArray(InOldName);
	if (Index != INDEX_NONE)
	{
		// make sure nobody is using NewName
		if (Hierarchy.FindIndexFromBoneArray(InNewName) == INDEX_NONE)
		{
			TArray<int32> ChildIndices = Hierarchy.FindIndicesByParentName(InOldName);

			for (int32 ChildIndex = 0; ChildIndex < ChildIndices.Num(); ++ChildIndex)
			{
				Hierarchy.Bones[ChildIndices[ChildIndex]].ParentName = InNewName;
			}

			// now change me
			Hierarchy.Bones[Index].Name = InNewName;
			Hierarchy.RebuildCacheData();
			BoneRenamedDelegate.Broadcast(InOldName, InNewName);
			return true;
		}
	}

	return false;
}

// reparent to InNewParent
//	if not found - either InName or InParent
//	Or if InNewParent is invalid (i.e. it's a child of InName)
bool UIKRigDefinition::ReparentBone(const FName& InName, const FName& InNewParent)
{
	int32 Index = Hierarchy.FindIndexFromBoneArray(InName);
	if (Index != INDEX_NONE)
	{
		int32 NewParentIndex = INDEX_NONE;
		if (InNewParent != NAME_None)
		{
			NewParentIndex = Hierarchy.FindIndexFromBoneArray(InNewParent);
			if (NewParentIndex == INDEX_NONE)
			{
				return false;
			}

			// we have to ensure we don't have cycling situation
			int32 CurIndex = NewParentIndex;
			while (CurIndex != INDEX_NONE)
			{
				// i hit myself
				if (CurIndex == Index)
				{
					// we're cycling
					return false;
				}

				// find parent
				CurIndex = Hierarchy.FindIndexFromBoneArray(Hierarchy.Bones[CurIndex].ParentName);
			}
		}

		// we don't have a cycling, now allow set it
		Hierarchy.Bones[Index].ParentName = InNewParent;
		EnsureSortedCorrectly(true);
		BoneReparentedDelegate.Broadcast(InName, InNewParent);
		return true;
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
					} while (Iter);
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
					NewTransforms[Jndex] = ReferencePose.GlobalTransforms[Index];
				}
			}
		}

		// override back to it
		Hierarchy.Bones = NewBones;
		ReferencePose.GlobalTransforms = NewTransforms;
		Hierarchy.RebuildCacheData();
	}
}

void UIKRigDefinition::ResetHierarchy()
{
	Hierarchy.Bones.Reset();
	Hierarchy.ParentIndices.Reset();
	Hierarchy.RuntimeNameLookupTable.Reset();
	ReferencePose.GlobalTransforms.Reset();
}

void UIKRigDefinition::EnsureCreateUniqueGoalName(FName& InOutGoal) const
{
	int32 Index = 1;

	FString GoalNameString = InOutGoal.ToString();

	// while contains
	while(IKGoals.Find(FName(*GoalNameString)))
	{
		GoalNameString += FString::Format(TEXT("_{0}"), {Index++});
	}

	InOutGoal = FName(*GoalNameString);
}

#endif // WITH_EDITOR

void UIKRigDefinition::PostLoad()
{
	Super::PostLoad();

	Hierarchy.RebuildCacheData();
}