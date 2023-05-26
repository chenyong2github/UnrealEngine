// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Tasks/Task.h"
#include "AssetRegistry/AssetIdentifier.h"
#include "Styling/SlateTypes.h"

//////////////////////////////////////////////////////////////////////////
// SPluginAuditBrowser

class IPlugin;
struct FGameFeaturePlugin;
class UGameplayTagsManager;
struct FGameplayTag;
class IMessageLogListing;
class IMessageToken;
class STableViewBase;
class ITableRow;
class FTokenizedMessage;

class SPluginAuditBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPluginAuditBrowser)
	{}

	SLATE_END_ARGS()
public:
	void Construct(const FArguments& InArgs);

private:
	void CreateLogListing();
	void BuildPluginList();
	void RefreshToolBar();

	void OnGlobalCheckboxStateChanged(ECheckBoxState State);

	class FCookedPlugin
	{
	public:
		FCookedPlugin(const TSharedRef<IPlugin>& InPlugin)
			: Plugin(InPlugin)
		{}
		virtual ~FCookedPlugin() = default;

		TSharedRef<IPlugin> Plugin;
		bool bSimulateDisabled = false;
	};

	TSharedRef<ITableRow> MakeCookedPluginRow(TSharedRef<FCookedPlugin> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	struct FGameFeaturePlugin
	{
		TSharedPtr<IPlugin> Plugin;
		TArray<FString> ModuleNames;
		TArray<FString> ScriptPackages;
		FName ContentRoot;
	};

	enum class EDoesPluginDependOnGameplayTagSource
	{
		Yes,
		No,
		UnknownTag
	};

	void RefreshViolations();

	static TArray<TSharedRef<FTokenizedMessage>> ScanForViolations(TArray<TSharedRef<IPlugin>> InIncludedGameFeaturePlugins, TArray<TSharedRef<IPlugin>> InExcludedGameFeaturePlugins);
	static TArray<TSharedPtr<IPlugin>> GetTagSourcePlugins(const UGameplayTagsManager& Manager, FName TagName);
	static EDoesPluginDependOnGameplayTagSource DoesPluginDependOnGameplayTagSource(const UGameplayTagsManager& Manager, const TSharedPtr<IPlugin>& DependentPlugin, FName TagName, TArray<TSharedPtr<IPlugin>>& OutPossibleSources);
	static bool IsTagOnlyAvailableFromExcludedSources(const UGameplayTagsManager& Manager, const FGameplayTag& Tag, const TArray<FGameFeaturePlugin>& ExcludedPlugins);
	static void GetGameFeaturePlugins(const TArray<TSharedRef<IPlugin>>& Plugins, TArray<FGameFeaturePlugin>& GameFeaturePlugins);

private:
	TArray<TSharedRef<IPlugin>> IncludedGameFeaturePlugins;
	TArray<TSharedRef<IPlugin>> ExcludedGameFeaturePlugins;

	TArray<TSharedRef<FCookedPlugin>> CookedPlugins;
	TSharedPtr<IMessageLogListing> LogListing;
};
