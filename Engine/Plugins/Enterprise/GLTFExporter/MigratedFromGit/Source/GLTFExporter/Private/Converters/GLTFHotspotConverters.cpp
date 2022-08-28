// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFHotspotConverters.h"
#include "Json/FGLTFJsonHotspot.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Animation/SkeletalMeshActor.h"

FGLTFJsonHotspotIndex FGLTFHotspotConverter::Convert(const AGLTFInteractionHotspotActor* HotspotActor)
{
	FGLTFJsonHotspot JsonHotspot;
	HotspotActor->GetName(JsonHotspot.Name);

	if (const ASkeletalMeshActor* SkeletalMeshActor = HotspotActor->SkeletalMeshActor)
	{
		const FGLTFJsonNodeIndex RootNode = Builder.GetOrAddNode(SkeletalMeshActor);
		if (const USkeletalMesh* SkeletalMesh = SkeletalMeshActor->GetSkeletalMeshComponent()->SkeletalMesh)
		{
			if (const UAnimSequence* AnimSequence = HotspotActor->AnimationSequence)
			{
				if (SkeletalMesh->Skeleton == AnimSequence->GetSkeleton())
				{
					JsonHotspot.Animation = Builder.GetOrAddAnimation(RootNode, SkeletalMesh, AnimSequence);
				}
				else
				{
					// TODO: report warning
				}
			}
			else
			{
				// TODO: report warning
			}
		}
		else
		{
			// TODO: report warning
		}
	}
	else
	{
		// TODO: report warning
	}

	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotActor->Image);
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotActor->HoveredImage);
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotActor->ToggledImage);
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotActor->ToggledHoveredImage);

	return Builder.AddHotspot(JsonHotspot);
}
