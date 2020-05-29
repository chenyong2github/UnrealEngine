// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleAssetToolkit.h"

#include "AssetEditorModeManager.h"
#include "EditorModeManager.h"
#include "GizmoEdMode.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tools/UAssetEditor.h"
#include "EditorViewportClient.h"

FExampleAssetToolkit::FExampleAssetToolkit(UAssetEditor* InOwningAssetEditor)
    : FBaseAssetToolkit(InOwningAssetEditor)
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	WindowOpenedDelegateHandle                  = AssetEditorSubsystem->OnAssetOpenedInEditor().AddLambda(
        [this, AssetEditorSubsystem](UObject* Asset, IAssetEditorInstance* AssetEditorInstance) {
            if (AssetEditorInstance == static_cast<IAssetEditorInstance*>(this) && !bWindowHasOpened)
            {
				bWindowHasOpened = true;
				FAssetEditorModeManager* ModeManager = static_cast<FAssetEditorModeManager*>(ViewportClient->GetModeTools());
				ModeManager->SetPreviewScene(ViewportClient->GetPreviewScene());
                ModeManager->SetToolkitHost(GetToolkitHost());
                ModeManager->ActivateMode(GetDefault<UGizmoEdMode>()->GetID());
            }
        });
}

FExampleAssetToolkit::~FExampleAssetToolkit()
{
	ViewportClient->GetModeTools()->DeactivateMode(GetDefault<UGizmoEdMode>()->GetID());
	if (WindowOpenedDelegateHandle.IsValid())
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(WindowOpenedDelegateHandle);
	}
}
