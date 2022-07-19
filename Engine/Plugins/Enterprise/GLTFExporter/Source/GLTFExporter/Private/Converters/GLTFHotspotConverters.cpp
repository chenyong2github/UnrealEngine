// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFHotspotConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"

FGLTFJsonHotspot* FGLTFHotspotConverter::Convert(const AGLTFHotspotActor* HotspotActor)
{
	FGLTFJsonHotspot* JsonHotspot = Builder.AddHotspot();
	HotspotActor->GetName(JsonHotspot->Name);

	if (const ASkeletalMeshActor* SkeletalMeshActor = HotspotActor->SkeletalMeshActor)
	{
		if (!Builder.ExportOptions->bExportVertexSkinWeights)
		{
			Builder.LogWarning(
				FString::Printf(TEXT("Can't export animation in hotspot %s because vertex skin weights are disabled by export options"),
				*JsonHotspot->Name));
		}
		else if (!Builder.ExportOptions->bExportAnimationSequences)
		{
			Builder.LogWarning(
				FString::Printf(TEXT("Can't export animation in hotspot %s because animation sequences are disabled by export options"),
				*JsonHotspot->Name));
		}
		else
		{
			FGLTFJsonNode* RootNode = Builder.AddUniqueNode(SkeletalMeshActor);

			const USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshActor->GetSkeletalMeshComponent();
			if (const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh())
			{
				if (const UAnimSequence* AnimSequence = HotspotActor->AnimationSequence)
				{
					JsonHotspot->Animation = Builder.AddUniqueAnimation(RootNode, SkeletalMesh, AnimSequence);
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
	}
	else if (const ULevelSequence* LevelSequence = HotspotActor->LevelSequence)
	{
		if (!Builder.ExportOptions->bExportLevelSequences)
		{
			Builder.LogWarning(
				FString::Printf(TEXT("Can't export animation in hotspot %s because level sequences are disabled by export options"),
				*JsonHotspot->Name));
		}
		else
		{
			JsonHotspot->Animation = Builder.AddUniqueAnimation(HotspotActor->GetLevel(), LevelSequence);
		}
	}
	else
	{
		// TODO: report warning
	}

	JsonHotspot->Image = Builder.AddUniqueTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Default));
	JsonHotspot->HoveredImage = Builder.AddUniqueTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Hovered));
	JsonHotspot->ToggledImage = Builder.AddUniqueTexture(HotspotActor->GetImageForState(EGLTFHotspotState::Toggled));
	JsonHotspot->ToggledHoveredImage = Builder.AddUniqueTexture(HotspotActor->GetImageForState(EGLTFHotspotState::ToggledHovered));

	return JsonHotspot;
}
