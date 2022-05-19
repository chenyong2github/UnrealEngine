// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReferenceViewer/EdGraph_ReferenceViewer.h"
#include "ReferenceViewer/EdGraphNode_Reference.h"
#include "ReferenceViewer/ReferenceViewerSettings.h"
#include "EdGraph/EdGraphPin.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "SReferenceViewer.h"
#include "SReferenceNode.h"
#include "GraphEditor.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "AssetManagerEditorModule.h"
#include "Engine/AssetManager.h"
#include "Settings/EditorProjectSettings.h"

ReferenceNodeInfo::ReferenceNodeInfo(const FAssetIdentifier& InAssetId, bool InbReferencers)
	: AssetId(InAssetId)
	, bReferencers(InbReferencers)
	, OverflowCount(0)
	, ChildProvisionSize(0)
{}

bool ReferenceNodeInfo::IsFirstParent(const FAssetIdentifier& InParentId) const
{
	return Parents.IsEmpty() || Parents[0] == InParentId;
}

bool ReferenceNodeInfo::IsADuplicate() const
{
	return Parents.Num() > 1;
}

int32 ReferenceNodeInfo::ProvisionSize(const FAssetIdentifier& InParentId) const
{
	return IsFirstParent(InParentId) ? ChildProvisionSize : 1;
}

UEdGraph_ReferenceViewer::UEdGraph_ReferenceViewer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AssetThumbnailPool = MakeShareable( new FAssetThumbnailPool(1024) );

	Settings = GetMutableDefault<UReferenceViewerSettings>();

	bUseNodeInfos = true;
}

void UEdGraph_ReferenceViewer::BeginDestroy()
{
	AssetThumbnailPool.Reset();

	Super::BeginDestroy();
}

void UEdGraph_ReferenceViewer::SetGraphRoot(const TArray<FAssetIdentifier>& GraphRootIdentifiers, const FIntPoint& GraphRootOrigin)
{
	CurrentGraphRootIdentifiers = GraphRootIdentifiers;
	CurrentGraphRootOrigin = GraphRootOrigin;

	// If we're focused on a searchable name, enable that flag
	for (const FAssetIdentifier& AssetId : GraphRootIdentifiers)
	{
		if (AssetId.IsValue())
		{
			Settings->SetShowSearchableNames(true);
		}
		else if (AssetId.GetPrimaryAssetId().IsValid())
		{
			if (UAssetManager::IsValid())
			{
				UAssetManager::Get().UpdateManagementDatabase();
			}
			
			Settings->SetShowManagementReferencesEnabled(true);
		}
	}
}

const TArray<FAssetIdentifier>& UEdGraph_ReferenceViewer::GetCurrentGraphRootIdentifiers() const
{
	return CurrentGraphRootIdentifiers;
}

void UEdGraph_ReferenceViewer::SetReferenceViewer(TSharedPtr<SReferenceViewer> InViewer)
{
	ReferenceViewer = InViewer;
}

bool UEdGraph_ReferenceViewer::GetSelectedAssetsForMenuExtender(const class UEdGraphNode* Node, TArray<FAssetIdentifier>& SelectedAssets) const
{
	if (!ReferenceViewer.IsValid())
	{
		return false;
	}
	TSharedPtr<SGraphEditor> GraphEditor = ReferenceViewer.Pin()->GetGraphEditor();

	if (!GraphEditor.IsValid())
	{
		return false;
	}

	TSet<UObject*> SelectedNodes = GraphEditor->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_Reference* ReferenceNode = Cast<UEdGraphNode_Reference>(*It))
		{
			if (!ReferenceNode->IsCollapsed())
			{
				SelectedAssets.Add(ReferenceNode->GetIdentifier());
			}
		}
	}
	return true;
}

UEdGraphNode_Reference* UEdGraph_ReferenceViewer::RebuildGraph()
{
	RemoveAllNodes();
	UEdGraphNode_Reference* NewRootNode = ConstructNodes(CurrentGraphRootIdentifiers, CurrentGraphRootOrigin);
	NotifyGraphChanged();

	return NewRootNode;
}

FName UEdGraph_ReferenceViewer::GetCurrentCollectionFilter() const
{
	return CurrentCollectionFilter;
}

void UEdGraph_ReferenceViewer::SetCurrentCollectionFilter(FName NewFilter)
{
	CurrentCollectionFilter = NewFilter;
}

FAssetManagerDependencyQuery UEdGraph_ReferenceViewer::GetReferenceSearchFlags(bool bHardOnly) const
{
	using namespace UE::AssetRegistry;
	FAssetManagerDependencyQuery Query;
	Query.Categories = EDependencyCategory::None;
	Query.Flags = EDependencyQuery::NoRequirements;

	bool bLocalIsShowSoftReferences = Settings->IsShowSoftReferences() && !bHardOnly;
	if (bLocalIsShowSoftReferences || Settings->IsShowHardReferences())
	{
		Query.Categories |= EDependencyCategory::Package;
		Query.Flags |= bLocalIsShowSoftReferences ? EDependencyQuery::NoRequirements : EDependencyQuery::Hard;
		Query.Flags |= Settings->IsShowHardReferences() ? EDependencyQuery::NoRequirements : EDependencyQuery::Soft;
		Query.Flags |= Settings->IsShowEditorOnlyReferences() ? EDependencyQuery::NoRequirements : EDependencyQuery::Game;
	}
	if (Settings->IsShowSearchableNames() && !bHardOnly)
	{
		Query.Categories |= EDependencyCategory::SearchableName;
	}
	if (Settings->IsShowManagementReferences())
	{
		Query.Categories |= EDependencyCategory::Manage;
		Query.Flags |= bHardOnly ? EDependencyQuery::Direct : EDependencyQuery::NoRequirements;
	}

	return Query;
}

UEdGraphNode_Reference* UEdGraph_ReferenceViewer::ConstructNodes(const TArray<FAssetIdentifier>& GraphRootIdentifiers, const FIntPoint& GraphRootOrigin )
{
	UEdGraphNode_Reference* RootNode = NULL;

	if (GraphRootIdentifiers.Num() > 0)
	{
		// It both were false, nothing (other than the GraphRootIdentifiers) would be displayed
		check(Settings->IsShowReferencers() || Settings->IsShowDependencies());

		// Refresh the current collection filter
		CurrentCollectionPackages.Empty();
		if (ShouldFilterByCollection())
		{
			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
			TArray<FName> AssetPaths;
			CollectionManagerModule.Get().GetAssetsInCollection(CurrentCollectionFilter, ECollectionShareType::CST_All, AssetPaths);
			CurrentCollectionPackages.Reserve(AssetPaths.Num());
			for (FName AssetPath : AssetPaths)
			{
				CurrentCollectionPackages.Add(FName(*FPackageName::ObjectPathToPackageName(AssetPath.ToString())));
			}
		}

		// Create & Populate the NodeInfo Maps 
		// Note to add an empty parent to the root so that if the root node again gets found again as a duplicate, that next parent won't be 
		// identified as the primary root and also it will appear as having multiple parents.
		TMap<FAssetIdentifier, ReferenceNodeInfo> ReferenceNodeInfos;
		ReferenceNodeInfo& RootNodeInfo = ReferenceNodeInfos.FindOrAdd( GraphRootIdentifiers[0], ReferenceNodeInfo(GraphRootIdentifiers[0], true));
		RootNodeInfo.Parents.Emplace(FAssetIdentifier(NAME_None));
		RecursivelyPopulateNodeInfos(true, GraphRootIdentifiers[0], ReferenceNodeInfos, 0, Settings->GetSearchReferencerDepthLimit());

		TMap<FAssetIdentifier, ReferenceNodeInfo> DependencyNodeInfos;
		ReferenceNodeInfo& DRootNodeInfo = DependencyNodeInfos.FindOrAdd( GraphRootIdentifiers[0], ReferenceNodeInfo(GraphRootIdentifiers[0], false));
		DRootNodeInfo.Parents.Emplace(FAssetIdentifier(NAME_None));
		RecursivelyPopulateNodeInfos(false, GraphRootIdentifiers[0], DependencyNodeInfos, 0, Settings->GetSearchDependencyDepthLimit());

		TMap<FAssetIdentifier, int32> ReferencerNodeSizes;
		TSet<FAssetIdentifier> VisitedReferencerSizeNames;
		if (Settings->IsShowReferencers())
		{
			const int32 ReferencerDepth = 1;
			RecursivelyGatherSizes(/*bReferencers=*/true, GraphRootIdentifiers, CurrentCollectionPackages, ReferencerDepth, Settings->GetSearchReferencerDepthLimit(), VisitedReferencerSizeNames, ReferencerNodeSizes);
		}

		TMap<FAssetIdentifier, int32> DependencyNodeSizes;
		TSet<FAssetIdentifier> VisitedDependencySizeNames;
		if (Settings->IsShowDependencies())
		{
			const int32 DependencyDepth = 1;
			RecursivelyGatherSizes(/*bReferencers=*/false, GraphRootIdentifiers, CurrentCollectionPackages, DependencyDepth, Settings->GetSearchDependencyDepthLimit(), VisitedDependencySizeNames, DependencyNodeSizes);
		}

		TSet<FName> AllPackageNames;
		auto AddPackage = [](const FAssetIdentifier& AssetId, TSet<FName>& PackageNames)
		{ 
			// Only look for asset data if this is a package
			if (!AssetId.IsValue() && !AssetId.PackageName.IsNone())
			{
				PackageNames.Add(AssetId.PackageName);
			}
		};

		if (Settings->IsShowReferencers())
		{
			for (const FAssetIdentifier& AssetId : VisitedReferencerSizeNames)
			{
				AddPackage(AssetId, AllPackageNames);
			}
		}

		if (Settings->IsShowDependencies())
		{
			for (const FAssetIdentifier& AssetId : VisitedDependencySizeNames)
			{
				AddPackage(AssetId, AllPackageNames);
			}
		}

		TMap<FName, FAssetData> PackagesToAssetDataMap;
		GatherAssetData(AllPackageNames, PackagesToAssetDataMap);

		// Store the AssetData in the NodeInfos
		for (TPair<FAssetIdentifier, ReferenceNodeInfo>&  InfoPair : ReferenceNodeInfos)
		{
			InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
		}

		for (TPair<FAssetIdentifier, ReferenceNodeInfo>&  InfoPair : DependencyNodeInfos)
		{
			InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
		}

		// Create the root node
		RootNode = CreateReferenceNode();
		bool bRootIsDuplicated = DependencyNodeInfos[GraphRootIdentifiers[0]].IsADuplicate() || ReferenceNodeInfos[GraphRootIdentifiers[0]].IsADuplicate();
		RootNode->SetupReferenceNode(GraphRootOrigin, GraphRootIdentifiers, PackagesToAssetDataMap.FindRef(GraphRootIdentifiers[0].PackageName), /*bInAllowThumbnail = */ !Settings->IsCompactMode(), /*bIsDuplicate*/ bRootIsDuplicated);
		RootNode->SetMakeCommentBubbleVisible(Settings->IsShowPath());

		if (bUseNodeInfos) // @LULU true = using new stuff, false = Not using new stuff 
		{
			if (Settings->IsShowReferencers())
			{
				RecursivelyCreateNodes(true, GraphRootIdentifiers[0], GraphRootOrigin, GraphRootIdentifiers[0], RootNode, ReferenceNodeInfos, 0, Settings->GetSearchReferencerDepthLimit(), /*bIsRoot*/ true);
			}

			if (Settings->IsShowDependencies())
			{
				RecursivelyCreateNodes(false, GraphRootIdentifiers[0], GraphRootOrigin, GraphRootIdentifiers[0], RootNode, DependencyNodeInfos, 0, Settings->GetSearchDependencyDepthLimit(), /*bIsRoot*/ true);
			}
		}
		else {

			if (Settings->IsShowReferencers())
			{
				TSet<FAssetIdentifier> VisitedReferencerNames;
				const int32 VisitedReferencerDepth = 1;
				RecursivelyConstructNodes(/*bReferencers=*/true, RootNode, GraphRootIdentifiers, GraphRootOrigin, ReferencerNodeSizes, PackagesToAssetDataMap, CurrentCollectionPackages, VisitedReferencerDepth, Settings->GetSearchReferencerDepthLimit(), VisitedReferencerNames);
			}

			if (Settings->IsShowDependencies())
			{
				TSet<FAssetIdentifier> VisitedDependencyNames;
				const int32 VisitedDependencyDepth = 1;
				RecursivelyConstructNodes(/*bReferencers=*/false, RootNode, GraphRootIdentifiers, GraphRootOrigin, DependencyNodeSizes, PackagesToAssetDataMap, CurrentCollectionPackages, VisitedDependencyDepth, Settings->GetSearchDependencyDepthLimit(), VisitedDependencyNames);
			}
		}
	}

	return RootNode;
}

void UEdGraph_ReferenceViewer::GetSortedLinks(const TArray<FAssetIdentifier>& Identifiers, bool bReferencers, const FAssetManagerDependencyQuery& Query, TMap<FAssetIdentifier, EDependencyPinCategory>& OutLinks) const
{
	using namespace UE::AssetRegistry;
	auto CategoryOrder = [](EDependencyCategory InCategory)
	{
		switch (InCategory)
		{
		case EDependencyCategory::Package: return 0;
		case EDependencyCategory::Manage: return 1;
		case EDependencyCategory::SearchableName: return 2;
		default: check(false);  return 3;
		}
	};
	auto IsHard = [](EDependencyProperty Properties)
	{
		return static_cast<bool>(((Properties & EDependencyProperty::Hard) != EDependencyProperty::None) | ((Properties & EDependencyProperty::Direct) != EDependencyProperty::None));
	};

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetDependency> LinksToAsset;
	for (const FAssetIdentifier& AssetId : Identifiers)
	{
		LinksToAsset.Reset();
		if (bReferencers)
		{
			AssetRegistry.GetReferencers(AssetId, LinksToAsset, Query.Categories, Query.Flags);
		}
		else
		{
			AssetRegistry.GetDependencies(AssetId, LinksToAsset, Query.Categories, Query.Flags);
		}

		// Sort the links from most important kind of link to least important kind of link, so that if we can't display them all in an ExceedsMaxSearchBreadth test, we
		// show the most important links.
		Algo::Sort(LinksToAsset, [&CategoryOrder, &IsHard](const FAssetDependency& A, const FAssetDependency& B)
			{
				if (A.Category != B.Category)
				{
					return CategoryOrder(A.Category) < CategoryOrder(B.Category);
				}
				if (A.Properties != B.Properties)
				{
					bool bAIsHard = IsHard(A.Properties);
					bool bBIsHard = IsHard(B.Properties);
					if (bAIsHard != bBIsHard)
					{
						return bAIsHard;
					}
				}
				return A.AssetId.PackageName.LexicalLess(B.AssetId.PackageName);
			});
		for (FAssetDependency LinkToAsset : LinksToAsset)
		{
			EDependencyPinCategory& Category = OutLinks.FindOrAdd(LinkToAsset.AssetId, EDependencyPinCategory::LinkEndActive);
			bool bIsHard = IsHard(LinkToAsset.Properties);
			bool bIsUsedInGame = (LinkToAsset.Category != EDependencyCategory::Package) | ((LinkToAsset.Properties & EDependencyProperty::Game) != EDependencyProperty::None);
			Category |= EDependencyPinCategory::LinkEndActive;
			Category |= bIsHard ? EDependencyPinCategory::LinkTypeHard : EDependencyPinCategory::LinkTypeNone;
			Category |= bIsUsedInGame ? EDependencyPinCategory::LinkTypeUsedInGame : EDependencyPinCategory::LinkTypeNone;
		}
	}

	// Check filters and Filter for our registry source
	TArray<FAssetIdentifier> ReferenceIds;
	OutLinks.GenerateKeyArray(ReferenceIds);
	IAssetManagerEditorModule::Get().FilterAssetIdentifiersForCurrentRegistrySource(ReferenceIds, GetReferenceSearchFlags(false), !bReferencers);

	for (TMap<FAssetIdentifier, EDependencyPinCategory>::TIterator It(OutLinks); It; ++It)
	{
		if (!IsPackageIdentifierPassingFilter(It.Key()))
		{
			It.RemoveCurrent();
		}

		else if (!ReferenceIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}

		// Collection Filter
		else if (ShouldFilterByCollection() && It.Key().IsPackage() && !CurrentCollectionPackages.Contains(It.Key().PackageName))
		{
			It.RemoveCurrent();
		}
	}
}

bool UEdGraph_ReferenceViewer::IsPackageIdentifierPassingFilter(const FAssetIdentifier& InAssetIdentifier) const
{
	if (!InAssetIdentifier.IsValue())
	{
		if (!Settings->IsShowNativePackages() && InAssetIdentifier.PackageName.ToString().StartsWith(TEXT("/Script")))
		{
			return false;
		}

		if (Settings->IsShowFilteredPackagesOnly() && IsPackageNamePassingFilterCallback.IsSet() && !(*IsPackageNamePassingFilterCallback)(InAssetIdentifier.PackageName))
		{
			return false;
		}
	}

	return true;
}

void
UEdGraph_ReferenceViewer::RecursivelyPopulateNodeInfos(bool bInReferencers, const FAssetIdentifier& InAssetId, TMap<FAssetIdentifier, ReferenceNodeInfo>& InNodeInfos, int32 InCurrentDepth, int32 InMaxDepth)
{
	int32 ProvisionSize = 0;

	int32 Breadth = 0;
	if (InMaxDepth > 0 && InCurrentDepth < InMaxDepth)
	{
		TArray<FAssetIdentifier> AssetIds;
		AssetIds.Add(InAssetId);

		TMap<FAssetIdentifier, EDependencyPinCategory> ReferenceLinks;
		GetSortedLinks(AssetIds, bInReferencers, GetReferenceSearchFlags(false), ReferenceLinks);

		InNodeInfos[InAssetId].Children.Reserve(ReferenceLinks.Num());
		for (const TPair<FAssetIdentifier, EDependencyPinCategory>& Pair : ReferenceLinks)
		{
			FAssetIdentifier ChildId = Pair.Key;
			if (!ExceedsMaxSearchBreadth(Breadth))
			{
				// Only Gather Children the first time
				if (!InNodeInfos.Contains(ChildId))
				{
					ReferenceNodeInfo& NewNodeInfo = InNodeInfos.FindOrAdd(ChildId, ReferenceNodeInfo(ChildId, bInReferencers));
					InNodeInfos[ChildId].Parents.Emplace(InAssetId);
					InNodeInfos[InAssetId].Children.Emplace(Pair);

					RecursivelyPopulateNodeInfos(bInReferencers, ChildId, InNodeInfos, InCurrentDepth + 1, InMaxDepth);
					ProvisionSize += InNodeInfos[ChildId].ProvisionSize(InAssetId);
					Breadth++;
				}

				else if (Settings->IsShowDuplicates() && !InNodeInfos[ChildId].Parents.Contains(InAssetId))
				{
					InNodeInfos[ChildId].Parents.Emplace(InAssetId);
					InNodeInfos[InAssetId].Children.Emplace(Pair);
					ProvisionSize += 1;
					Breadth++;
				}
			}
			else
			{
				// Count the overflow nodes to report in the UI but otherwise skip adding them 
				if (Settings->IsShowDuplicates() || !InNodeInfos.Contains(ChildId))
				{
					InNodeInfos[InAssetId].OverflowCount++;
					Breadth++;
				}
			}

		}
	}

	// Account for an overflow node if necessary
	if (InNodeInfos[InAssetId].OverflowCount > 0)
	{
		ProvisionSize++;
	}

	InNodeInfos[InAssetId].ChildProvisionSize = ProvisionSize > 0 ? ProvisionSize : 1;
}

int32 UEdGraph_ReferenceViewer::RecursivelyGatherSizes(bool bReferencers, const TArray<FAssetIdentifier>& Identifiers, const TSet<FName>& AllowedPackageNames, int32 CurrentDepth, int32 MaxDepth, TSet<FAssetIdentifier>& VisitedNames, TMap<FAssetIdentifier, int32>& OutNodeSizes) const
{
	check(Identifiers.Num() > 0);

	VisitedNames.Append(Identifiers);

	TMap<FAssetIdentifier, EDependencyPinCategory> ReferenceLinks;
	GetSortedLinks(Identifiers, bReferencers, GetReferenceSearchFlags(false), ReferenceLinks);

	int32 NodeSize = 0;

	if ( ReferenceLinks.Num() > 0 && !ExceedsMaxSearchDepth(CurrentDepth, MaxDepth) )
	{
		int32 NumReferencesMade = 0;
		int32 NumReferencesExceedingMax = 0;

		for (const TPair<FAssetIdentifier, EDependencyPinCategory>& Pair : ReferenceLinks)
		{
			const FAssetIdentifier& AssetId = Pair.Key;
			if ( !VisitedNames.Contains(AssetId) )
			{
				if ( !ExceedsMaxSearchBreadth(NumReferencesMade) )
				{
					TArray<FAssetIdentifier> NewPackageNames;
					NewPackageNames.Add(AssetId);
					NodeSize += RecursivelyGatherSizes(bReferencers, NewPackageNames, AllowedPackageNames, CurrentDepth + 1, MaxDepth, VisitedNames, OutNodeSizes);
					NumReferencesMade++;
				}
				else
				{
					NumReferencesExceedingMax++;
				}
			}
		}

		if ( NumReferencesExceedingMax > 0 )
		{
			// Add one size for the collapsed node
			NodeSize++;
		}
	}

	if ( NodeSize == 0 )
	{
		// If you have no valid children, the node size is just 1 (counting only self to make a straight line)
		NodeSize = 1;
	}

	OutNodeSizes.Add(Identifiers[0], NodeSize);
	return NodeSize;
}

void UEdGraph_ReferenceViewer::GatherAssetData(const TSet<FName>& AllPackageNames, TMap<FName, FAssetData>& OutPackageToAssetDataMap) const
{
	UE::AssetRegistry::GetAssetForPackages(AllPackageNames.Array(), OutPackageToAssetDataMap);
}

UEdGraphNode_Reference* UEdGraph_ReferenceViewer::RecursivelyCreateNodes(bool bInReferencers, const FAssetIdentifier& InAssetId, const FIntPoint& InNodeLoc, const FAssetIdentifier& InParentId, UEdGraphNode_Reference* InParentNode, TMap<FAssetIdentifier, ReferenceNodeInfo>& InNodeInfos, int32 InCurrentDepth, int32 InMaxDepth, bool bIsRoot)
{
	check(InNodeInfos.Contains(InAssetId));

	const ReferenceNodeInfo& NodeInfo = InNodeInfos[InAssetId];
	int32 NodeProvSize = 1;

	UEdGraphNode_Reference* NewNode = NULL;
	if (bIsRoot)
	{
		NewNode = InParentNode;
		NodeProvSize = NodeInfo.ProvisionSize(FAssetIdentifier(NAME_None));
	}
	else
	{
		NewNode = CreateReferenceNode();
		NewNode->SetupReferenceNode(InNodeLoc, {InAssetId}, NodeInfo.AssetData, /*bInAllowThumbnail*/ !Settings->IsCompactMode(), /*bIsADuplicate*/ NodeInfo.Parents.Num() > 1);
		NewNode->SetMakeCommentBubbleVisible(Settings->IsShowPath());
		NodeProvSize = NodeInfo.ProvisionSize(InParentId);
	}

	bool bIsFirstOccurance = bIsRoot || NodeInfo.IsFirstParent(InParentId);
	FIntPoint ChildLoc = InNodeLoc;
	if (InMaxDepth > 0 && (InCurrentDepth < InMaxDepth) && bIsFirstOccurance) // Only expand the first parent
	{

		// position the children nodes
		const int32 ColumnWidth = Settings->IsCompactMode() ? 500 : 800;
		ChildLoc.X += bInReferencers ? -ColumnWidth : ColumnWidth;

		int32 NodeSizeY = Settings->IsCompactMode() ? 100 : 200;
		NodeSizeY += Settings->IsShowPath() ? 40 : 0;

		ChildLoc.Y -= (NodeProvSize - 1) * NodeSizeY * 0.5 ;

		int32 Breadth = 0;

		for (const TPair<FAssetIdentifier, EDependencyPinCategory>& Pair : InNodeInfos[InAssetId].Children)
		{

			    FAssetIdentifier ChildId = Pair.Key;
			    int32 ChildProvSize = InNodeInfos[ChildId].ProvisionSize(InAssetId);

				ChildLoc.Y += (ChildProvSize - 1) * NodeSizeY * 0.5;

				UEdGraphNode_Reference* ChildNode = RecursivelyCreateNodes(bInReferencers, ChildId, ChildLoc, InAssetId, NewNode, InNodeInfos, InCurrentDepth + 1, InMaxDepth);	

				if (bInReferencers)
				{
					ChildNode->GetDependencyPin()->PinType.PinCategory = ::GetName(Pair.Value);
					NewNode->AddReferencer( ChildNode );
				}
				else
				{
					ChildNode->GetReferencerPin()->PinType.PinCategory = ::GetName(Pair.Value);
					ChildNode->AddReferencer( NewNode );
				}

				ChildLoc.Y += NodeSizeY * (ChildProvSize + 1) * 0.5;

		}

		// There were more references than allowed to be displayed. Make a collapsed node.
		if (NodeInfo.OverflowCount > 0)
		{
			UEdGraphNode_Reference* OverflowNode = CreateReferenceNode();
			FIntPoint RefNodeLoc;
			RefNodeLoc.X = ChildLoc.X;
			RefNodeLoc.Y = ChildLoc.Y;

			if ( ensure(OverflowNode) )
			{
				OverflowNode->SetAllowThumbnail(!Settings->IsCompactMode());
				OverflowNode->SetReferenceNodeCollapsed(RefNodeLoc, NodeInfo.OverflowCount);

				if ( bInReferencers )
				{
					NewNode->AddReferencer( OverflowNode );
				}
				else
				{
					OverflowNode->AddReferencer( NewNode );
				}
			}
		}
	}

	return NewNode;
}

UEdGraphNode_Reference* UEdGraph_ReferenceViewer::RecursivelyConstructNodes(bool bReferencers, UEdGraphNode_Reference* RootNode, const TArray<FAssetIdentifier>& Identifiers, const FIntPoint& NodeLoc, const TMap<FAssetIdentifier, int32>& NodeSizes, const TMap<FName, FAssetData>& PackagesToAssetDataMap, const TSet<FName>& AllowedPackageNames, int32 CurrentDepth, int32 MaxDepth, TSet<FAssetIdentifier>& VisitedNames)
{
	check(Identifiers.Num() > 0);

	VisitedNames.Append(Identifiers);

	UEdGraphNode_Reference* NewNode = NULL;
	if ( RootNode->GetIdentifier() == Identifiers[0] )
	{
		// Don't create the root node. It is already created!
		NewNode = RootNode;
	}
	else
	{
		NewNode = CreateReferenceNode();
		NewNode->SetupReferenceNode(NodeLoc, Identifiers, PackagesToAssetDataMap.FindRef(Identifiers[0].PackageName), /*bInAllowThumbnail = */ !Settings->IsCompactMode(), /*bIsDuplicate*/ false);
	}

	TMap<FAssetIdentifier, EDependencyPinCategory> Referencers;
	GetSortedLinks(Identifiers, bReferencers, GetReferenceSearchFlags(false), Referencers);

	if (Referencers.Num() > 0 && !ExceedsMaxSearchDepth(CurrentDepth, MaxDepth))
	{
		FIntPoint ReferenceNodeLoc = NodeLoc;

		if (bReferencers)
		{
			// Referencers go left
			ReferenceNodeLoc.X -= Settings->IsCompactMode() ? 400 : 800;
		}
		else
		{
			// Dependencies go right
			ReferenceNodeLoc.X += Settings->IsCompactMode() ? 400 : 800;
		}

		const int32 NodeSizeY = Settings->IsCompactMode() ? 100 : 200;
		const int32 TotalReferenceSizeY = NodeSizes.FindChecked(Identifiers[0]) * NodeSizeY;

		ReferenceNodeLoc.Y -= TotalReferenceSizeY * 0.5f;
		ReferenceNodeLoc.Y += NodeSizeY * 0.5f;

		int32 NumReferencesMade = 0;
		int32 NumReferencesExceedingMax = 0;

		for ( TPair<FAssetIdentifier, EDependencyPinCategory>& DependencyPair : Referencers)
		{
			FAssetIdentifier ReferenceName = DependencyPair.Key;

			if ( !VisitedNames.Contains(ReferenceName) )
			{
				if ( !ExceedsMaxSearchBreadth(NumReferencesMade) )
				{
					int32 ThisNodeSizeY = ReferenceName.IsValue() ? 100 : NodeSizeY;

					const int32 RefSizeY = NodeSizes.FindChecked(ReferenceName);
					FIntPoint RefNodeLoc;
					RefNodeLoc.X = ReferenceNodeLoc.X;
					RefNodeLoc.Y = ReferenceNodeLoc.Y + RefSizeY * ThisNodeSizeY * 0.5 - ThisNodeSizeY * 0.5;
					
					TArray<FAssetIdentifier> NewIdentifiers;
					NewIdentifiers.Add(ReferenceName);
					
					UEdGraphNode_Reference* ReferenceNode = RecursivelyConstructNodes(bReferencers, RootNode, NewIdentifiers, RefNodeLoc, NodeSizes, PackagesToAssetDataMap, AllowedPackageNames, CurrentDepth + 1, MaxDepth, VisitedNames);
					if ( ensure(ReferenceNode) )
					{
						if (bReferencers)
						{
							ReferenceNode->GetDependencyPin()->PinType.PinCategory = ::GetName(DependencyPair.Value);
						}
						else
						{
							ReferenceNode->GetReferencerPin()->PinType.PinCategory = ::GetName(DependencyPair.Value);
						}

						if ( bReferencers )
						{
							NewNode->AddReferencer( ReferenceNode );
						}
						else
						{
							ReferenceNode->AddReferencer( NewNode );
						}

						ReferenceNodeLoc.Y += RefSizeY * ThisNodeSizeY;
					}

					NumReferencesMade++;
				}
				else
				{
					NumReferencesExceedingMax++;
				}
			}
		}

		if ( NumReferencesExceedingMax > 0 )
		{
			// There are more references than allowed to be displayed. Make a collapsed node.
			UEdGraphNode_Reference* ReferenceNode = CreateReferenceNode();
			FIntPoint RefNodeLoc;
			RefNodeLoc.X = ReferenceNodeLoc.X;
			RefNodeLoc.Y = ReferenceNodeLoc.Y;

			if ( ensure(ReferenceNode) )
			{
				ReferenceNode->SetAllowThumbnail(!Settings->IsCompactMode());
				ReferenceNode->SetReferenceNodeCollapsed(RefNodeLoc, NumReferencesExceedingMax);

				if ( bReferencers )
				{
					NewNode->AddReferencer( ReferenceNode );
				}
				else
				{
					ReferenceNode->AddReferencer( NewNode );
				}
			}
		}
	}

	return NewNode;
}

const TSharedPtr<FAssetThumbnailPool>& UEdGraph_ReferenceViewer::GetAssetThumbnailPool() const
{
	return AssetThumbnailPool;
}

bool UEdGraph_ReferenceViewer::ExceedsMaxSearchDepth(int32 Depth, int32 MaxDepth) const
{
	// ExceedsMaxSearchDepth requires only greater (not equal than) because, even though the Depth is 1-based indexed (similarly to Breadth), the first index (index 0) corresponds to the root object 
	return Settings->IsSearchDepthLimited() && Depth > MaxDepth;
}

bool UEdGraph_ReferenceViewer::ExceedsMaxSearchBreadth(int32 Breadth) const
{
	// ExceedsMaxSearchBreadth requires greater or equal than because the Breadth is 1-based indexed
	return Settings->IsSearchBreadthLimited() && (Breadth >=  Settings->GetSearchBreadthLimit());
}

UEdGraphNode_Reference* UEdGraph_ReferenceViewer::CreateReferenceNode()
{
	const bool bSelectNewNode = false;
	return Cast<UEdGraphNode_Reference>(CreateNode(UEdGraphNode_Reference::StaticClass(), bSelectNewNode));
}

void UEdGraph_ReferenceViewer::RemoveAllNodes()
{
	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (int32 NodeIndex = 0; NodeIndex < NodesToRemove.Num(); ++NodeIndex)
	{
		RemoveNode(NodesToRemove[NodeIndex]);
	}
}

bool UEdGraph_ReferenceViewer::ShouldFilterByCollection() const
{
	return Settings->GetEnableCollectionFilter() && CurrentCollectionFilter != NAME_None;
}
