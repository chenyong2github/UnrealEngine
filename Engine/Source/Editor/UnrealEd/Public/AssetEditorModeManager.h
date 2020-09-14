// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorModeManager.h"

class FPreviewScene;
class UTypedElementList;

//////////////////////////////////////////////////////////////////////////
// FAssetEditorModeManager

class UNREALED_API FAssetEditorModeManager : public FEditorModeTools
{
public:
	FAssetEditorModeManager();
	virtual ~FAssetEditorModeManager();

	// FEditorModeTools interface
	virtual class USelection* GetSelectedActors() const override;
	virtual class USelection* GetSelectedObjects() const override;
	virtual class USelection* GetSelectedComponents() const override;
	virtual UWorld* GetWorld() const override;
	// End of FEditorModeTools interface

	void SetPreviewScene(class FPreviewScene* NewPreviewScene);
	FPreviewScene* GetPreviewScene() const;

protected:
	class USelection* ActorSet = nullptr;
	class USelection* ObjectSet = nullptr;
	class USelection* ComponentSet = nullptr;
	class FPreviewScene* PreviewScene = nullptr;
	UTypedElementList* SelectedElements = nullptr;
};
