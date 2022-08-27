// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFLevelSequenceExporter.h"
#include "Exporters/GLTFExporterUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "LevelSequence.h"

UGLTFLevelSequenceExporter::UGLTFLevelSequenceExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULevelSequence::StaticClass();
}

bool UGLTFLevelSequenceExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const ULevelSequence* LevelSequence = CastChecked<ULevelSequence>(Object);

	if (!Builder.ExportOptions->bExportLevelSequences)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level sequence %s because level sequences are disabled by export options"),
			*LevelSequence->GetName()));
		return false;
	}

	TArray<ULevel*> Levels = FGLTFExporterUtility::GetReferencedLevels(LevelSequence);
	if (Levels.Num() == 0)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level sequence %s because no level referenced"),
			*LevelSequence->GetName()));
		return false;
	}

	if (Levels.Num() > 1)
	{
		Builder.AddErrorMessage(
            FString::Printf(TEXT("Failed to export level sequence %s because more than one level referenced"),
            *LevelSequence->GetName()));
		return false;
	}

	ULevel* Level = Levels[0];
	const FGLTFJsonSceneIndex SceneIndex = Builder.GetOrAddScene(Level);
	if (SceneIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level %s for level sequence %s"),
			*Level->GetName(),
			*LevelSequence->GetName()));
		return false;
	}

	const FGLTFJsonAnimationIndex AnimationIndex = Builder.GetOrAddAnimation(Level, LevelSequence);
	if (AnimationIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(
			FString::Printf(TEXT("Failed to export level sequence %s"),
			*LevelSequence->GetName()));
		return false;
	}

	return true;
}
