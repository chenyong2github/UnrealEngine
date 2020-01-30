// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_FABRIK.h"
#include "Units/RigUnitContext.h"

FRigUnit_FABRIK_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	TArray<FFABRIKChainLink>& Chain = WorkData.Chain;
	TArray<int32>& BoneIndices = WorkData.BoneIndices;
	int32& EffectorIndex = WorkData.EffectorIndex;

	if (Context.State == EControlRigState::Init)
	{
		BoneIndices.Reset();

		// verify the chain
		const int32 RootIndex = Hierarchy->GetIndex(StartBone);
		if (RootIndex != INDEX_NONE)
		{
			int32 CurrentIndex = EffectorIndex = Hierarchy->GetIndex(EffectorBone);
			while (CurrentIndex != INDEX_NONE)
			{
				// ensure the chain
				int32 ParentIndex = (*Hierarchy)[CurrentIndex].ParentIndex;
				if (ParentIndex != INDEX_NONE)
				{
					BoneIndices.Add(CurrentIndex);
				}

				if (ParentIndex == RootIndex)
				{
					BoneIndices.Add(RootIndex);
					break;
				}

				CurrentIndex = ParentIndex;
			}

			Chain.Reserve(BoneIndices.Num());
		}
	}
	else  if (Context.State == EControlRigState::Update)
	{
		if (BoneIndices.Num() > 0)
		{
			// Gather chain links. These are non zero length bones.
			Chain.Reset();
			
			TArray<FTransform> Transforms;
			Transforms.AddDefaulted(BoneIndices.Num());

			float MaximumReach = 0.f;
			int32 const NumChainLinks = BoneIndices.Num();
			const int32 RootIndex = BoneIndices.Last();
			Chain.Add(FFABRIKChainLink(Hierarchy->GetGlobalTransform(RootIndex).GetLocation(), 0.f, RootIndex, 0));
			Transforms[0] = Hierarchy->GetGlobalTransform(RootIndex);

			// start from child to up
			for (int32 ChainIndex = BoneIndices.Num() - 2; ChainIndex >= 0 ; --ChainIndex)
			{
				const FTransform& BoneTransform = Hierarchy->GetGlobalTransform(BoneIndices[ChainIndex]);
				const FTransform& ParentTransform = Hierarchy->GetGlobalTransform(BoneIndices[ChainIndex + 1]);

				// Calculate the combined length of this segment of skeleton
				float const BoneLength = FVector::Dist(BoneTransform.GetLocation(), ParentTransform.GetLocation());

				const int32 TransformIndex = Chain.Num();
				Chain.Add(FFABRIKChainLink(BoneTransform.GetLocation(), BoneLength, BoneIndices[ChainIndex], TransformIndex));
				MaximumReach += BoneLength;

				Transforms[TransformIndex] = BoneTransform;
			}


			bool bBoneLocationUpdated = AnimationCore::SolveFabrik(Chain, EffectorTransform.GetLocation(), MaximumReach, Precision, MaxIterations);
			// If we moved some bones, update bone transforms.
			if (bBoneLocationUpdated)
			{
				// FABRIK algorithm - re-orientation of bone local axes after translation calculation
				for (int32 LinkIndex = 0; LinkIndex < NumChainLinks - 1; LinkIndex++)
				{
					const FFABRIKChainLink& CurrentLink = Chain[LinkIndex];
					const FFABRIKChainLink& ChildLink = Chain[LinkIndex + 1];

					// Calculate pre-translation vector between this bone and child
					FVector const OldDir = (Hierarchy->GetGlobalTransform(ChildLink.BoneIndex).GetLocation() - Hierarchy->GetGlobalTransform(CurrentLink.BoneIndex).GetLocation()).GetUnsafeNormal();

					// Get vector from the post-translation bone to it's child
					FVector const NewDir = (ChildLink.Position - CurrentLink.Position).GetUnsafeNormal();

					// Calculate axis of rotation from pre-translation vector to post-translation vector
					FVector const RotationAxis = FVector::CrossProduct(OldDir, NewDir).GetSafeNormal();
					float const RotationAngle = FMath::Acos(FVector::DotProduct(OldDir, NewDir));
					FQuat const DeltaRotation = FQuat(RotationAxis, RotationAngle);
					// We're going to multiply it, in order to not have to re-normalize the final quaternion, it has to be a unit quaternion.
					checkSlow(DeltaRotation.IsNormalized());

					// Calculate absolute rotation and set it
					FTransform& CurrentBoneTransform = Transforms[CurrentLink.TransformIndex];
					CurrentBoneTransform.SetRotation(DeltaRotation * CurrentBoneTransform.GetRotation());
					CurrentBoneTransform.NormalizeRotation();
					CurrentBoneTransform.SetTranslation(CurrentLink.Position);
				}

				// fill up the last data transform
				const FFABRIKChainLink & CurrentLink = Chain[NumChainLinks - 1];
				FTransform& CurrentBoneTransform = Transforms[CurrentLink.TransformIndex];
				CurrentBoneTransform.SetTranslation(CurrentLink.Position);
				CurrentBoneTransform.SetRotation(Hierarchy->GetGlobalTransform(CurrentLink.BoneIndex).GetRotation());

				if (FMath::IsNearlyEqual(Weight, 1.f))
				{
					for (int32 LinkIndex = 0; LinkIndex < NumChainLinks; LinkIndex++)
					{
						FFABRIKChainLink const & LocalLink = Chain[LinkIndex];
						Hierarchy->SetGlobalTransform(LocalLink.BoneIndex, Transforms[LocalLink.TransformIndex], bPropagateToChildren);
					}

					Hierarchy->SetGlobalTransform(EffectorIndex, EffectorTransform, bPropagateToChildren);
				}
				else
				{
					float T = FMath::Clamp<float>(Weight, 0.f, 1.f);

					for (int32 LinkIndex = 0; LinkIndex < NumChainLinks; LinkIndex++)
					{
						FFABRIKChainLink const & LocalLink = Chain[LinkIndex];
						FTransform PreviousXfo = Hierarchy->GetGlobalTransform(LocalLink.BoneIndex);
						FTransform Xfo = FControlRigMathLibrary::LerpTransform(PreviousXfo, Transforms[LocalLink.TransformIndex], T);
						Hierarchy->SetGlobalTransform(LocalLink.BoneIndex, Xfo, bPropagateToChildren);
					}

					FTransform PreviousXfo = Hierarchy->GetGlobalTransform(EffectorIndex);
					FTransform Xfo = FControlRigMathLibrary::LerpTransform(PreviousXfo, EffectorTransform, T);
					Hierarchy->SetGlobalTransform(EffectorIndex, Xfo, bPropagateToChildren);
				}
			}
		}
	}
}
