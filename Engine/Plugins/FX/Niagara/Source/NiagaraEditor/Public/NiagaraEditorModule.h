// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "NiagaraTypes.h"
#include "INiagaraCompiler.h"
#include "AssetTypeCategories.h"

class IAssetTools;
class IAssetTypeActions;
class INiagaraEditorTypeUtilities;
class UNiagaraSettings;
class USequencerSettings;
class UNiagaraStackViewModel;
class UNiagaraStackEntry;
class UNiagaraStackIssue;
class FNiagaraSystemViewModel;
class FNiagaraScriptMergeManager;
class FNiagaraCompileOptions;
class FNiagaraCompileRequestDataBase;
class UMovieSceneNiagaraParameterTrack;
struct IConsoleCommand;
class INiagaraEditorOnlyDataUtilities;
class FNiagaraEditorCommands;
struct FNiagaraScriptHighlight;
class FNiagaraClipboard;

DECLARE_STATS_GROUP(TEXT("Niagara Editor"), STATGROUP_NiagaraEditor, STATCAT_Advanced);

/* Defines methods for allowing external modules to supply widgets to the core editor module. */
class NIAGARAEDITOR_API INiagaraEditorWidgetProvider
{
public:
	virtual TSharedRef<SWidget> CreateStackView(UNiagaraStackViewModel& StackViewModel) const = 0;
	virtual TSharedRef<SWidget> CreateSystemOverview(TSharedRef<FNiagaraSystemViewModel> SystemViewModel) const = 0;
	virtual TSharedRef<SWidget> CreateStackIssueIcon(UNiagaraStackViewModel& StackViewModel, UNiagaraStackEntry& StackEntry) const = 0;
	virtual FLinearColor GetColorForExecutionCategory(FName ExecutionCategory) const = 0;
};

extern int32 GbShowFastPathOptions;

/** Niagara Editor module */
class FNiagaraEditorModule : public IModuleInterface,
	public IHasMenuExtensibility, public IHasToolBarExtensibility, public FGCObject
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(UMovieSceneNiagaraParameterTrack*, FOnCreateMovieSceneTrackForParameter, FNiagaraVariable);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCheckScriptToolkitsShouldFocusGraphElement, const FNiagaraScriptIDAndGraphFocusInfo*);

public:
	FNiagaraEditorModule();

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the instance of this module. */
	NIAGARAEDITOR_API static FNiagaraEditorModule& Get();

	/** Compile the specified script. */
	virtual TSharedPtr<FNiagaraVMExecutableData> CompileScript(const FNiagaraCompileRequestDataBase* InCompileRequest, const FNiagaraCompileOptions& InCompileOptions);

	TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> Precompile(UObject* Obj);

	/** Gets the extensibility managers for outside entities to extend static mesh editor's menus and toolbars */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override {return MenuExtensibilityManager;}
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override {return ToolBarExtensibilityManager;}

	/** Registers niagara editor type utilities for a specific type. */
	void RegisterTypeUtilities(FNiagaraTypeDefinition Type, TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> EditorUtilities);

	/** Register/unregister niagara editor settings. */
	void RegisterSettings();
	void UnregisterSettings();

	/** Gets Niagara editor type utilities for a specific type if there are any registered. */
	TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> NIAGARAEDITOR_API GetTypeUtilities(const FNiagaraTypeDefinition& Type);

	static EAssetTypeCategories::Type GetAssetCategory() { return NiagaraAssetCategory; }

	NIAGARAEDITOR_API void RegisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider);
	NIAGARAEDITOR_API void UnregisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider);

	TSharedRef<INiagaraEditorWidgetProvider> GetWidgetProvider() const;

	TSharedRef<FNiagaraScriptMergeManager> GetScriptMergeManager() const;

	void RegisterParameterTrackCreatorForType(const UScriptStruct& StructType, FOnCreateMovieSceneTrackForParameter CreateTrack);
	void UnregisterParameterTrackCreatorForType(const UScriptStruct& StructType);
	bool CanCreateParameterTrackForType(const UScriptStruct& StructType);
	UMovieSceneNiagaraParameterTrack* CreateParameterTrackForType(const UScriptStruct& StructType, FNiagaraVariable Parameter);

	/** Niagara Editor app identifier string */
	static const FName NiagaraEditorAppIdentifier;

	/** The tab color scale for niagara editors. */
	static const FLinearColor WorldCentricTabColorScale;

	/** Get the niagara UI commands. */
	NIAGARAEDITOR_API const class FNiagaraEditorCommands& Commands();

	FOnCheckScriptToolkitsShouldFocusGraphElement& GetOnScriptToolkitsShouldFocusGraphElement() { return OnCheckScriptToolkitsShouldFocusGraphElement; };

	NIAGARAEDITOR_API TSharedPtr<FNiagaraSystemViewModel> GetExistingViewModelForSystem(UNiagaraSystem* InSystem);

	NIAGARAEDITOR_API const FNiagaraEditorCommands& GetCommands() const;

	void InvalidateCachedScriptAssetData();

	const TArray<FNiagaraScriptHighlight>& GetCachedScriptAssetHighlights() const;

	void GetScriptAssetsMatchingHighlight(const FNiagaraScriptHighlight& InHighlight, TArray<FAssetData>& OutMatchingScriptAssets) const;

	FNiagaraClipboard& GetClipboard() const;

private:
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	void OnNiagaraSettingsChangedEvent(const FString& PropertyName, const UNiagaraSettings* Settings);
	void OnPreGarbageCollection();
	void OnExecParticleInvoked(const TCHAR* InStr);
	void OnPostEngineInit();

	/** FGCObject interface */
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	virtual FString GetReferencerName() const override
	{
		return "FNiagaraEditorModule";
	}

	void TestCompileScriptFromConsole(const TArray<FString>& Arguments);

private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;

	FCriticalSection TypeEditorsCS;
	TMap<FNiagaraTypeDefinition, TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe>> TypeToEditorUtilitiesMap;
	TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> EnumTypeUtilities;

	static EAssetTypeCategories::Type NiagaraAssetCategory;

	FDelegateHandle CreateEmitterTrackEditorHandle;
	FDelegateHandle CreateSystemTrackEditorHandle;

	FDelegateHandle CreateBoolParameterTrackEditorHandle;
	FDelegateHandle CreateFloatParameterTrackEditorHandle;
	FDelegateHandle CreateIntegerParameterTrackEditorHandle;
	FDelegateHandle CreateVectorParameterTrackEditorHandle;
	FDelegateHandle CreateColorParameterTrackEditorHandle;

	FDelegateHandle ScriptCompilerHandle;
	FDelegateHandle PrecompilerHandle;

	USequencerSettings* SequencerSettings;
	
	TSharedPtr<INiagaraEditorWidgetProvider> WidgetProvider;

	TSharedPtr<FNiagaraScriptMergeManager> ScriptMergeManager;

	TSharedPtr<INiagaraEditorOnlyDataUtilities> EditorOnlyDataUtilities;

	TMap<const UScriptStruct*, FOnCreateMovieSceneTrackForParameter> TypeToParameterTrackCreatorMap;

	IConsoleCommand* TestCompileScriptCommand;
	IConsoleCommand* DumpRapidIterationParametersForAsset;
	IConsoleCommand* PreventSystemRecompileCommand;
	IConsoleCommand* PreventAllSystemRecompilesCommand;
	IConsoleCommand* UpgradeAllNiagaraAssetsCommand;
	IConsoleCommand* DumpCompileIdDataForAssetCommand;

	FOnCheckScriptToolkitsShouldFocusGraphElement OnCheckScriptToolkitsShouldFocusGraphElement;

	mutable TOptional<TArray<FNiagaraScriptHighlight>> CachedScriptAssetHighlights;

	bool bThumbnailRenderersRegistered;

	TSharedRef<FNiagaraClipboard> Clipboard;
};
