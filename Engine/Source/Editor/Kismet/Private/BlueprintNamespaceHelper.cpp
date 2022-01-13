// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceHelper.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorSettings.h"
#include "Settings/BlueprintEditorProjectSettings.h"
#include "Toolkits/ToolkitManager.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "SPinTypeSelector.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ClassViewerFilter.h"
#include "BlueprintNamespacePathTree.h"
#include "BlueprintNamespaceUtilities.h"

#define LOCTEXT_NAMESPACE "BlueprintNamespaceHelper"

// ---
// @todo_namespaces - Remove CVar flags/sink below after converting to editable 'config' properties
// ---

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceFilteringFeatures(
	TEXT("BP.EnableNamespaceFilteringFeatures"),
	false,
	TEXT("Enables namespace filtering features in the Blueprint editor (experimental).")
);

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceImportingFeatures(
	TEXT("BP.EnableNamespaceImportingFeatures"),
	false,
	TEXT("Enables namespace importing features in the Blueprint editor (experimental)."));

static TAutoConsoleVariable<bool> CVarBPImportParentClassNamespaces(
	TEXT("BP.ImportParentClassNamespaces"),
	false,
	TEXT("Enables import of parent class namespaces when opening a Blueprint for editing."));

static void UpdateNamespaceFeatureSettingsCVarSinkFunction()
{
	// Note: Do NOT try to access settings objects below during the initial editor load! They rely on the config being loaded, which may not have occurred yet.
	if (GIsInitialLoad || !GEditor)
	{
		return;
	}

	auto CheckAndUpdateSettingValueLambda = [](bool& CurValue, const bool NewValue) -> bool
	{
		if (CurValue != NewValue)
		{
			CurValue = NewValue;
			return true;
		}

		return false;
	};

	bool bWasUpdated = false;

	// Blueprint editor settings.
	UBlueprintEditorSettings* BlueprintEditorSettingsPtr = GetMutableDefault<UBlueprintEditorSettings>();
	bWasUpdated |= CheckAndUpdateSettingValueLambda(BlueprintEditorSettingsPtr->bEnableNamespaceFilteringFeatures, CVarBPEnableNamespaceFilteringFeatures.GetValueOnGameThread());
	bWasUpdated |= CheckAndUpdateSettingValueLambda(BlueprintEditorSettingsPtr->bEnableNamespaceImportingFeatures, CVarBPEnableNamespaceImportingFeatures.GetValueOnGameThread());

	if (bWasUpdated)
	{
		// Refresh all relevant open Blueprint editor UI elements.
		// @todo_namespaces - Move this into PostEditChangeProperty() on the appropriate settings object(s).
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
						BlueprintEditorPtr->RefreshEditors();
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

class FClassViewerNamespaceFilter : public IClassViewerFilter
{
public:
	FClassViewerNamespaceFilter(const FBlueprintNamespaceHelper* InNamespaceHelper)
		: CachedNamespaceHelper(InNamespaceHelper)
	{
	}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		return CachedNamespaceHelper->IsImportedObject(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InBlueprint, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		FSoftObjectPath ClassPath(InBlueprint->GetClassPath());
		return CachedNamespaceHelper->IsImportedObject(ClassPath);
	}

private:
	/** Associated namespace helper object. */
	const FBlueprintNamespaceHelper* CachedNamespaceHelper;
};

// ---

class FPinTypeSelectorNamespaceFilter : public IPinTypeSelectorFilter
{
	DECLARE_MULTICAST_DELEGATE(FOnFilterChanged);

public:
	FPinTypeSelectorNamespaceFilter(const FBlueprintNamespaceHelper* InNamespaceHelper)
		: CachedNamespaceHelper(InNamespaceHelper)
	{
	}

	virtual bool ShouldShowPinTypeTreeItem(FPinTypeTreeItem InItem) const override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		const bool bForceLoadSubCategoryObject = false;
		const FEdGraphPinType& PinType = InItem->GetPinType(bForceLoadSubCategoryObject);

		if (PinType.PinSubCategoryObject.IsValid() && !CachedNamespaceHelper->IsImportedObject(PinType.PinSubCategoryObject.Get()))
		{
			// A pin type whose underlying object is loaded, but not imported.
			return false;
		}
		else
		{
			const FSoftObjectPath& AssetRef = InItem->GetSubCategoryObjectAsset();
			if (AssetRef.IsValid() && !CachedNamespaceHelper->IsImportedObject(AssetRef))
			{
				// A pin type whose underlying asset may be either loaded or unloaded, but is not imported.
				return false;
			}
		}

		return true;
	}

private:
	/** Associated namespace helper object. */
	const FBlueprintNamespaceHelper* CachedNamespaceHelper;
};

// ---

FBlueprintNamespaceHelper::FBlueprintNamespaceHelper(const UBlueprint* InBlueprint)
{
	// Instance the path tree used to store/retrieve namespaces.
	NamespacePathTree = MakeUnique<FBlueprintNamespacePathTree>();

	// Add the default namespace paths implicitly imported by every Blueprint.
	AddNamespaces(GetDefault<UBlueprintEditorSettings>()->NamespacesToAlwaysInclude);
	AddNamespaces(GetDefault<UBlueprintEditorProjectSettings>()->NamespacesToAlwaysInclude);

	if (InBlueprint)
	{
		// Add the namespace for the given Blueprint.
		AddNamespace(FBlueprintNamespaceUtilities::GetObjectNamespace(InBlueprint));

		// Also add the namespace for the Blueprint's parent class.
		AddNamespace(FBlueprintNamespaceUtilities::GetObjectNamespace(InBlueprint->ParentClass));

		// Additional namespaces that are explicitly imported by this Blueprint.
		AddNamespaces(InBlueprint->ImportedNamespaces);

		// If enabled, also inherit namespaces that are explicitly imported by all ancestor BPs.
		const bool bAddParentClassNamespaces = CVarBPImportParentClassNamespaces.GetValueOnGameThread();
		if(bAddParentClassNamespaces)
		{
			const UClass* ParentClass = InBlueprint->ParentClass;
			while (ParentClass)
			{
				if (const UBlueprint* ParentClassBlueprint = UBlueprint::GetBlueprintFromClass(ParentClass))
				{
					AddNamespaces(ParentClassBlueprint->ImportedNamespaces);
				}
				else
				{
					break;
				}

				ParentClass = ParentClass->GetSuperClass();
			}
		}
	}

	// Instance the filters that can be used with type pickers, etc.
	ClassViewerFilter = MakeShared<FClassViewerNamespaceFilter>(this);
	PinTypeSelectorFilter = MakeShared<FPinTypeSelectorNamespaceFilter>(this);
}

FBlueprintNamespaceHelper::~FBlueprintNamespaceHelper()
{
}

void FBlueprintNamespaceHelper::AddNamespace(const FString& Namespace)
{
	if (!Namespace.IsEmpty())
	{
		// Add the path corresponding to the given namespace identifier.
		NamespacePathTree->AddPath(Namespace);
	}
}

bool FBlueprintNamespaceHelper::IsIncludedInNamespaceList(const FString& TestNamespace) const
{
	// Empty namespace == global namespace
	if (TestNamespace.IsEmpty())
	{
		return true;
	}

	// Check to see if X is added, followed by X.Y (which contains X.Y.Z), and so on until we run out of path segments
	const bool bMatchFirstInclusivePath = true;
	TSharedPtr<FBlueprintNamespacePathTree::FNode> PathNode = NamespacePathTree->FindPathNode(TestNamespace, bMatchFirstInclusivePath);

	// Return true if this is a valid path that was explicitly added
	return PathNode.IsValid();
}

bool FBlueprintNamespaceHelper::IsImportedObject(const UObject* InObject) const
{
	// Determine the object's namespace identifier.
	FString Namespace = FBlueprintNamespaceUtilities::GetObjectNamespace(InObject);

	// Return whether or not the namespace was added, explicitly or otherwise.
	return IsIncludedInNamespaceList(Namespace);
}

bool FBlueprintNamespaceHelper::IsImportedObject(const FSoftObjectPath& InObjectPath) const
{
	// Determine the object's namespace identifier.
	FString Namespace = FBlueprintNamespaceUtilities::GetObjectNamespace(InObjectPath);

	// Return whether or not the namespace was added, explicitly or otherwise.
	return IsIncludedInNamespaceList(Namespace);
}

bool FBlueprintNamespaceHelper::IsImportedAsset(const FAssetData& InAssetData) const
{
	// Determine the asset's namespace identifier.
	FString Namespace = FBlueprintNamespaceUtilities::GetAssetNamespace(InAssetData);

	// Return whether or not the namespace was added, explicitly or otherwise.
	return IsIncludedInNamespaceList(Namespace);
}

#undef LOCTEXT_NAMESPACE