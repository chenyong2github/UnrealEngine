// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceHelper.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorSettings.h"
#include "Settings/EditorProjectSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Toolkits/ToolkitManager.h"

FBlueprintNamespaceHelper::FBlueprintNamespaceHelper(const UBlueprint* InBlueprint)
{
	// Default namespace paths implicitly imported by every Blueprint.
	AddNamespaces(GetDefault<UBlueprintEditorSettings>()->NamespacesToAlwaysInclude);
	AddNamespaces(GetDefault<UBlueprintEditorProjectSettings>()->NamespacesToAlwaysInclude);

	if (InBlueprint)
	{
		AddNamespace(InBlueprint->BlueprintNamespace);
		AddNamespaces(InBlueprint->ImportedNamespaces);

		// @todo - Add parent class namespaces as well?
	}
}

bool FBlueprintNamespaceHelper::IsIncludedInNamespaceList(const FString& TestNamespace) const
{
	// Empty namespace == global namespace
	if (TestNamespace.IsEmpty())
	{
		return true;
	}

	// Check recursively to see if X.Y.Z is present, and if not X.Y (which contains X.Y.Z), and so on until we run out of path segments
	if (FullyQualifiedListOfNamespaces.Contains(TestNamespace))
	{
		return true;
	}
	else
	{
		int32 RightmostDotIndex;
		if (TestNamespace.FindLastChar(TEXT('.'), /*out*/ RightmostDotIndex))
		{
			if (RightmostDotIndex > 0)
			{
				return IsIncludedInNamespaceList(TestNamespace.Left(RightmostDotIndex));
			}
		}
	}

	return false;
}

// ---
// @todo_namespaces - Remove CVar flags/sink below after converting to editable 'config' properties
// ---

static TAutoConsoleVariable<bool> CVarEnableNamespaceFilteringFeatures(
	TEXT("Editor.EnableNamespaceFilteringFeatures"),
	false,
	TEXT("Enables namespace filtering features in the editor UI (experimental).")
);

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceImportingFeatures(
	TEXT("BP.EnableNamespaceImportingFeatures"),
	false,
	TEXT("Enables namespace importing features in the Blueprint editor (experimental)."));

static void UpdateNamespaceFeatureSettingsCVarSinkFunction()
{
	// Global editor UI settings.
	UEditorExperimentalSettings* EditorSettingsPtr = GetMutableDefault<UEditorExperimentalSettings>();
	EditorSettingsPtr->bEnableNamespaceFilteringFeatures = CVarEnableNamespaceFilteringFeatures.GetValueOnGameThread();

	// Blueprint editor settings.
	UBlueprintEditorSettings* BlueprintEditorSettingsPtr = GetMutableDefault<UBlueprintEditorSettings>();
	BlueprintEditorSettingsPtr->bEnableNamespaceImportingFeatures = CVarBPEnableNamespaceImportingFeatures.GetValueOnGameThread();

	// Refresh all relevant open Blueprint editor UI elements.
	// @todo_namespaces - Move this into PostEditChangeProperty() on the appropriate settings object(s).
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				if (Asset && Asset->IsA<UBlueprint>())
				{
					TSharedPtr<IToolkit> AssetEditorPtr = FToolkitManager::Get().FindEditorForAsset(Asset);
					if (AssetEditorPtr.IsValid() && AssetEditorPtr->IsBlueprintEditor())
					{
						TSharedPtr<IBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<IBlueprintEditor>(AssetEditorPtr);
						BlueprintEditorPtr->RefreshMyBlueprint();
						BlueprintEditorPtr->RefreshInspector();
					}
				}
			}
		}
	}
}

static FAutoConsoleVariableSink CVarUpdateNamespaceFeatureSettingsSink(
	FConsoleCommandDelegate::CreateStatic(&UpdateNamespaceFeatureSettingsCVarSinkFunction)
);

// ---