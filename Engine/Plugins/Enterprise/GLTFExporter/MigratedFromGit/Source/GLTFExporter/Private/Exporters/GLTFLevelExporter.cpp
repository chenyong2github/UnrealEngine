// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFLevelExporter.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/World.h"

UGLTFLevelExporter::UGLTFLevelExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
}

bool UGLTFLevelExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	const UWorld* World = CastChecked<UWorld>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	FGLTFContainerBuilder Builder;
	const FGLTFJsonSceneIndex SceneIndex = Builder.GetOrAddScene(World, bSelectedOnly);
	Builder.DefaultScene = SceneIndex;

	return Builder.Serialize(Archive, CurrentFilename);
}
