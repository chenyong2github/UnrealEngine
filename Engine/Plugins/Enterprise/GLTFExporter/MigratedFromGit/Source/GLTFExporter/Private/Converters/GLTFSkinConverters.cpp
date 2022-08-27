// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSkinConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFBoneUtility.h"
#include "Builders/GLTFConvertBuilder.h"

FGLTFJsonSkin* FGLTFSkinConverter::Convert(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	const USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	const USkeleton* Skeleton = SkeletalMesh->Skeleton;
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	FGLTFJsonSkin* JsonSkin = Builder.AddSkin();
	JsonSkin->Name = Skeleton != nullptr ? Skeleton->GetName() : SkeletalMesh->GetName();
	JsonSkin->Skeleton = RootNode;

	const int32 BoneCount = RefSkeleton.GetNum();
	if (BoneCount == 0)
	{
		// TODO: report warning
		return nullptr;
	}

	JsonSkin->Joints.AddUninitialized(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		JsonSkin->Joints[BoneIndex] = Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneIndex);
	}

	TArray<FGLTFMatrix4> InverseBindMatrices;
	InverseBindMatrices.AddUninitialized(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FTransform InverseBindTransform = FGLTFBoneUtility::GetBindTransform(RefSkeleton, BoneIndex).Inverse();
		InverseBindMatrices[BoneIndex] = FGLTFConverterUtility::ConvertTransform(InverseBindTransform, Builder.ExportOptions->ExportUniformScale);
	}

	FGLTFJsonAccessor* JsonBindMatricesAccessor = Builder.AddAccessor();
	JsonBindMatricesAccessor->BufferView = Builder.AddBufferView(InverseBindMatrices);
	JsonBindMatricesAccessor->ComponentType = EGLTFJsonComponentType::Float;
	JsonBindMatricesAccessor->Count = BoneCount;
	JsonBindMatricesAccessor->Type = EGLTFJsonAccessorType::Mat4;

	JsonSkin->InverseBindMatrices = JsonBindMatricesAccessor;
	return JsonSkin;
}
