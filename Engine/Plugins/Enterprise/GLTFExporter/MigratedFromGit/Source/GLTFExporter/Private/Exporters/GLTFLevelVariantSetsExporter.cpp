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
		Builder.LogError(
			FString::Printf(TEXT("Failed to export level variant sets %s because variant sets are disabled by export options"),
			*LevelVariantSets->GetName()));
		return false;
	}

	TArray<UWorld*> Worlds = FGLTFExporterUtility::GetAssociatedWorlds(LevelVariantSets);
	if (Worlds.Num() == 0)
	{
		Builder.LogError(
			FString::Printf(TEXT("Failed to export level variant sets %s because no associated level"),
			*LevelVariantSets->GetName()));
		return false;
	}

	if (Worlds.Num() > 1)
	{
		Builder.LogError(
			FString::Printf(TEXT("Failed to export level variant sets %s because more than one associated level"),
			*LevelVariantSets->GetName()));
		return false;
	}

	UWorld* World = Worlds[0];
	const FGLTFJsonSceneIndex SceneIndex = Builder.GetOrAddScene(World);
	if (SceneIndex == INDEX_NONE)
	{
		Builder.LogError(
			FString::Printf(TEXT("Failed to export level %s for level variant sets %s"),
			*World->GetName(),
			*LevelVariantSets->GetName()));
		return false;
	}

	const FGLTFJsonEpicLevelVariantSetsIndex EpicLevelVariantSetsIndex = Builder.GetOrAddEpicLevelVariantSets(LevelVariantSets);
	if (EpicLevelVariantSetsIndex == INDEX_NONE)
	{
		Builder.LogError(
			FString::Printf(TEXT("Failed to export level variant sets %s"),
			*LevelVariantSets->GetName()));
		return false;
	}

	Builder.GetScene(SceneIndex).EpicLevelVariantSets.AddUnique(EpicLevelVariantSetsIndex);

	Builder.DefaultScene = SceneIndex;
	return true;
}
