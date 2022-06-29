// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "BlueprintActionDatabase.h"
#include "FindInBlueprintManager.h"
#include "BlueprintTypePromotion.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditorModule.h"
#include "BlueprintNamespaceHelper.h"
#include "BlueprintNamespaceUtilities.h"

UBlueprintEditorSettings::UBlueprintEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	// Style Settings
	, bDrawMidpointArrowsInBlueprints(false)
	, bShowGraphInstructionText(true)
	, bHideUnrelatedNodes(false)
	, bShowShortTooltips(true)
	// Workflow Settings
	, bSplitContextTargetSettings(true)
	, bExposeAllMemberComponentFunctions(true)
	, bShowContextualFavorites(false)
	, bExposeDeprecatedFunctions(false)
	, bCompactCallOnMemberNodes(false)
	, bFlattenFavoritesMenus(true)
	, bAutoCastObjectConnections(false)
	, bShowViewportOnSimulate(false)
	, bSpawnDefaultBlueprintNodes(true)
	, bHideConstructionScriptComponentsInDetailsView(true)
	, bHostFindInBlueprintsInGlobalTab(true)
	, bNavigateToNativeFunctionsFromCallNodes(true)
	, bDoubleClickNavigatesToParent(true)
	, bEnableTypePromotion(true)
	, TypePromotionPinDenyList { UEdGraphSchema_K2::PC_String, UEdGraphSchema_K2::PC_Text, UEdGraphSchema_K2::PC_SoftClass }
	, BreakpointReloadMethod(EBlueprintBreakpointReloadMethod::RestoreAll)
	, bEnablePinValueInspectionTooltips(true)
	// Experimental
	, bEnableNamespaceEditorFeatures(false)
	, bEnableNamespaceFilteringFeatures(false)
	, bEnableNamespaceImportingFeatures(false)
	, bInheritImportedNamespacesFromParentBP(false)
	, bFavorPureCastNodes(false)
	// Compiler Settings
	, SaveOnCompile(SoC_Never)
	, bJumpToNodeErrors(false)
	, bAllowExplicitImpureNodeDisabling(false)
	// Developer Settings
	, bShowActionMenuItemSignatures(false)
	// Perf Settings
	, bShowDetailedCompileResults(false)
	, CompileEventDisplayThresholdMs(5)
	, NodeTemplateCacheCapMB(20.f)
	// No category
	, bShowInheritedVariables(false)
	, bAlwaysShowInterfacesInOverrides(true)
	, bShowParentClassInOverrides(true)
	, bShowEmptySections(true)
	, bShowAccessSpecifier(false)
	, bIncludeCommentNodesInBookmarksTab(true)
	, bShowBookmarksForCurrentDocumentOnlyInTab(false)
{
	// settings that were moved out of experimental...
	UEditorExperimentalSettings const* ExperimentalSettings = GetDefault<UEditorExperimentalSettings>();
	bDrawMidpointArrowsInBlueprints = ExperimentalSettings->bDrawMidpointArrowsInBlueprints;

	// settings that were moved out of editor-user settings...
	UEditorPerProjectUserSettings const* UserSettings = GetDefault<UEditorPerProjectUserSettings>();
	bShowActionMenuItemSignatures = UserSettings->bDisplayActionListItemRefIds;

	FString const ClassConfigKey = GetClass()->GetPathName();

	bool bOldSaveOnCompileVal = false;
	// backwards compatibility: handle the case where users have already switched this on
	if (GConfig->GetBool(*ClassConfigKey, TEXT("bSaveOnCompile"), bOldSaveOnCompileVal, GEditorPerProjectIni) && bOldSaveOnCompileVal)
	{
		SaveOnCompile = SoC_SuccessOnly;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().OnAssetRenamed().AddUObject(this, &UBlueprintEditorSettings::OnAssetRenamed);
	AssetRegistryModule.Get().OnInMemoryAssetDeleted().AddUObject(this, &UBlueprintEditorSettings::OnAssetRemoved);
}

void UBlueprintEditorSettings::OnAssetRenamed(FAssetData const& AssetInfo, const FString& InOldName)
{
	FPerBlueprintSettings Temp;
	if(PerBlueprintSettings.RemoveAndCopyValue(InOldName, Temp))
	{
		PerBlueprintSettings.Add(AssetInfo.ObjectPath.ToString(), Temp);
		SaveConfig();
	}
}

void UBlueprintEditorSettings::OnAssetRemoved(UObject* Object)
{
	if(UBlueprint* Blueprint = Cast<UBlueprint>(Object))
	{
		FKismetDebugUtilities::ClearBreakpoints(Blueprint);
		FKismetDebugUtilities::ClearPinWatches(Blueprint);
	}
}

void UBlueprintEditorSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Initialize transient flags and console variables for namespace editor features from the config.
	// @todo_namespaces - May be removed once dependent code is changed to utilize the config setting.
	bEnableNamespaceFilteringFeatures = bEnableNamespaceEditorFeatures;
	bEnableNamespaceImportingFeatures = bEnableNamespaceEditorFeatures;

	// Update console flags to match the current configuration.
	FBlueprintNamespaceHelper::RefreshEditorFeatureConsoleFlags();
}

void UBlueprintEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	bool bShouldRebuildRegistry = false;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, bExposeDeprecatedFunctions))
	{
		bShouldRebuildRegistry = true;
	}
	
	// Refresh type promotion when the preference gets changed so that we can correctly rebuild the action database
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, bEnableTypePromotion) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, TypePromotionPinDenyList))
	{
		FTypePromotion::RefreshPromotionTables();
		
		bShouldRebuildRegistry = true;
	}

	if (bShouldRebuildRegistry)
	{
		FBlueprintActionDatabase::Get().RefreshAll();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, bEnableNamespaceEditorFeatures))
	{
		// Update transient settings and console variable flags to reflect the new config setting value.
		// @todo_namespaces - May be removed once dependent code is changed to utilize the config setting.
		bEnableNamespaceFilteringFeatures = bEnableNamespaceEditorFeatures;
		bEnableNamespaceImportingFeatures = bEnableNamespaceEditorFeatures;

		// Update console flags to match the current configuration.
		FBlueprintNamespaceHelper::RefreshEditorFeatureConsoleFlags();

		// Refresh the Blueprint editor UI environment to match current settings.
		FBlueprintNamespaceUtilities::RefreshBlueprintEditorFeatures();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UBlueprintEditorSettings, NamespacesToAlwaysInclude))
	{
		// Close any open Blueprint editor windows so that we have a chance to reload them with the updated import set.
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		for (const TSharedRef<IBlueprintEditor>& BlueprintEditor : BlueprintEditorModule.GetBlueprintEditors())
		{
			BlueprintEditor->CloseWindow();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}