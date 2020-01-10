// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Framework/Docking/TabManager.h"

class SGenerateUV;
class SDockTab;
class FMenuBuilder;
class FUICommandList;
class UStaticMesh;
class IStaticMeshEditor;
class FSpawnTabArgs;


class FUVGenerationToolbar : public TSharedFromThis<FUVGenerationToolbar>
{
public:
	virtual ~FUVGenerationToolbar();

	/** Add UV generation items to the StaticMesh Editor's toolbar */
	static void CreateUVMenu(FMenuBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* StaticMesh);

	/** Instantiate and register the UV generation tool to the StaticMeshEditor */
	static void CreateTool(const TWeakPtr<IStaticMeshEditor> InStaticMeshEditor);

private:
	bool Initialize(const TWeakPtr<IStaticMeshEditor>& InStaticMeshEditor);

	void DockStaticMeshEditorExtensionTabs(const TSharedRef<FTabManager::FStack>& TabStack);
	void RegisterStaticMeshEditorTabs(const TSharedRef<FTabManager>& TabManager);
	void UnregisterStaticMeshEditorTabs(const TSharedRef<FTabManager>& TabManager);

	TSharedRef<SDockTab> Spawn_UVGenerationToolTab(const FSpawnTabArgs& Args);

	/** Used to destroy the tool when the OnStaticMeshEditorClosed event is called */
	void OnCloseStaticMeshEditor();

	/** Pointer to the StaticMesh Editor hosting the toolbar */
	TWeakPtr<IStaticMeshEditor> StaticMeshEditorPtr;

	TSharedPtr<SGenerateUV> UVGenerationTab;

	TWeakPtr<SDockTab> UVGenerationToolTab;

	FDelegateHandle OnStaticMeshEditorClosedHandle;
};