// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFLevelExporter.h"
#include "GLTFContainerBuilder.h"
#include "Engine/World.h"

UGLTFLevelExporter::UGLTFLevelExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
}

bool UGLTFLevelExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UWorld* World = CastChecked<UWorld>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	FGLTFContainerBuilder Container;
	const FGLTFJsonSceneIndex SceneIndex = Container.AddScene(World, bSelectedOnly);
	Container.JsonRoot.DefaultScene = SceneIndex;

	Container.Serialize(Archive);
	return true;
}
