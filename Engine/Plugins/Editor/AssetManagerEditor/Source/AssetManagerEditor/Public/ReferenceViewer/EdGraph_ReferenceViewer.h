// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AssetRegistry/AssetData.h"
#include "AssetManagerEditorModule.h"
#include "EdGraph/EdGraph.h"
#include "Misc/AssetRegistryInterface.h"
#include "EdGraph_ReferenceViewer.generated.h"

class FAssetThumbnailPool;
class UEdGraphNode_Reference;
class SReferenceViewer;
enum class EDependencyPinCategory;

/*
*  Holds asset information for building reference graph
*/ 
struct ReferenceNodeInfo
{
	FAssetIdentifier AssetId;

	FAssetData AssetData;

	// immediate children (references or dependencies)
	TArray<TPair<FAssetIdentifier, EDependencyPinCategory>> Children;

	// this node's parent references (how it got included)
	TArray<FAssetIdentifier> Parents;

	// Which direction.  Referencers are left (other assets that depend on me), Dependencies are right (other assets I depend on)
	bool bReferencers;

	int32 OverflowCount;

	ReferenceNodeInfo(const FAssetIdentifier& InAssetId, bool InbReferencers);

	bool IsFirstParent(const FAssetIdentifier& InParentId) const;

	bool IsADuplicate() const;

	// The Provision Size, or vertical spacing required for layout, for a given parent.  
	// At the time of writing, the intent is only the first node manifestation of 
	// an asset will have its children shown
	int32 ProvisionSize(const FAssetIdentifier& InParentId) const;

	// how many nodes worth of children require vertical spacing 
	int32 ChildProvisionSize;

};


UCLASS()
class ASSETMANAGEREDITOR_API UEdGraph_ReferenceViewer : public UEdGraph
{
	GENERATED_UCLASS_BODY()

public:
	// UObject implementation
	virtual void BeginDestroy() override;
	// End UObject implementation

	/** Set reference viewer to focus on these assets */
	void SetGraphRoot(const TArray<FAssetIdentifier>& GraphRootIdentifiers, const FIntPoint& GraphRootOrigin = FIntPoint(ForceInitToZero));

	/** Returns list of currently focused assets */
	const TArray<FAssetIdentifier>& GetCurrentGraphRootIdentifiers() const;

	/** If you're extending the reference viewer via GetAllGraphEditorContextMenuExtender you can use this to get the list of selected assets to use in your menu extender */
	bool GetSelectedAssetsForMenuExtender(const class UEdGraphNode* Node, TArray<FAssetIdentifier>& SelectedAssets) const;

	/** Accessor for the thumbnail pool in this graph */
	const TSharedPtr<class FAssetThumbnailPool>& GetAssetThumbnailPool() const;

	/** Force the graph to rebuild */
	class UEdGraphNode_Reference* RebuildGraph();

	bool IsSearchDepthLimited() const;
	bool IsSearchBreadthLimited() const;
	bool IsShowSoftReferences() const;
	bool IsShowHardReferences() const;
	bool IsShowFilteredPackagesOnly() const;
	bool IsShowEditorOnlyReferences() const;
	bool IsShowManagementReferences() const;
	bool IsShowSearchableNames() const;
	bool IsShowNativePackages() const;
	bool IsShowReferencers() const;
	bool IsShowDependencies() const;
	bool IsCompactMode() const;
	bool IsShowDuplicates() const;

	void SetSearchDepthLimitEnabled(bool newEnabled);
	void SetSearchBreadthLimitEnabled(bool newEnabled);
	void SetShowSoftReferencesEnabled(bool newEnabled);
	void SetShowHardReferencesEnabled(bool newEnabled);
	void SetShowFilteredPackagesOnlyEnabled(bool newEnabled);
	void SetShowEditorOnlyReferencesEnabled(bool newEnabled);
	void SetShowManagementReferencesEnabled(bool newEnabled);
	void SetShowSearchableNames(bool newEnabled);
	void SetShowNativePackages(bool newEnabled);
	void SetShowReferencers(const bool bShouldShowReferencers);
	void SetShowDependencies(const bool bShouldShowDependencies);
	void SetCompactModeEnabled(bool newEnabled);
	void SetShowDuplicatesEnabled(bool newEnabled);

	using FIsPackageNamePassingFilterCallback = TFunction<bool(FName)>;
	void SetIsPackageNamePassingFilterCallback(const TOptional<FIsPackageNamePassingFilterCallback>& InIsPackageNamePassingFilterCallback) { IsPackageNamePassingFilterCallback = InIsPackageNamePassingFilterCallback; }

	int32 GetSearchReferencerDepthLimit() const;
	void SetSearchReferencerDepthLimit(int32 NewDepthLimit);

	int32 GetSearchDependencyDepthLimit() const;
	void SetSearchDependencyDepthLimit(int32 NewDepthLimit);

	int32 GetSearchBreadthLimit() const;
	void SetSearchBreadthLimit(int32 NewBreadthLimit);

	FName GetCurrentCollectionFilter() const;
	void SetCurrentCollectionFilter(FName NewFilter);

	bool GetEnableCollectionFilter() const;
	void SetEnableCollectionFilter(bool bEnabled);

	/* Temporary variable that allows reverting to deprecated layout methods */
	bool GetUseNodeInfos() const { return bUseNodeInfos; }
	void SetUseNodeInfos(bool InbUseNodeInfos) { bUseNodeInfos= InbUseNodeInfos; }

private:
	void SetReferenceViewer(TSharedPtr<SReferenceViewer> InViewer);
	UEdGraphNode_Reference* ConstructNodes(const TArray<FAssetIdentifier>& GraphRootIdentifiers, const FIntPoint& GraphRootOrigin);
	// RecursivelyGatherSizes is now deprecated, use RecursivelyPopulateNodeInfos with RecursivelyCreateNodes
	int32 RecursivelyGatherSizes(bool bReferencers, const TArray<FAssetIdentifier>& Identifiers, const TSet<FName>& AllowedPackageNames, int32 CurrentDepth, int32 MaxDepth, TSet<FAssetIdentifier>& VisitedNames, TMap<FAssetIdentifier, int32>& OutNodeSizes) const;
	void GatherAssetData(const TSet<FName>& AllPackageNames, TMap<FName, FAssetData>& OutPackageToAssetDataMap) const;

	// RecursivelyConstructNodes is now deprecated, use RecursivelyPopulateNodeInfos with RecursivelyCreateNodes
	class UEdGraphNode_Reference* RecursivelyConstructNodes(bool bReferencers, UEdGraphNode_Reference* RootNode, const TArray<FAssetIdentifier>& Identifiers, const FIntPoint& NodeLoc, const TMap<FAssetIdentifier, int32>& NodeSizes, const TMap<FName, FAssetData>& PackagesToAssetDataMap, const TSet<FName>& AllowedPackageNames, int32 CurrentDepth, int32 MaxDepth, TSet<FAssetIdentifier>& VisitedNames);

	bool ExceedsMaxSearchDepth(int32 Depth, int32 MaxDepth) const;
	bool ExceedsMaxSearchBreadth(int32 Breadth) const;
	FAssetManagerDependencyQuery GetReferenceSearchFlags(bool bHardOnly) const;

	UEdGraphNode_Reference* CreateReferenceNode();

	/* Generates a NodeInfo structure then used to generate and layout the graph nodes */
	void RecursivelyPopulateNodeInfos(bool bReferencers, const FAssetIdentifier& AssetId, TMap<FAssetIdentifier, ReferenceNodeInfo>& NodeInfos, int32 CurrentDepth, int32 MaxDepth);

	/* Uses the NodeInfos map to generate and layout the graph nodes */
	UEdGraphNode_Reference* RecursivelyCreateNodes(
		bool bInReferencers, 
		const FAssetIdentifier& InAssetId, 
		const FIntPoint& InNodeLoc, 
		const FAssetIdentifier& InParentId, 
		UEdGraphNode_Reference* InParentNode, 
		TMap<FAssetIdentifier, ReferenceNodeInfo>& InNodeInfos, 
		int32 InCurrentDepth, 
		int32 InMaxDepth, 
		bool bIsRoot = false
	);

	/** Removes all nodes from the graph */
	void RemoveAllNodes();

	/** Returns true if filtering is enabled and we have a valid collection */
	bool ShouldFilterByCollection() const;

	void GetSortedLinks(const TArray<FAssetIdentifier>& Identifiers, bool bReferencers, const FAssetManagerDependencyQuery& Query, TMap<FAssetIdentifier, EDependencyPinCategory>& OutLinks) const;
	bool IsPackageIdentifierPassingFilter(const FAssetIdentifier& InAssetIdentifier) const;

private:
	/** Pool for maintaining and rendering thumbnails */
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;

	/** Editor for this pool */
	TWeakPtr<SReferenceViewer> ReferenceViewer;

	TArray<FAssetIdentifier> CurrentGraphRootIdentifiers;
	FIntPoint CurrentGraphRootOrigin;

	int32 MaxSearchReferencerDepth; // How deep to search references
	int32 MaxSearchDependencyDepth; // How deep to search dependanies
	int32 MaxSearchBreadth;

	/** Current collection filter. NAME_None for no filter */
	FName CurrentCollectionFilter;
	bool bEnableCollectionFilter;

	bool bLimitSearchDepth;
	bool bLimitSearchBreadth;
	bool bIsShowSoftReferences;
	bool bIsShowHardReferences;
	bool bIsShowEditorOnlyReferences;
	bool bIsShowManagementReferences;
	bool bIsShowSearchableNames;
	bool bIsShowNativePackages;
	/* Whether to display the Referencers */
	bool bIsShowReferencers;
	/* Whether to display the Dependencies */
	bool bIsShowDependencies;
	/* Whether to show duplicate asset references */
	bool bIsShowDuplicates;

	/* This is a convenience toggle to switch between the old & new methods for computing & displaying the graph */
	bool bUseNodeInfos;

	bool bIsShowFilteredPackagesOnly;
	TOptional<FIsPackageNamePassingFilterCallback> IsPackageNamePassingFilterCallback;

	/** List of packages the current collection filter allows */
	TSet<FName> CurrentCollectionPackages;

	bool bIsCompactMode;

	friend SReferenceViewer;
};
