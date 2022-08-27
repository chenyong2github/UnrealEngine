// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFLevelVariantSetsExporter.h"
#include "Exporters/GLTFExporterUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "LevelVariantSets.h"

UGLTFLevelVariantSetsExporter::UGLTFLevelVariantSetsExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULevelVariantSets::StaticClass();
}

bool UGLTFLevelVariantSetsExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const ULevelVariantSets* LevelVariantSets = CastChecked<ULevelVariantSets>(Object);

	if (!Builder.ExportOptions->bExportVariantSets)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level variant sets %s because variant sets are disabled by export options"),
			*LevelVariantSets->GetName()));
		return false;
	}

	TArray<ULevel*> Levels = FGLTFExporterUtility::GetReferencedLevels(LevelVariantSets);
	if (Levels.Num() == 0)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level variant sets %s because no level referenced"),
			*LevelVariantSets->GetName()));
		return false;
	}

	if (Levels.Num() > 1)
	{
		Builder.AddErrorMessage(
            FString::Printf(TEXT("Failed to export level variant sets %s because more than one level referenced"),
            *LevelVariantSets->GetName()));
		return false;
	}

	ULevel* Level = Levels[0];
	const FGLTFJsonSceneIndex SceneIndex = Builder.GetOrAddScene(Level);
	if (SceneIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level %s for level variant sets %s"),
			*Level->GetName(),
			*LevelVariantSets->GetName()));
		return false;
	}

	const FGLTFJsonVariationIndex VariationIndex = Builder.GetOrAddVariation(LevelVariantSets);
	if (VariationIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level variant sets %s"),
			*LevelVariantSets->GetName()));
		return false;
	}

	Builder.GetScene(SceneIndex).Variations.AddUnique(VariationIndex);

	Builder.DefaultScene = SceneIndex;
	return true;
}
