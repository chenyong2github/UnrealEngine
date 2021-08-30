// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STreeView.h"

#include "FilterListData.h"
#include "Data/LevelSnapshotsEditorData.h"
#include "PropertySelection.h"

class SLevelSnapshotsEditorResults;
class ULevelSnapshot;

class FMenuBuilder;
class FPropertyEditorModule;
class FReply;
class IDetailTreeNode;
class IPropertyHandle;
class IPropertyRowGenerator;
class SBox;
class SSplitter;
class SSearchBox;
class STextBlock;

class ULevelSnapshotsEditorData;

struct EVisibility;
struct FPropertyRowGeneratorArgs;

struct FLevelSnapshotsEditorResultsRow;
struct FLevelSnapshotsEditorResultsSplitterManager;

typedef TSharedPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRowPtr;
typedef TSharedPtr<FLevelSnapshotsEditorResultsSplitterManager> FLevelSnapshotsEditorResultsSplitterManagerPtr;

enum ELevelSnapshotsObjectType
{
	ObjectType_None,
	ObjectType_Snapshot,
	ObjectType_World
};

struct FLevelSnapshotsEditorResultsSplitterManager
{
	float NestedColumnWidth = 0.5f; // The right side of the first splitter which contains the nested splitter for the property widgets
	float SnapshotPropertyColumnWidth = 0.5f;
};

struct FLevelSnapshotsEditorResultsRowStateMemory
{
	FLevelSnapshotsEditorResultsRowStateMemory()
	{
		bIsExpanded = false;
		WidgetCheckedState = ECheckBoxState::Checked;
	};
	
	FLevelSnapshotsEditorResultsRowStateMemory(const FString& InPathToRow, const bool bNewIsExpanded, const ECheckBoxState NewWidgetCheckedState)
		: PathToRow(InPathToRow)
		, bIsExpanded(bNewIsExpanded)
		, WidgetCheckedState(NewWidgetCheckedState)
	{};
	
	FString PathToRow;
	bool bIsExpanded;
	ECheckBoxState WidgetCheckedState;
};

struct FRowGeneratorInfo
{
	~FRowGeneratorInfo()
	{
		FlushReferences();
	}
	
	FRowGeneratorInfo(
		const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InBoundObject, const ELevelSnapshotsObjectType InGeneratorType, const TSharedRef<IPropertyRowGenerator>& InGeneratorObject)
		: BoundObject(InBoundObject)
		, GeneratorType(InGeneratorType)
		, GeneratorObject(InGeneratorObject)
	{};
	
	TWeakPtr<IPropertyRowGenerator> GetGeneratorObject() const;

	void FlushReferences();

private:
	/* The object that represents the object passed into the generator */
	TWeakPtr<FLevelSnapshotsEditorResultsRow> BoundObject;
	/* Whether this generator represents the snapshot or world object */
	ELevelSnapshotsObjectType GeneratorType = ObjectType_None;
	/* The actual generator ptr */
	TSharedPtr<IPropertyRowGenerator> GeneratorObject;
};

struct FPropertyHandleHierarchy
{
	FPropertyHandleHierarchy(const TSharedPtr<IDetailTreeNode>& InNode, const TSharedPtr<IPropertyHandle>& InHandle, const TWeakPtr<FPropertyHandleHierarchy> InParentHierarchy);
	
	TSharedPtr<IDetailTreeNode> Node;
	TSharedPtr<IPropertyHandle> Handle;
	TArray<TSharedRef<FPropertyHandleHierarchy>> DirectChildren;

	TWeakPtr<FPropertyHandleHierarchy> ParentHierarchy;
	// This chain is only used to identify counterparts. It may not accurately reflect the property chain used to apply this property the world.
	FLevelSnapshotPropertyChain TempIdentifierChain;
	// Used as a fallback for identifying counterparts for collection members
	FText DisplayName;
};

struct FLevelSnapshotsEditorResultsRow final : TSharedFromThis<FLevelSnapshotsEditorResultsRow>
{
	enum ELevelSnapshotsEditorResultsRowType
	{
		None,
		TreeViewHeader, // Includes rows that represent headers grouping together added, removed, or modified actor rows
		AddedActor, // Includes rows that represent actors which have been created in the world since the snapshot was taken
		RemovedActor, // Includes rows that represent actors which have been deleted from the world since the snapshot was taken
		ActorGroup, // Modified Actor group. Use AddedActor or RemovedActor for other Actor row types.
		ComponentGroup, // Includes rows that represent UActorComponent types
		SubObjectGroup, // Includes rows that represent separate non-component objects wholly owned by the world or snapshot actor
		StructGroup, // Rows that represent a single struct or a struct inside of a struct
		StructInMap, // Rows that represent a struct that's a value inside of a map
		StructInSetOrArray, // Rows that represent a struct inside a collection that isn't a map
		CollectionGroup, // Includes rows that represent TMap, TSet, and TArray.
		SingleProperty,
		SinglePropertyInStruct,
		SinglePropertyInMap,
		SinglePropertyInSetOrArray
	};

	enum ELevelSnapshotsEditorResultsTreeViewHeaderType
	{
		HeaderType_None,
		HeaderType_ModifiedActors,
		HeaderType_AddedActors,
		HeaderType_RemovedActors,
	};

	~FLevelSnapshotsEditorResultsRow();

	void FlushReferences();
	
	FLevelSnapshotsEditorResultsRow(const FText InDisplayName, const ELevelSnapshotsEditorResultsRowType InRowType, const ECheckBoxState StartingWidgetCheckboxState, 
		const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView, const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow = nullptr);

	void InitHeaderRow(
		const ELevelSnapshotsEditorResultsTreeViewHeaderType InHeaderType, const TArray<FText>& InColumns);
	
	void InitAddedActorRow(AActor* InAddedActor);
	void InitRemovedActorRow(const FSoftObjectPath& InRemovedActorPath);
	
	void InitActorRow(AActor* InSnapshotActor, AActor* InWorldActor);
	void InitObjectRow(
		UObject* InSnapshotObject, UObject* InWorldObject,
		const TWeakPtr<FRowGeneratorInfo>& InSnapshotRowGenerator,
		const TWeakPtr<FRowGeneratorInfo>& InWorldRowGenerator);

	void InitPropertyRow(
		const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InContainingObjectGroup,
		const TSharedPtr<FPropertyHandleHierarchy>& InSnapshotHierarchy, const TSharedPtr<FPropertyHandleHierarchy>& InWorldHandleHierarchy,
		const bool bNewIsCounterpartValueSame);

	void ApplyRowStateMemoryIfAvailable();
	const FString& GetOrGenerateRowPath();

	void GenerateActorGroupChildren(FPropertySelectionMap& PropertySelectionMap);

	bool DoesRowRepresentGroup() const;
	bool DoesRowRepresentObject() const;
	
	ELevelSnapshotsEditorResultsRowType GetRowType() const;

	/* Returns the ELevelSnapshotsEditorResultsRowType of a given property. Will never return ActorGroup or TreeViewHeader. Returns None on error. */
	static ELevelSnapshotsEditorResultsRowType DetermineRowTypeFromProperty(FProperty* InProperty, const bool bIsCustomized, const bool bHasChildProperties);

	const TArray<FText>& GetHeaderColumns() const;

	FText GetDisplayName() const;
	void SetDisplayName(const FText InDisplayName);

	const FSlateBrush* GetIconBrush() const;
	
	/* bHasGeneratedChildren must be true to get actual children. */
	const TArray<FLevelSnapshotsEditorResultsRowPtr>& GetChildRows() const;
	/* bHasGeneratedChildren must be true to get an accurate value. */
	int32 GetChildCount() const;
	void SetChildRows(const TArray<FLevelSnapshotsEditorResultsRowPtr>& InChildRows);
	void AddToChildRows(const FLevelSnapshotsEditorResultsRowPtr& InRow);
	void InsertChildRowAtIndex(const FLevelSnapshotsEditorResultsRowPtr& InRow, const int32 AtIndex = 0);

	bool GetIsTreeViewItemExpanded() const;
	void SetIsTreeViewItemExpanded(const bool bNewExpanded);

	bool GetShouldExpandAllChildren() const;
	void SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren);

	uint8 GetChildDepth() const;
	void SetChildDepth(const uint8 InDepth);

	TWeakPtr<FLevelSnapshotsEditorResultsRow> GetDirectParentRow() const;
	void SetDirectParentRow(const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow);

	/* Walks up each direct parent's direct parents until it reaches the first FLevelSnapshotsEditorResultsRow without a direct parent, then returns it. */
	TWeakPtr<FLevelSnapshotsEditorResultsRow> GetParentRowAtTopOfHierarchy();

	TWeakPtr<FLevelSnapshotsEditorResultsRow> GetContainingObjectGroup() const;

	bool GetHasGeneratedChildren() const;
	void SetHasGeneratedChildren(const bool bNewGenerated);

	/* If bMatchAnyTokens is false, only nodes that match all terms will be returned. */
	bool MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, const bool bMatchAnyTokens = false);

	/* This overload creates tokens from a string first, then calls ExecuteSearchOnChildNodes(const TArray<FString>& Tokens). */
	void ExecuteSearchOnChildNodes(const FString& SearchString) const;
	void ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const;

	void SetCachedSearchTerms(const FString& InTerms);

	UObject* GetSnapshotObject() const;
	UObject* GetWorldObject() const;

	UObject* GetFirstValidObject(ELevelSnapshotsObjectType& ReturnedType) const;

	FSoftObjectPath GetObjectPath() const;
	
	FProperty* GetProperty() const;
	FLevelSnapshotPropertyChain GetPropertyChain() const;

	TSharedPtr<IDetailTreeNode> GetSnapshotPropertyNode() const;
	TSharedPtr<IDetailTreeNode> GetWorldPropertyNode() const;
	ELevelSnapshotsObjectType GetFirstValidPropertyNode(TSharedPtr<IDetailTreeNode>& OutNode) const;

	TSharedPtr<IPropertyHandle> GetSnapshotPropertyHandle() const;
	TSharedPtr<IPropertyHandle> GetWorldPropertyHandle() const;
	ELevelSnapshotsObjectType GetFirstValidPropertyHandle(TSharedPtr<IPropertyHandle>& OutHandle) const;

	bool GetIsCounterpartValueSame() const;
	void SetIsCounterpartValueSame(const bool bIsValueSame);

	ECheckBoxState GetWidgetCheckedState() const;
	void SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates = false);

	bool GetIsNodeChecked() const;
	void SetIsNodeChecked(const bool bNewChecked, const bool bShouldUpdateHierarchyCheckedStates = false);

	/* Hierarchy utilities */

	bool HasVisibleChildren() const;

	bool HasCheckedChildren()const;
	bool HasUncheckedChildren() const;

	/* Whether the group has any children with associated properties that have any difference between the chosen snapshot and the current level. */
	bool HasChangedChildren() const;

	void GetAllCheckedChildProperties(TArray<FLevelSnapshotsEditorResultsRowPtr>& CheckedSinglePropertyNodeArray) const;
	void GetAllUncheckedChildProperties(TArray<FLevelSnapshotsEditorResultsRowPtr>& UncheckedSinglePropertyNodeArray) const;
	
	bool GetShouldCheckboxBeHidden() const;
	void SetShouldCheckboxBeHidden(const bool bNewShouldCheckboxBeHidden);
	
	EVisibility GetDesiredVisibility() const;

private:

	/* Generic properties */
	ELevelSnapshotsEditorResultsRowType RowType = SingleProperty;
	FText DisplayName;
	TArray<FLevelSnapshotsEditorResultsRowPtr> ChildRows;
	bool bIsTreeViewItemExpanded = false;

	// Used to epand all children on shift+click.
	bool bShouldExpandAllChildren = false;

	/* For Header Rows */
	ELevelSnapshotsEditorResultsTreeViewHeaderType HeaderType = HeaderType_None;
	TArray<FText> HeaderColumns;

	/* Number of parents in row's ancestor chain */
	uint8 ChildDepth = 0;
	TWeakPtr<FLevelSnapshotsEditorResultsRow> DirectParentRow = nullptr;

	/* This is the component, subobject or actor group to which this row belongs. If nullptr, this row is a top-level actor group. */
	TWeakPtr<FLevelSnapshotsEditorResultsRow> ContainingObjectGroup = nullptr;

	// Only applies to object groups - all of the property groups are generated with the rest of the single properties
	bool bHasGeneratedChildren = false;

	/* When we generate Search Terms for a node, it's saved here so it does not need to be generated again until filters are changed */
	FString CachedSearchTerms;
	bool bDoesRowMatchSearchTerms = true;

	/* This is a breadcrumb trail of display names used to find or store the state of the row. */
	FString RowPath;

	/* Returns a string of searchable keywords such as object names, property names, paths or anything else associated with the row that might be useful to search for. */
	const FString& GetOrCacheSearchTerms();

	/* Use MatchSearchTerms() first to match Search Terms against tokens, then call this method*/
	bool GetDoesRowMatchSearchTerms() const;
	void SetDoesRowMatchSearchTerms(const bool bNewMatch);

	/* For removed actor type rows */
	FSoftObjectPath RemovedActorPath;

	/* For object type rows */

	TWeakObjectPtr<UObject> SnapshotObject;
	TWeakObjectPtr<UObject> WorldObject;
	TWeakPtr<SLevelSnapshotsEditorResults> ResultsViewPtr;

	TWeakPtr<FRowGeneratorInfo> SnapshotRowGeneratorInfo;
	TWeakPtr<FRowGeneratorInfo> WorldRowGeneratorInfo;

	/* For property type rows */

	TSharedPtr<FPropertyHandleHierarchy> SnapshotPropertyHandleHierarchy;
	TSharedPtr<FPropertyHandleHierarchy> WorldPropertyHandleHierarchy;

	/* Whether the snapshot and world object properties have the same value */
	bool bIsCounterpartValueSame = false;

	void EvaluateAndSetAllParentGroupCheckedStates() const;

	/* Evaluates all factors which should make a row visible or invisible but does not set visibility. */
	bool ShouldRowBeVisible() const;

	bool bShouldCheckboxBeHidden;

	/* Checkbox in widget is bound to this property */
	ECheckBoxState WidgetCheckedState = ECheckBoxState::Checked;
};

class SLevelSnapshotsEditorResults final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResults)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ULevelSnapshotsEditorData* InEditorData);

	virtual ~SLevelSnapshotsEditorResults() override;

	FMenuBuilder BuildShowOptionsMenu();

	void SetShowFilteredRows(const bool bNewSetting);
	void SetShowUnselectedRows(const bool bNewSetting);

	bool GetShowFilteredRows() const;
	bool GetShowUnselectedRows() const;
	
	void FlushMemory(const bool bShouldKeepMemoryAllocated);

	TOptional<ULevelSnapshot*> GetSelectedLevelSnapshot() const;

	void OnSnapshotSelected(const TOptional<ULevelSnapshot*>& InLevelSnapshot);
	void RefreshResults(const bool bSnapshotHasChanged = false);
	FReply OnClickApplyToWorld();

	void UpdateSnapshotNameText(const TOptional<ULevelSnapshot*>& InLevelSnapshot) const;
	void UpdateSnapshotInformationText();

	void RefreshScroll() const;

	/* This method builds a selection set of all visible and checked properties to pass back to apply to the world. */
	void BuildSelectionSetFromSelectedPropertiesInEachActorGroup();

	FString GetSearchStringFromSearchInputField() const;
	void ExecuteResultsViewSearchOnAllActors(const FString& SearchString) const;
	void ExecuteResultsViewSearchOnSpecifiedActors(const FString& SearchString, const TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& ActorRowsToConsider) const;

	bool DoesTreeViewHaveVisibleChildren() const;

	void SetTreeViewItemExpanded(const TSharedPtr<FLevelSnapshotsEditorResultsRow>& RowToExpand, const bool bNewExpansion) const;

	ULevelSnapshotsEditorData* GetEditorDataPtr() const;

	// Row Generator Management
	
	static FPropertyRowGeneratorArgs GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs();

	/* This method should be used in place of the PropertyEditorModule's CreatePropertyRowGenerator
	 * because the returned generator is put into a struct managed by the Results View.*/
	TWeakPtr<FRowGeneratorInfo> RegisterRowGenerator(
		const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InBoundObject, const ELevelSnapshotsObjectType InGeneratorType,
		FPropertyEditorModule& PropertyEditorModule);

	void CleanUpGenerators(const bool bShouldKeepMemoryAllocated);

	bool FindRowStateMemoryByPath(const FString& InPath, FLevelSnapshotsEditorResultsRowStateMemory& OutRowStateMemory);
	void AddRowStateToRowStateMemory(const TSharedPtr<FLevelSnapshotsEditorResultsRowStateMemory> InRowStateMemory);
	void GenerateRowStateMemoryRecursively();

private:

	FLevelSnapshotsEditorResultsRowPtr& GetOrCreateDummyRow();

	// Header Slate Pointers
	TSharedPtr<STextBlock> SelectedSnapshotNamePtr;

	// Snapshot Information Text
	TSharedPtr<SVerticalBox> InfoTextBox;
	TSharedPtr<STextBlock> SelectedActorCountText;
	TSharedPtr<STextBlock> MiscActorCountText;

	FText DefaultNameText;
		
	FReply SetAllActorGroupsCollapsed();

	// Search
	
	void OnResultsViewSearchTextChanged(const FText& Text) const;

	FDelegateHandle OnActiveSnapshotChangedHandle;
	FDelegateHandle OnRefreshResultsHandle;

	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorDataPtr;

	TSharedPtr<SSearchBox> ResultsSearchBoxPtr;
	TSharedPtr<SBox> ResultsBoxContainerPtr;

	// 'Show' Options

	bool bShowFilteredActors = false;
	bool bShowUnselectedActors = true;

	/* For splitter sync */
	FLevelSnapshotsEditorResultsSplitterManagerPtr SplitterManagerPtr;

	//  Tree View Implementation

	void GenerateTreeView(const bool bSnapshotHasChanged);
	bool GenerateTreeViewChildren_ModifiedActors(FLevelSnapshotsEditorResultsRowPtr ModifiedActorsHeader, ULevelSnapshotFilter* UserFilters);
	bool GenerateTreeViewChildren_AddedActors(FLevelSnapshotsEditorResultsRowPtr AddedActorsHeader);
	bool GenerateTreeViewChildren_RemovedActors(FLevelSnapshotsEditorResultsRowPtr RemovedActorsHeader);
	
	void OnGetRowChildren(FLevelSnapshotsEditorResultsRowPtr Row, TArray<FLevelSnapshotsEditorResultsRowPtr>& OutChildren);
	void OnRowChildExpansionChange(FLevelSnapshotsEditorResultsRowPtr Row, const bool bIsExpanded, const bool bIsRecursive = false) const;

	void SetChildExpansionRecursively(const FLevelSnapshotsEditorResultsRowPtr& InRow, const bool bNewIsExpanded) const;
	
	TSharedPtr<STreeView<FLevelSnapshotsEditorResultsRowPtr>> TreeViewPtr;
	
	/** Holds all the header groups */
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewRootHeaderObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewModifiedActorGroupObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewAddedActorGroupObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewRemovedActorGroupObjects;

	/* Used to show that a group has children before the children are actually generated */
	FLevelSnapshotsEditorResultsRowPtr DummyRow;
	FFilterListData FilterListData;

	/* The results view should be the sole manager of the RowGenerators' lifetimes */
	TSet<TSharedPtr<FRowGeneratorInfo>> RegisteredRowGenerators;

	/* This list remembers the checked and expansion states of previously created rows so these states can be recreated on refresh. */
	TSet<TSharedPtr<FLevelSnapshotsEditorResultsRowStateMemory>> RowStateMemory;
};

class SLevelSnapshotsEditorResultsRow : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsRow)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InRow, const FLevelSnapshotsEditorResultsSplitterManagerPtr& InSplitterManagerPtr);

	~SLevelSnapshotsEditorResultsRow();

	float GetNameColumnSize() const;

	float CalculateAndReturnNestedColumnSize();

	float GetSnapshotColumnSize() const;

	float GetWorldColumnSize() const;

	void SetNestedColumnSize(const float InWidth) const;

	void SetSnapshotColumnSize(const float InWidth) const;

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	
	TSharedPtr<SSplitter> OuterSplitterPtr = nullptr;
	TSharedPtr<SSplitter> NestedSplitterPtr = nullptr;
	
	TWeakPtr<FLevelSnapshotsEditorResultsRow> Item = nullptr;

	/* For splitter sync */

	/* To sync up splitter location in tree view items, we need to account for the tree view's indentation.
	 * Instead of calculating the coefficient twice each frame (for left and right splitter slots), we do it once and cache it here. */
	float CachedNestedColumnWidthAdjusted = 0.f;
	
	FLevelSnapshotsEditorResultsSplitterManagerPtr SplitterManagerPtr;
};
