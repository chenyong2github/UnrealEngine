// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFHotspotConverters.h"
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

			const USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
			if (const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->SkeletalMesh)
			{
				if (const UAnimSequence* AnimSequence = HotspotActor->AnimationSequence)
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

	JsonHotspot.Image = Builder.GetOrAddTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Default));
	JsonHotspot.HoveredImage = Builder.GetOrAddTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Hovered));
	JsonHotspot.ToggledImage = Builder.GetOrAddTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Toggled));
	JsonHotspot.ToggledHoveredImage = Builder.GetOrAddTexture(HotspotActor->GetImageForState(EGLTFHotspotState::ToggledHovered));

	return Builder.AddHotspot(JsonHotspot);
}
