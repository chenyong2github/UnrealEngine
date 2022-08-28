// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSkinConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Builders/GLTFConvertBuilder.h"

FGLTFJsonSkinIndex FGLTFSkinConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh)
{
	FGLTFJsonSkin Skin;
	Skin.Name = SkeletalMesh->Skeleton != nullptr ? SkeletalMesh->Skeleton->GetName() : SkeletalMesh->GetName();
	Skin.Skeleton = RootNode;

	const int32 BoneCount = SkeletalMesh->RefSkeleton.GetNum();
	Skin.Joints.AddUninitialized(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		Skin.Joints[BoneIndex] = Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneIndex);
	}

	return Builder.AddSkin(Skin);
}
