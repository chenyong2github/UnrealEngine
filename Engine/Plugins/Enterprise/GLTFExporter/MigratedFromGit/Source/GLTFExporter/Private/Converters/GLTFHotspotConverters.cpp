// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFHotspotConverters.h"
#include "Json/FGLTFJsonHotspot.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Animation/SkeletalMeshActor.h"

FGLTFJsonHotspotIndex FGLTFHotspotConverter::Convert(const AGLTFInteractionHotspotActor* HotspotActor)
{
	FGLTFJsonHotspot JsonHotspot;
	HotspotActor->GetName(JsonHotspot.Name);

	if (!Builder.ExportOptions->bExportVertexSkinWeights)
	{
		Builder.AddWarningMessage(
			FString::Printf(TEXT("Can't export animation in hotspot %s because vertex skin weights are disabled by export options"),
			*JsonHotspot.Name));
	}
	else if (!Builder.ExportOptions->bExportAnimationSequences)
	{
		Builder.AddWarningMessage(
			FString::Printf(TEXT("Can't export animation in hotspot %s because animation sequences are disabled by export options"),
			*JsonHotspot.Name));
	}
	else
	{
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
	}

	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotActor->Image);
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotActor->HoveredImage);
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotActor->ToggledImage);
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotActor->ToggledHoveredImage);

	return Builder.AddHotspot(JsonHotspot);
}
