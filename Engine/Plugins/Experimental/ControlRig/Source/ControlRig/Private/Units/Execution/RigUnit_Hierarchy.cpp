// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_Hierarchy.h"
#include "Units/Execution/RigUnit_Item.h"
#include "Units/RigUnitContext.h"

FRigUnit_HierarchyGetParent_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedChild.Reset();
		CachedParent.Reset();
	}

	if(CachedChild.IsIdentical(Child, Context.Hierarchy))
	{
		Parent = CachedParent.GetKey();
	}
	else
	{
		Parent.Reset();
		CachedParent.Reset();

		if(CachedChild.UpdateCache(Child, Context.Hierarchy))
		{
			Parent = Context.Hierarchy->GetFirstParent(Child);
			if(Parent.IsValid())
			{
				CachedParent.UpdateCache(Parent, Context.Hierarchy);
			}
		}
	}
}

FRigUnit_HierarchyGetParents_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedChild.Reset();
		CachedParents.Reset();
	}

	if(!CachedChild.IsIdentical(Child, Context.Hierarchy))
	{
		CachedParents.Reset();

		if(CachedChild.UpdateCache(Child, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;
			FRigElementKey Parent = Child;
			do
			{
				if(bIncludeChild || Parent != Child)
				{
					Keys.Add(Parent);
				}
				Parent = Context.Hierarchy->GetFirstParent(Parent);
			}
			while(Parent.IsValid());

			CachedParents = FRigElementKeyCollection(Keys);
			if(bReverse)
			{
				CachedParents = FRigElementKeyCollection::MakeReversed(CachedParents);
			}
		}
	}

	Parents = CachedParents;
}

FRigUnit_HierarchyGetChildren_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedParent.Reset();
		CachedChildren.Reset();
	}

	if(!CachedParent.IsIdentical(Parent, Context.Hierarchy))
	{
		CachedChildren.Reset();

		if(CachedParent.UpdateCache(Parent, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;

			if(bIncludeParent)
			{
				Keys.Add(Parent);
			}

			
			Keys.Append(Context.Hierarchy->GetChildren(Parent, bRecursive));

			CachedChildren = FRigElementKeyCollection(Keys);
		}
	}

	Children = CachedChildren;
}

FRigUnit_HierarchyGetSiblings_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	if (Context.State == EControlRigState::Init)
	{
		CachedItem.Reset();
		CachedSiblings.Reset();
	}

	if(!CachedItem.IsIdentical(Item, Context.Hierarchy))
	{
		CachedSiblings.Reset();

		if(CachedItem.UpdateCache(Item, Context.Hierarchy))
		{
			TArray<FRigElementKey> Keys;

			FRigElementKey Parent = Context.Hierarchy->GetFirstParent(Item);
			if(Parent.IsValid())
			{
				TArray<FRigElementKey> Children = Context.Hierarchy->GetChildren(Parent, false);
				for(FRigElementKey Child : Children)
				{
					if(bIncludeItem || Child != Item)
					{
						Keys.Add(Child);
					}
				}
			}

			if(Keys.Num() == 0 && bIncludeItem)
			{
				Keys.Add(Item);
			}

			CachedSiblings = FRigElementKeyCollection(Keys);
		}
	}

	Siblings = CachedSiblings;
}

FRigUnit_HierarchyGetPose_Execute()
{
	Pose = Context.Hierarchy->GetPose(Initial, ElementType, ItemsToGet);
}

FRigUnit_HierarchySetPose_Execute()
{
	ExecuteContext.Hierarchy->SetPose(
		Pose,
		Space == EBoneGetterSetterMode::GlobalSpace ? ERigTransformType::CurrentGlobal : ERigTransformType::CurrentLocal,
		ElementType,
		ItemsToSet,
		Weight
	);
}

FRigUnit_PoseIsEmpty_Execute()
{
	IsEmpty = Pose.Num() == 0;
}

FRigUnit_PoseGetItems_Execute()
{
	Items.Reset();

	for(const FRigPoseElement& PoseElement : Pose)
	{
		// filter by type
		if (((uint8)ElementType & (uint8)PoseElement.Index.GetKey().Type) == 0)
		{
			continue;
		}
		Items.Add(PoseElement.Index.GetKey());
	}
}

FRigUnit_PoseGetDelta_Execute()
{
	PosesAreEqual = true;
	ItemsWithDelta.Reset();

	if(PoseA.Num() == 0 && PoseB.Num() == 0)
	{
		PosesAreEqual = true;
		return;
	}

	if(PoseA.Num() == 0 && PoseB.Num() != 0)
	{
		PosesAreEqual = false;
		FRigUnit_PoseGetItems::StaticExecute(RigVMExecuteContext, PoseB, ElementType, ItemsWithDelta, Context);
		return;
	}

	if(PoseA.Num() != 0 && PoseB.Num() == 0)
	{
		PosesAreEqual = false;
		FRigUnit_PoseGetItems::StaticExecute(RigVMExecuteContext, PoseA, ElementType, ItemsWithDelta, Context);
		return;
	}

	const float PositionU = FMath::Abs(PositionThreshold);
	const float RotationU = FMath::Abs(RotationThreshold);
	const float ScaleU = FMath::Abs(ScaleThreshold);
	const float CurveU = FMath::Abs(CurveThreshold);

	auto ArePoseElementsEqual = [
		PositionU,
		RotationU,
		ScaleU,
		CurveU,
		Space
		] (
			const FRigPoseElement& A,
			const FRigPoseElement& B
		) -> bool
	{
		const FRigElementKey& KeyA = A.Index.GetKey();
		const FRigElementKey& KeyB = B.Index.GetKey();
		check(KeyA == KeyB);
		
		if(KeyA.Type == ERigElementType::Curve)
		{
			if(CurveU > SMALL_NUMBER)
			{
				return FMath::Abs(A.CurveValue - B.CurveValue) < CurveU;
			}
			return true;
		}
		
		const FTransform& TransformA = (Space == EBoneGetterSetterMode::GlobalSpace) ? A.GlobalTransform : A.LocalTransform;
		const FTransform& TransformB = (Space == EBoneGetterSetterMode::GlobalSpace) ? B.GlobalTransform : B.LocalTransform;
		
		if(PositionU > SMALL_NUMBER)
		{
			const FVector PositionA = TransformA.GetLocation();
			const FVector PositionB = TransformB.GetLocation();
			const FVector Delta = (PositionA - PositionB).GetAbs();
			if( (Delta.X >= PositionU) ||
				(Delta.Y >= PositionU) ||
				(Delta.Z >= PositionU))
			{
				return false;
			}
		}

		if(RotationU > SMALL_NUMBER)
		{
			const FVector RotationA = TransformA.GetRotation().Rotator().Euler();
			const FVector RotationB = TransformB.GetRotation().Rotator().Euler();
			const FVector Delta = (RotationA - RotationB).GetAbs();
			if( (Delta.X >= RotationU) ||
				(Delta.Y >= RotationU) ||
				(Delta.Z >= RotationU))
			{
				return false;
			}
		}

		if(ScaleU > SMALL_NUMBER)
		{
			const FVector ScaleA = TransformA.GetScale3D();
			const FVector ScaleB = TransformB.GetScale3D();
			const FVector Delta = (ScaleA - ScaleB).GetAbs();
			if( (Delta.X >= ScaleU) ||
				(Delta.Y >= ScaleU) ||
				(Delta.Z >= ScaleU))
			{
				return false;
			}
		}

		return true;
	};

	// if we should compare a sub set of things
	if(!ItemsToCompare.IsEmpty())
	{
		for(int32 Index = 0; Index < ItemsToCompare.Num(); Index++)
		{
			const FRigElementKey& Key = ItemsToCompare[Index];
			if (((uint8)ElementType & (uint8)Key.Type) == 0)
			{
				continue;
			}

			const int32 IndexA = PoseA.GetIndex(Key);
			if(IndexA == INDEX_NONE)
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(Key);
				continue;
			}
			
			const int32 IndexB = PoseB.GetIndex(Key);
			if(IndexB == INDEX_NONE)
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(Key);
				continue;
			}

			const FRigPoseElement& A = PoseA[IndexA];
			const FRigPoseElement& B = PoseB[IndexB];

			if(!ArePoseElementsEqual(A, B))
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(Key);
			}
		}		
	}
	
	// if the poses have the same hash we can accelerate this
	// since they are structurally the same
	else if(PoseA.PoseHash == PoseB.PoseHash)
	{
		for(int32 Index = 0; Index < PoseA.Num(); Index++)
		{
			const FRigPoseElement& A = PoseA[Index];
			const FRigPoseElement& B = PoseB[Index];
			const FRigElementKey& KeyA = A.Index.GetKey(); 
			const FRigElementKey& KeyB = B.Index.GetKey(); 
			check(KeyA == KeyB);
			
			if (((uint8)ElementType & (uint8)KeyA.Type) == 0)
			{
				continue;
			}

			if(!ArePoseElementsEqual(A, B))
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(KeyA);
			}
		}
	}
	
	// if the poses have different hashes they might not contain the same elements
	else
	{
		for(int32 IndexA = 0; IndexA < PoseA.Num(); IndexA++)
		{
			const FRigPoseElement& A = PoseA[IndexA];
			const FRigElementKey& KeyA = A.Index.GetKey(); 
			
			if (((uint8)ElementType & (uint8)KeyA.Type) == 0)
			{
				continue;
			}

			const int32 IndexB = PoseB.GetIndex(KeyA);
			if(IndexB == INDEX_NONE)
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(KeyA);
				continue;
			}

			const FRigPoseElement& B = PoseB[IndexB];
			
			if(!ArePoseElementsEqual(A, B))
			{
				PosesAreEqual = false;
				ItemsWithDelta.Add(KeyA);
			}
		}

		// let's loop the other way as well and find relevant
		// elements which are not part of this
		for(int32 IndexB = 0; IndexB < PoseB.Num(); IndexB++)
		{
			const FRigPoseElement& B = PoseB[IndexB];
			const FRigElementKey& KeyB = B.Index.GetKey(); 
			
			if (((uint8)ElementType & (uint8)KeyB.Type) == 0)
			{
				continue;
			}

			const int32 IndexA = PoseA.GetIndex(KeyB);
			if(IndexA == INDEX_NONE)
			{
				PosesAreEqual = false;
				ItemsWithDelta.AddUnique(KeyB);
			}
		}
	}
}

FRigUnit_PoseGetTransform_Execute()
{
	if (Context.State == EControlRigState::Init)
	{
		CachedPoseElementIndex = CachedPoseHash = INDEX_NONE;
	}

	// set up defaults
	Valid = false;
	Transform = FTransform::Identity;

	if(CachedPoseHash != Pose.HierarchyTopologyVersion)
	{
		CachedPoseHash = Pose.PoseHash;
		CachedPoseElementIndex = Pose.GetIndex(Item);
	}

	if(CachedPoseElementIndex == INDEX_NONE)
	{
		return;
	}

	Valid = true;

	const FRigPoseElement& PoseElement = Pose[CachedPoseElementIndex];

	if(Space == EBoneGetterSetterMode::GlobalSpace)
	{
		Transform = PoseElement.GlobalTransform;
	}
	else
	{
		Transform = PoseElement.LocalTransform;
	}
}

FRigUnit_PoseGetCurve_Execute()
{
	if (Context.State == EControlRigState::Init)
	{
		CachedPoseElementIndex = CachedPoseHash = INDEX_NONE;
	}

	// set up defaults
	Valid = false;
	CurveValue = 0.f;

	if(CachedPoseHash != Pose.HierarchyTopologyVersion)
	{
		CachedPoseHash = Pose.PoseHash;
		CachedPoseElementIndex = Pose.GetIndex(FRigElementKey(Curve, ERigElementType::Curve));
	}

	if(CachedPoseElementIndex == INDEX_NONE)
	{
		return;
	}

	Valid = true;

	const FRigPoseElement& PoseElement = Pose[CachedPoseElementIndex];
	CurveValue = PoseElement.CurveValue;
}

FRigUnit_PoseLoop_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Count = Pose.Num();
	Continue = Pose.IsValidIndex(Index);
	Ratio = GetRatioFromIndex(Index, Count);

	if(Continue)
	{
		const FRigPoseElement& PoseElement = Pose.Elements[Index];
		Item = PoseElement.Index.GetKey();
		GlobalTransform = PoseElement.GlobalTransform;
		LocalTransform = PoseElement.LocalTransform;
		CurveValue = PoseElement.CurveValue;
	}
	else
	{
		Item = FRigElementKey();
		GlobalTransform = LocalTransform = FTransform::Identity;
		CurveValue = 0.f;
	}
}