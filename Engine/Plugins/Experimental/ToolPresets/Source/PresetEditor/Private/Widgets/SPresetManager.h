// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Misc/ITransaction.h"

class FTransaction;
class UPresetUserSettings;
class SPositiveActionButton;
class SNegativeActionButton;
class SSplitter;
class SVerticalBox;
class SEditableTextBox;
template<class ItemType> class STreeView;
template<class ItemType> class SListView;
class UInteractiveToolsPresetCollectionAsset;
class FUICommandList;

/**
 * Implements the preset manager panel.
 */
class SPresetManager
	: public SCompoundWidget
{

	struct FPresetCollectionInfo
	{
		FSoftObjectPath PresetCollectionPath;
		bool bCollectionEnabled;

		FPresetCollectionInfo(FSoftObjectPath InPresetCollectionPath, bool bInEnabled)
			: PresetCollectionPath(InPresetCollectionPath), bCollectionEnabled(bInEnabled)
		{}
	};

public:

	struct FPresetViewEntry
	{
		enum class EEntryType : uint8
		{
			Collection,
			Tool,
			Preset
		};
		
		EEntryType EntryType;

		// Used for Collections/Tool Entries
		bool bEnabled = false;
		FSoftObjectPath CollectionPath;
		bool bIsDefaultCollection = false;
		bool bIsRenaming = false;
		FText EntryLabel;
		FSlateBrush EntryIcon;
		int32 Count = 0;

		// Used for Preset/Tool entries
		FString ToolName = "";
		int32 PresetIndex = 0;
		FString PresetLabel = "";
		FString PresetTooltip = "";

		TSharedPtr< FPresetViewEntry> Parent;
		TArray<TSharedPtr< FPresetViewEntry> > Children;

		// Collection Constructor
		FPresetViewEntry(bool bEnabledIn, FSoftObjectPath CollectionPathIn, FText EntryLabelIn, int32 CountIn)
			: EntryType(EEntryType::Collection),
			  bEnabled(bEnabledIn), 
			  CollectionPath(CollectionPathIn),
			  EntryLabel(EntryLabelIn),
			  Count(CountIn)
		{}

		// Tool Constructor
		FPresetViewEntry(FText EntryLabelIn, FSlateBrush EntryIconIn, FSoftObjectPath CollectionPathIn, FString ToolNameIn, int32 CountIn)
		  :	EntryType(EEntryType::Tool),
			CollectionPath(CollectionPathIn),
			EntryLabel(EntryLabelIn),
			EntryIcon(EntryIconIn),
			Count(CountIn),
			ToolName(ToolNameIn)
		{}

		// Preset Constructor
		FPresetViewEntry(FString ToolNameIn, int32 PresetIndexIn, FString PresetLabelIn, FString PresetTooltipIn, FText EntryLabelIn)
			: EntryType(EEntryType::Preset),
			  EntryLabel(EntryLabelIn),
			  ToolName(ToolNameIn),
			  PresetIndex(PresetIndexIn),
			  PresetLabel(PresetLabelIn),
			  PresetTooltip(PresetTooltipIn)
		{}

		bool HasSameMetadata(FPresetViewEntry& Other)
		{
			bool bIsEqual = 
				 EntryType == Other.EntryType &&
				 CollectionPath == Other.CollectionPath &&
				 bIsDefaultCollection == Other.bIsDefaultCollection &&
				 Count == Other.Count &&
				 ToolName.Equals(Other.ToolName) &&
				 PresetIndex == Other.PresetIndex &&
				 PresetLabel.Equals(Other.PresetLabel) &&
				 PresetTooltip.Equals(Other.PresetTooltip) &&
				 Children.Num() == Other.Children.Num();

			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				bIsEqual = bIsEqual && (*Children[ChildIndex]).HasSameMetadata(*Other.Children[ChildIndex]);
			}
			return bIsEqual;
		}

		bool operator==(FPresetViewEntry& Other)
		{
			bool bIsEqual = bEnabled == Other.bEnabled &&
				 EntryType == Other.EntryType &&
				 CollectionPath == Other.CollectionPath &&
				 bIsDefaultCollection == Other.bIsDefaultCollection && 
				 Count == Other.Count &&
				 EntryLabel.EqualTo(Other.EntryLabel) &&
				 ToolName.Equals(Other.ToolName) &&
				 PresetIndex == Other.PresetIndex &&
				 PresetLabel.Equals(Other.PresetLabel) &&
				 PresetTooltip.Equals(Other.PresetTooltip) &&
				 Children.Num() == Other.Children.Num();

			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				bIsEqual = bIsEqual && (*Children[ChildIndex]) == (*Other.Children[ChildIndex]);
			}

			return bIsEqual;
		}

		FPresetViewEntry& Root()
		{
			FPresetViewEntry* ActiveNode = this;
			while (ActiveNode->Parent)
			{
				ActiveNode = ActiveNode->Parent.Get();
			}
			return *ActiveNode;
		}
	};



	SLATE_BEGIN_ARGS(SPresetManager) { }
	SLATE_END_ARGS()

	virtual ~SPresetManager();

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget.
	 */
	void Construct( const FArguments& InArgs );
	
	//~ Begin SWidget Interface
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	//~ End SWidget Interface

protected:

private:

	void BindCommands();

	void RegeneratePresetTrees();

	int32 GetTotalPresetCount() const;

	TSharedRef<ITableRow> HandleTreeGenerateRow(TSharedPtr<FPresetViewEntry> TreeEntry, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleTreeGetChildren(TSharedPtr<FPresetViewEntry> TreeEntry, TArray< TSharedPtr<FPresetViewEntry> >& ChildrenOut);
	void HandleTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type);
	void HandleUserTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type);
	void HandleEditorTreeSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type);
	EVisibility ProjectPresetCollectionsVisibility() const;

	TSharedPtr<SWidget> OnGetPresetContextMenuContent() const;
	TSharedPtr<SWidget> OnGetCollectionContextMenuContent() const;

	void GeneratePresetList(TSharedPtr<FPresetViewEntry> TreeEntry);
	TSharedRef<ITableRow> HandleListGenerateRow(TSharedPtr<FPresetViewEntry> TreeEntry, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleListSelectionChanged(TSharedPtr<FPresetViewEntry> TreeEntry, ESelectInfo::Type SelectInfo);

	bool EditAreaEnabled() const;
	void SetCollectionEnabled(TSharedPtr<FPresetViewEntry> TreeEntry, ECheckBoxState State);
	void DeletePresetFromCollection(TSharedPtr< FPresetViewEntry > Entry);

	void CollectionRenameStarted(TSharedPtr<FPresetViewEntry> TreeEntry, TSharedPtr<SEditableTextBox> RenameWidget);
	void CollectionRenameEnded(TSharedPtr<FPresetViewEntry> TreeEntry, const FText& NewText);

	void DeleteSelectedUserPresetCollection();
	void AddNewUserPresetCollection();

	void SetPresetLabel(TSharedPtr< FPresetViewEntry >, FText InLabel);
	void SetPresetTooltip(TSharedPtr< FPresetViewEntry >, FText InTooltip);

	const FSlateBrush* GetProjectCollectionsExpanderImage() const;
	const FSlateBrush* GetUserCollectionsExpanderImage() const;
	const FSlateBrush* GetExpanderImage(TSharedPtr<SWidget> ExpanderWidget, bool bIsUserCollections) const;

	UInteractiveToolsPresetCollectionAsset* GetCollectionFromEntry(TSharedPtr<FPresetViewEntry> Entry);
	void SaveIfDefaultCollection(TSharedPtr<FPresetViewEntry> Entry);

	void OnDeleteClicked();
	bool CanDelete();

	void OnRenameClicked();
	bool CanRename();

private:
	
	TSharedPtr<FUICommandList> UICommandList;

	TWeakObjectPtr<UPresetUserSettings> UserSettings;

	TWeakPtr< SListView<TSharedPtr<FPresetViewEntry> > >  LastFocusedList;

	bool bAreProjectCollectionsExpanded = true;
	TSharedPtr<SButton> ProjectCollectionsExpander;
	TArray< TSharedPtr< FPresetViewEntry > > ProjectCollectionsDataList;
	TSharedPtr<STreeView<TSharedPtr<FPresetViewEntry> > > ProjectPresetCollectionTreeView;

	bool bAreUserCollectionsExpanded = true;
	TSharedPtr<SButton> UserCollectionsExpander;
	TArray< TSharedPtr< FPresetViewEntry > > UserCollectionsDataList;
	TSharedPtr<STreeView<TSharedPtr<FPresetViewEntry> > > UserPresetCollectionTreeView;

	TArray< TSharedPtr< FPresetViewEntry > > EditorCollectionsDataList;
	TSharedPtr<STreeView<TSharedPtr<FPresetViewEntry> > > EditorPresetCollectionTreeView;


	TArray< TSharedPtr< FPresetViewEntry > > PresetDataList;
	TSharedPtr<SListView<TSharedPtr<FPresetViewEntry> > > PresetListView;

	int32 TotalPresetCount;
	bool bHasActiveCollection;
	bool bHasPresetsInCollection;
	FText ActiveCollectionLabel;
	bool bIsActiveCollectionEnabled;

	TSharedPtr<SSplitter> Splitter;

	TSharedPtr<SVerticalBox>  EditPresetArea;
	TSharedPtr<SEditableTextBox> EditPresetLabel;
	TSharedPtr<SEditableTextBox> EditPresetTooltip;
	TSharedPtr<FPresetViewEntry> ActivePresetToEdit;

	TSharedPtr<SPositiveActionButton> AddUserPresetButton;
	TSharedPtr<SNegativeActionButton> DeleteUserPresetButton;
};
