#include "ExampleAssetToolkit.h"

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
            if (AssetEditorInstance == static_cast<IAssetEditorInstance*>(this))
            {
                ViewportClient->GetModeTools()->SetToolkitHost(GetToolkitHost());
                ViewportClient->GetModeTools()->ActivateMode(GetDefault<UGizmoEdMode>()->GetID());
                AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(WindowOpenedDelegateHandle);
                WindowOpenedDelegateHandle.Reset();
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
