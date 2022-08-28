// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSceneConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "LevelVariantSetsActor.h"
#include "LevelVariantSets.h"
#include "VariantSet.h"

FGLTFJsonSceneIndex FGLTFSceneConverter::Convert(const UWorld* World)
{
	FGLTFJsonScene Scene;
	World->GetName(Scene.Name);

	const TArray<ULevel*>& Levels = World->GetLevels();
	if (Levels.Num() > 0)
	{
		for (const ULevel* Level : Levels)
		{
			if (Level == nullptr)
			{
				continue;
			}

			// TODO: add support for exporting Level->Model?

			if (Builder.ExportOptions->VariantSetsMode != EGLTFVariantSetsMode::None)
			{
				for (const AActor* Actor : Level->Actors)
				{
					// TODO: should a LevelVariantSet be exported even if not selected for export?
					if (const ALevelVariantSetsActor *LevelVariantSetsActor = Cast<ALevelVariantSetsActor>(Actor))
					{
						const ULevelVariantSets* LevelVariantSets = const_cast<ALevelVariantSetsActor*>(LevelVariantSetsActor)->GetLevelVariantSets(true);

						if (Builder.ExportOptions->VariantSetsMode == EGLTFVariantSetsMode::Epic)
						{
							if (LevelVariantSets != nullptr)
							{
								const FGLTFJsonEpicLevelVariantSetsIndex EpicLevelVariantSetsIndex = Builder.GetOrAddEpicLevelVariantSets(LevelVariantSets);
								if (EpicLevelVariantSetsIndex != INDEX_NONE)
								{
									Scene.EpicLevelVariantSets.Add(EpicLevelVariantSetsIndex);
								}
							}
						}
						else if (Builder.ExportOptions->VariantSetsMode == EGLTFVariantSetsMode::Khronos)
						{
							if (LevelVariantSets != nullptr)
							{
								for (const UVariantSet* VariantSet: LevelVariantSets->GetVariantSets())
								{
									for (const UVariant* Variant: VariantSet->GetVariants())
									{
										Builder.GetOrAddKhrMaterialVariant(Variant);
									}
								}
							}
						}
					}
				}
			}

			for (const AActor* Actor : Level->Actors)
			{
				const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Actor);
				if (NodeIndex != INDEX_NONE && Builder.IsRootActor(Actor))
				{
					// TODO: to avoid having to add irrelevant actors/components let GLTFComponentConverter decide and add root nodes to scene.
					// This change may require node converters to support cyclic calls.
					Scene.Nodes.Add(NodeIndex);
				}
			}
		}
	}
	else
	{
		Builder.LogWarning(
			FString::Printf(TEXT("World %s has no levels. Please make sure the world has been fully initialized"),
			*World->GetName()));
	}

	return Builder.AddScene(Scene);
}
