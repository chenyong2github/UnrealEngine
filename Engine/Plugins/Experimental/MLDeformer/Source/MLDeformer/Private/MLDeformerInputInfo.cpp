// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerInputInfo.h"
#include "MLDeformerAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"
#include "ReferenceSkeleton.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"


void FMLDeformerInputInfo::Init(const FMLDeformerInputInfoInitSettings& Settings)
{
	// Reset things.
	BoneNameStrings.Empty();
	BoneNames.Empty();
	CurveNameStrings.Empty();
	CurveNames.Empty();
	NumBaseMeshVertices = 0;
	NumTargetMeshVertices = 0;

	// Make sure we have all required data.
	USkeletalMesh* SkeletalMesh = Settings.SkeletalMesh;
	UGeometryCache* GeomCache = Settings.TargetMesh;

#if WITH_EDITOR
	NumBaseMeshVertices = UMLDeformerAsset::ExtractNumImportedSkinnedVertices(SkeletalMesh);
	NumTargetMeshVertices = UMLDeformerAsset::ExtractNumImportedGeomCacheVertices(GeomCache);
#endif

	// Handle bones.
	if (Settings.bIncludeBones && SkeletalMesh)
	{
		// Include all the bones when no list was provided.
		const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
		if (Settings.BoneNamesToInclude.IsEmpty())
		{
			// Grab all bone names.
			const int32 NumBones = RefSkeleton.GetNum();
			BoneNameStrings.AddDefaulted(NumBones);
			for (int32 Index = 0; Index < NumBones; ++Index)
			{
				const FName BoneName = RefSkeleton.GetBoneName(Index);
				BoneNameStrings[Index] = BoneName.ToString();
			}
		}
		else // A list of bones to include was provided.
		{
			BoneNameStrings = Settings.BoneNamesToInclude;

			// Remove bones that don't exist.
			for (int32 Index = 0; Index < BoneNames.Num();)
			{
				const FName BoneName(BoneNameStrings[Index]);
				if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
				{
					UE_LOG(LogMLDeformer, Warning, TEXT("Bone '%s' in the bones include list doesn't exist, ignoring it."), *BoneNameStrings[Index]);
					BoneNameStrings.Remove(BoneNameStrings[Index]);
				}
				else
				{
					Index++;
				}
			}
		}
	}

	// Handle curves.
	if (Settings.bIncludeCurves && SkeletalMesh)
	{
		// Anim curves.
		USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
		const FSmartNameMapping* SmartNameMapping = Skeleton ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr;
		if (SmartNameMapping) // When there are curves.
		{
			// Include all curves when no list was provided.
			if (Settings.CurveNamesToInclude.IsEmpty())
			{
				SmartNameMapping->FillNameArray(CurveNames);
				CurveNameStrings.Reserve(CurveNames.Num());
				for (FName Name : CurveNames)
				{
					CurveNameStrings.Add(Name.ToString());
				}
			}
			else // A list of curve names was provided.
			{
				CurveNameStrings = Settings.CurveNamesToInclude;

				// Remove curves that don't exist.
				for (int32 Index = 0; Index < CurveNameStrings.Num();)
				{
					FName CurveName(CurveNameStrings[Index]);
					if (!SmartNameMapping->Exists(CurveName))
					{
						UE_LOG(LogMLDeformer, Warning, TEXT("Anim curve '%s' doesn't exist, ignoring it."), *CurveNameStrings[Index]);
						CurveNameStrings.RemoveAt(Index);
					}
					else
					{
						Index++;
					}
				}
			}
		}
	}

	// Update the FName arrays.
	UpdateFNames();
}

void FMLDeformerInputInfo::UpdateFNames()
{
	// Update the bone names.
	const int32 NumBones = BoneNameStrings.Num();
	BoneNames.Reset(NumBones);
	BoneNames.Reserve(NumBones);
	for (const FString& NameString : BoneNameStrings)
	{
		BoneNames.Add(FName(NameString));
	}

	// Update the curve names.
	const int32 NumCurves = CurveNameStrings.Num();
	CurveNames.Reset(NumCurves);
	CurveNames.Reserve(NumCurves);
	for (const FString& NameString : CurveNameStrings)
	{
		CurveNames.Add(FName(NameString));
	}
}

bool FMLDeformerInputInfo::IsCompatible(USkeletalMesh* SkeletalMesh) const
{
	if (SkeletalMesh == nullptr)
	{
		return false;
	}

	// Verify that all required bones are there.
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	for (const FName BoneName : BoneNames)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE) // We're missing the required bone. The network needs to input the transform for this bone.
		{
			return false;
		}
	}

	// Verify that all required curves are there.
	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	const FSmartNameMapping* SmartNameMapping = Skeleton ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr;
	if (SmartNameMapping)
	{
		for (const FName CurveName : CurveNames)
		{
			if (!SmartNameMapping->Exists(CurveName))
			{
				return false;
			}
		}
	}

	return true;
}

FString FMLDeformerInputInfo::GenerateCompatibilityErrorString(USkeletalMesh* SkeletalMesh) const
{
	if (SkeletalMesh == nullptr)
	{
		return FString();
	}

	// Verify that all required bones are there.
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	FString ErrorString;
	for (const FName BoneName : BoneNames)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE) // We're missing the required bone. The network needs to input the transform for this bone.
		{
			ErrorString += FString::Format(TEXT("Required bone '{0}' is missing.\n"), {*BoneName.ToString()});
		}
	}

	// Verify that all required curves are there.
	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	const FSmartNameMapping* SmartNameMapping = Skeleton ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr;
	if (SmartNameMapping)
	{
		for (const FName CurveName : CurveNames)
		{
			if (!SmartNameMapping->Exists(CurveName))
			{
				ErrorString += FString::Format(TEXT("Required curve '{0}' is missing.\n"), {*CurveName.ToString()});
			}
		}
	}

	// Check vertex count.
#if WITH_EDITORONLY_DATA
	if ((NumBaseMeshVertices > 0 && NumTargetMeshVertices > 0) &&
		NumBaseMeshVertices != SkeletalMesh->GetNumImportedVertices())
	{
		ErrorString += FString::Format(TEXT("The number of vertices that the network was trained on ({0} verts) doesn't match the skeletal mesh '{1}' ({2} verts)..\n"), 
			{
				NumBaseMeshVertices, 
				SkeletalMesh->GetName(),
				SkeletalMesh->GetNumImportedVertices(),
			} );
	}
#endif

	return ErrorString;
}

void FMLDeformerInputInfo::ExtractCurveValues(USkeletalMeshComponent* SkelMeshComponent, TArray<float>& OutValues) const
{
	check(CurveNames.Num() == CurveNameStrings.Num());

	UAnimInstance* AnimInstance = SkelMeshComponent->GetAnimInstance();
	const int32 NumCurves = CurveNames.Num();
	OutValues.Reset(NumCurves);
	OutValues.AddUninitialized(NumCurves);
	for (int32 Index = 0; Index < NumCurves; ++Index)
	{
		const FName CurveName = CurveNames[Index];
		OutValues[Index] = AnimInstance->GetCurveValue(CurveName);
	}
}

void FMLDeformerInputInfo::ExtractBoneRotations(USkeletalMeshComponent* SkelMeshComponent, TArray<float>& OutRotations) const
{
	const TArray<FTransform>& BoneTransforms = SkelMeshComponent->GetBoneSpaceTransforms();
	const int32 NumBones = GetNumBones();
	const int32 NumFloats = NumBones * 6; // 2 Columns of the rotation matrix.
	OutRotations.Reset(NumFloats);
	OutRotations.AddUninitialized(NumFloats);
	int32 Offset = 0;
	for (int32 Index = 0; Index < NumBones; ++Index)
	{
		const FName BoneName = GetBoneName(Index);
		const int32 SkelMeshBoneIndex = SkelMeshComponent->GetBoneIndex(BoneName);
		const FMatrix RotationMatrix = (SkelMeshBoneIndex != INDEX_NONE) ? BoneTransforms[SkelMeshBoneIndex].GetRotation().ToMatrix() : FMatrix::Identity;
		const FVector X = RotationMatrix.GetColumn(0);
		const FVector Y = RotationMatrix.GetColumn(1);	
		OutRotations[Offset++] = X.X;
		OutRotations[Offset++] = X.Y;
		OutRotations[Offset++] = X.Z;
		OutRotations[Offset++] = Y.X;
		OutRotations[Offset++] = Y.Y;
		OutRotations[Offset++] = Y.Z;
	}
}

int32 FMLDeformerInputInfo::CalcNumNeuralNetInputs() const
{
	return 
		BoneNameStrings.Num() * 6 +	// Six floats per bone.
		CurveNameStrings.Num();		// One float per curve.
}
