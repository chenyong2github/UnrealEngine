// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargeterThumbnailRenderer.h"

#include "Engine/SkeletalMesh.h"
#include "Retargeter/IKRetargeter.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IKRetargeterThumbnailRenderer)

bool UIKRetargeterThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	return GetPreviewMeshFromAsset(Object) != nullptr;
}

void UIKRetargeterThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (USkeletalMesh* MeshToDraw = GetPreviewMeshFromAsset(Object))
	{
		Super::Draw(MeshToDraw, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);	
	}
}

EThumbnailRenderFrequency UIKRetargeterThumbnailRenderer::GetThumbnailRenderFrequency(UObject* Object) const
{
	return GetPreviewMeshFromAsset(Object) ? EThumbnailRenderFrequency::Realtime : EThumbnailRenderFrequency::OnPropertyChange;
}

USkeletalMesh* UIKRetargeterThumbnailRenderer::GetPreviewMeshFromAsset(UObject* Object) const
{
	const UIKRetargeter* InRetargeter = Cast<UIKRetargeter>(Object);
	if (!InRetargeter)
	{
		return nullptr;
	}

	// prefer target mesh for thumbnail
	if (const UIKRigDefinition* TargetIKRig =  InRetargeter->GetTargetIKRig())
	{
		if (USkeletalMesh* TargetMesh = TargetIKRig->GetPreviewMesh())
		{
			return TargetMesh;
		}
	}

	// fallback to source mesh if no target applied
	if (const UIKRigDefinition* SourceIKRig =  InRetargeter->GetSourceIKRig())
	{
		if (USkeletalMesh* SourceMesh = SourceIKRig->GetPreviewMesh())
		{
			return SourceMesh;
		}
	}

	return nullptr;
}
