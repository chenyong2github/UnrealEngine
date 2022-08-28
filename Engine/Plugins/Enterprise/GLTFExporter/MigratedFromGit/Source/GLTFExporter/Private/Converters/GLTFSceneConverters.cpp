// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSceneConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"

FGLTFJsonSceneIndex FGLTFLevelConverter::Convert(const ULevel* Level)
{
	FGLTFJsonScene Scene;

	if (const UWorld* World = Level->GetWorld())
	{
		World->GetName(Scene.Name);
	}
	else
	{
		Level->GetName(Scene.Name);
	}

	// TODO: export Level->Model ?

	for (const AActor* Actor : Level->Actors)
	{
		// TODO: should a LevelVariantSet be exported even if not selected for export?
		if (const ALevelVariantSetsActor *LevelVariantSetsActor = Cast<ALevelVariantSetsActor>(Actor))
		{
			if (Builder.ExportOptions->bExportVariantSets)
			{
				const FGLTFJsonLevelVariantSetsIndex LevelVariantSetsIndex = Builder.GetOrAddLevelVariantSets(LevelVariantSetsActor);
				if (LevelVariantSetsIndex != INDEX_NONE)
				{
					Scene.LevelVariantSets.Add(LevelVariantSetsIndex);
				}
			}
			else
			{
				Builder.AddWarningMessage(FString::Printf(
					TEXT("Level Variant Set %s disabled by export options"),
					*LevelVariantSetsActor->GetName()));
			}
		}

		const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Actor);
		if (NodeIndex != INDEX_NONE && FGLTFActorUtility::IsRootActor(Actor, Builder.bSelectedActorsOnly))
		{
			Scene.Nodes.Add(NodeIndex);
		}
	}

	return Builder.AddScene(Scene);
}
