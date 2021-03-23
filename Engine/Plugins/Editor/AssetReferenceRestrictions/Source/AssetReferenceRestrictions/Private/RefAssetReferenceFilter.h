// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Editor/UnrealEdEngine.h"

struct FAssetData;
struct FDomainDatabase;
struct FDomainData;

class FRefAssetReferenceFilter : public IAssetReferenceFilter
{
	enum class EReferencingLayer : uint8
	{
		Engine,
		Game,
		GameFeaturePlugin,
		Plugin,
		AllowAll
	};
public:
	FRefAssetReferenceFilter(const FAssetReferenceFilterContext& Context);
	virtual bool PassesFilter(const FAssetData& AssetData, FText* OutOptionalFailureReason = nullptr) const override;

private:
	bool HasMostRestrictiveFilter() const;
	void ProcessReferencingAsset(const FAssetData& ReferencingAsset, TArray<FAssetData>& OutDerivedReferencingAssets);

	/** Heruistic to find actual assets from preview assets (i.e. the material editor's preview material) */
	bool GetAssetDataFromPossiblyPreviewObject(const FAssetData& PossiblyPreviewObject, FAssetData& OriginalAsset) const;
	bool GetContentRootPathFromPackageName(const FString& PackageName, FString& OutContentRootPath) const;

private:
	const FString EnginePath;
	const FString GamePath;
	const FString TempPath;
	const FString ScriptPath;
	const FString ScriptEnginePath;
	const FString ScriptGamePath;
	const FName EngineTransientPackageName;
	TArray<FString> AllGameFeaturePluginPaths;
	TArray<FString> CrossPluginAllowedReferences;
	FString ReferencingAssetPluginPath;
	EReferencingLayer ReferencingAssetLayer;

	TSharedPtr<FDomainData> ReferencingDomain;

	FText Failure_RestrictedFolder;
	FText Failure_Engine;
	FText Failure_Game;
	FText Failure_GameFeaturePlugin;
	FText Failure_Plugin;
	bool bAllowAssetsInRestrictedFolders;
};
