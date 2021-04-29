// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FilterListData.h"
#include "Data/LevelSnapshotsEditorData.h"

#include "IPropertyRowGenerator.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

struct FFilterListData;
class SLevelSnapshotsEditorResults;
struct FLevelSnapshotsEditorResultsRow;
struct FLevelSnapshotsEditorResultsSplitterManager;
class FLevelSnapshotsEditorResults;
class SBox;
class SButton;
class SSearchBox;
class SSplitter;
class UFilteredResults;

struct FLevelSnapshotsEditorViewBuilder;
struct FLevelSnapshotPropertyChain;
struct FPropertySelection;
struct FPropertySelectionMap;

typedef TSharedPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRowPtr;
typedef TSharedPtr<FLevelSnapshotsEditorResultsSplitterManager> FLevelSnapshotsEditorResultsSplitterManagerPtr;

struct FLevelSnapshotsEditorResultsSplitterManager
{
	float NameColumnWidth = 1.0f;
	float SnapshotPropertyColumnWidth = 1.0f;
	float WorldObjectPropertyColumnWidth = 1.0f;
};

struct FLevelSnapshotsEditorResultsRow final : TSharedFromThis<FLevelSnapshotsEditorResultsRow>
{
	enum ELevelSnapshotsEditorResultsRowType
	{
		None,
		TreeViewHeader,
		AddedActor,
		RemovedActor,
		ActorGroup,
		ComponentGroup,
		SubObjectGroup,
		StructGroup,
		CollectionGroup,
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

	enum ELevelSnapshotsObjectType
	{
		ObjectType_None,
		ObjectType_Snapshot,
		ObjectType_World
	};

	enum ELevelSnapshotsWidgetTypeCustomization
	{
		WidgetType_NoCustomWidget,
		WidgetType_Location,
		WidgetType_Rotation,
		WidgetType_Scale3D,
		WidgetType_UnsupportedProperty
	};

	~FLevelSnapshotsEditorResultsRow();
	
	FLevelSnapshotsEditorResultsRow(const FText InDisplayName, const ELevelSnapshotsEditorResultsRowType InRowType, const ECheckBoxState StartingWidgetCheckboxState, const FLevelSnapshotsEditorResultsRowPtr DirectParent = nullptr);

	void InitHeaderRow(
		const ELevelSnapshotsEditorResultsTreeViewHeaderType InHeaderType, const TArray<FText>& InColumns, const TSharedRef<SLevelSnapshotsEditorResults>& InResultsView);
	
	void InitAddedActorRow(const TWeakObjectPtr<AActor>& InAddedActor, const TSharedRef<SLevelSnapshotsEditorResults>& InResultsView);
	void InitRemovedActorRow(const FSoftObjectPath& InRemovedActorPath, const TSharedRef<SLevelSnapshotsEditorResults>& InResultsView);
	
	void InitActorRow(const TWeakObjectPtr<AActor>& InSnapshotActor, const TWeakObjectPtr<AActor>& InWorldActor, const TSharedRef<SLevelSnapshotsEditorResults>& InResultsView);
	void InitObjectRow(const TSharedPtr<IPropertyRowGenerator> InSnapshotRowGenerator, const TSharedPtr<IPropertyRowGenerator> InWorldRowGenerator);

	// Only populate InWorldProperty and InSnapshotProperty if InWorldHandle is nullptr, such as when a handle is not created by PropertyRowGenerator
	void InitPropertyRowWithHandles(
		const FLevelSnapshotsEditorResultsRowPtr InContainingObjectGroup, 
		TSharedPtr<IPropertyHandle> InSnapshotHandle, TSharedPtr<IPropertyHandle> InWorldHandle, 
		const bool bNewIsCounterpartValueSame = false, const ELevelSnapshotsWidgetTypeCustomization InWidgetTypeCustomization = WidgetType_NoCustomWidget);

	void InitPropertyRowWithObjectsAndProperty(
		const FLevelSnapshotsEditorResultsRowPtr InContainingObjectGroup, 
		UObject* InSnapshotObject, UObject* InWorldObject,
		FProperty* InPropertyForCustomization, const ELevelSnapshotsWidgetTypeCustomization InWidgetTypeCustomization = WidgetType_Location);

	void GenerateActorGroupChildren(FPropertySelectionMap& PropertySelectionMap);

	bool DoesRowRepresentGroup() const;
	bool DoesRowRepresentObject() const;
	
	ELevelSnapshotsEditorResultsRowType GetRowType() const;

	/* Returns the ELevelSnapshotsEditorResultsRowType of a given property. Will never return ActorGroup or TreeViewHeader. Returns None on error. */
	static ELevelSnapshotsEditorResultsRowType DetermineRowTypeFromProperty(FProperty* InProperty);

	const TArray<FText>& GetHeaderColumns() const;

	FText GetDisplayName() const;
	void SetDisplayName(const FText InDisplayName);

	TArray<FLevelSnapshotsEditorResultsRowPtr> GetChildRows() const;
	void SetChildRows(const TArray<FLevelSnapshotsEditorResultsRowPtr> InChildRows);
	void AddToChildRows(FLevelSnapshotsEditorResultsRowPtr InRow);
	void InsertChildRowAtIndex(FLevelSnapshotsEditorResultsRowPtr InRow, const int32 AtIndex = 0);

	bool GetIsTreeViewItemExpanded() const;
	void SetIsTreeViewItemExpanded(const bool bNewExpanded);

	uint8 GetChildDepth() const;
	void SetChildDepth(const uint8 InDepth);

	FLevelSnapshotsEditorResultsRowPtr GetDirectParentRow() const;
	void SetDirectParentRow(const FLevelSnapshotsEditorResultsRowPtr& InDirectParentRow);

	/* Walks up each direct parent's direct parents until it reaches the first FLevelSnapshotsEditorResultsRow without a direct parent, then returns it. */
	FLevelSnapshotsEditorResultsRowPtr GetParentRowAtTopOfHierarchy();

	const FLevelSnapshotsEditorResultsRowPtr& GetContainingObjectGroup() const;

	ELevelSnapshotsWidgetTypeCustomization GetWidgetTypeCustomization() const;

	bool GetHasGeneratedChildren() const;
	void SetHasGeneratedChildren(const bool bNewGenerated);

	/* If bMatchAnyTokens is false, only nodes that match all terms will be returned. */
	bool MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, const bool bMatchAnyTokens = false);

	/* This overload creates tokens from a string first, then calls ExecuteSearchOnChildNodes(const TArray<FString>& Tokens). */
	void ExecuteSearchOnChildNodes(const FString& SearchString) const;
	void ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const;

	UObject* GetSnapshotObject() const;
	UObject* GetWorldObject() const;

	UObject* GetFirstValidObject(ELevelSnapshotsObjectType& ReturnedType) const;

	FSoftObjectPath GetObjectPath() const;

	const TSharedPtr<IPropertyRowGenerator>& GetSnapshotRowGenerator() const;
	const TSharedPtr<IPropertyRowGenerator>& GetWorldRowGenerator() const;
	
	FProperty* GetProperty() const;
	FLevelSnapshotPropertyChain GetPropertyChain() const;

	const TSharedPtr<IPropertyHandle>& GetSnapshotPropertyHandle() const;
	const TSharedPtr<IPropertyHandle>& GetWorldPropertyHandle() const;
	ELevelSnapshotsObjectType GetFirstValidPropertyHandle(TSharedPtr<IPropertyHandle>& OutHandle) const;

	FProperty* GetPropertyForCustomization() const;

	bool GetIsCounterpartValueSame() const;
	void SetIsCounterpartValueSame(const bool bIsValueSame);

	ECheckBoxState GetWidgetCheckedState() const;
	void SetWidgetCheckedState(const ECheckBoxState NewState, const bool bUserClicked = false);

	bool GetIsNodeChecked() const;
	void SetIsNodeChecked(const bool bNewChecked);

	/* Hierarchy utilities */

	bool HasVisibleChildren() const;

	bool HasCheckedChildren()const;
	bool HasUncheckedChildren() const;

	/* Whether the group has any children with associated properties that have any difference between the chosen snapshot and the current level. */
	bool HasChangedChildren() const;

	/* Gets child single properties, whole collection properties and individual properties inside of structs that belong to a specific group. Skips nested object groups like components and subobjects. */
	void GetAllCheckedChildProperties(TArray<FLevelSnapshotsEditorResultsRowPtr>& CheckedSinglePropertyNodeArray) const;
	
	EVisibility GetDesiredVisibility() const;

private:

	/* Generic properties */
	ELevelSnapshotsEditorResultsRowType RowType = SingleProperty;
	FText DisplayName;
	TArray<FLevelSnapshotsEditorResultsRowPtr> ChildRows;
	bool bIsTreeViewItemExpanded = false;

	/* For Header Rows */
	ELevelSnapshotsEditorResultsTreeViewHeaderType HeaderType = HeaderType_None;
	TArray<FText> HeaderColumns;

	/* Number of parents in row's ancestor chain */
	uint8 ChildDepth = 0;
	FLevelSnapshotsEditorResultsRowPtr DirectParentRow = nullptr;

	/* This is the component, subobject or actor group to which this row belongs. If nullptr, this row is a top-level actor group. */
	FLevelSnapshotsEditorResultsRowPtr ContainingObjectGroup = nullptr;

	// Whether we will generate a custom widget for this row instead of using a handle from PropertyRowGenerator
	ELevelSnapshotsWidgetTypeCustomization WidgetTypeCustomization = WidgetType_NoCustomWidget;

	// Only applies to object groups - all of the property groups are generated with the rest of the single properties
	bool bHasGeneratedChildren = false;

	/* When we generate Search Terms for a node, it's saved here so it does not need to be generated again until filters are changed */
	FString CachedSearchTerms;
	bool bDoesRowMatchSearchTerms = true;

	/* Returns a string of searchable keywords such as object names, property names, paths or anything else associated with the row that might be useful to search for. */
	const FString& GetSearchTerms() const;

	/* Use MatchSearchTerms() first to match Search Terms against tokens, then call this method*/
	bool GetDoesRowMatchSearchTerms() const;
	void SetDoesRowMatchSearchTerms(const bool bNewMatch);

	/* For removed actor type rows */
	FSoftObjectPath RemovedActorPath;

	/* For actor type rows */

	TWeakObjectPtr<AActor> SnapshotActor;
	TWeakObjectPtr<AActor> WorldActor;
	TSharedPtr<SLevelSnapshotsEditorResults> ResultsViewPtr;

	/* For non-actor object type rows */

	TSharedPtr<IPropertyRowGenerator> SnapshotRowGenerator;
	TSharedPtr<IPropertyRowGenerator> WorldRowGenerator;

	/* For property type rows */

	TSharedPtr<IPropertyHandle> SnapshotPropertyHandle;
	TSharedPtr<IPropertyHandle> WorldPropertyHandle;

	/* In the event a property row does not have a generated handle, we will store objects and FProperty directly. Otherwise, FProperty should be retrieved from the Handles. */

	TWeakObjectPtr<UObject> SnapshotObjectForCustomization;
	TWeakObjectPtr<UObject> WorldObjectForCustomization;
	FProperty* PropertyForCustomization;

	/* Whether the snapshot and world object properties have the same value */
	bool bIsCounterpartValueSame = false;

	void EvaluateAndSetAllParentGroupCheckedStates() const;

	/* Evaluates all factors which should make a row visible or invisible but does not set visibility. */
	bool ShouldRowBeVisible() const;

	/* Checkbox in widget is bound to this property */
	ECheckBoxState WidgetCheckedState = ECheckBoxState::Checked;
};

class SLevelSnapshotsEditorResults final : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResults)
	{}

	SLATE_END_ARGS()

	enum ERefreshStyle
	{
		FiltersChanged,
		FiltersNotChanged
	};

	void Construct(const FArguments& InArgs, ULevelSnapshotsEditorData* InEditorData);

	~SLevelSnapshotsEditorResults();

	TOptional<ULevelSnapshot*> GetSelectedLevelSnapshot() const;

	void OnSnapshotSelected(const TOptional<ULevelSnapshot*>& InLevelSnapshot);
	void RefreshResults();
	FReply OnClickApplyToWorld();

	void UpdateSnapshotInformationText();

	/* This method builds a selection set of all visible and checked properties to pass back to apply to the world. */
	void BuildSelectionSetFromSelectedPropertiesInEachActorGroup();

	const FString& GetSearchStringFromSearchInputField() const;

	TWeakObjectPtr<ULevelSnapshotsEditorData> GetEditorDataPtr() const;

private:

	FLevelSnapshotsEditorResultsRowPtr& GetOrCreateDummyRow();

	// Header Slate Pointers
	TSharedPtr<STextBlock> SelectedSnapshotNamePtr;

	// Snapshot Information Text
	TSharedPtr<STextBlock> SelectedActorCountText;
	TSharedPtr<STextBlock> TotalActorCountText;
	TSharedPtr<STextBlock> MiscActorCountText;
		
	FReply SetAllActorGroupsCollapsed();

	// Search
	
	void OnResultsViewSearchTextChanged(const FText& Text);
	void ExecuteResultsViewSearch(const FString& SearchString);

	FDelegateHandle OnActiveSnapshotChangedHandle;
	FDelegateHandle OnRefreshResultsHandle;

	TWeakObjectPtr<ULevelSnapshotsEditorData> EditorDataPtr;

	TSharedPtr<SSearchBox> ResultsSearchBoxPtr;
	TSharedPtr<SBox> ResultsBoxContainerPtr;

	/* For splitter sync */
	FLevelSnapshotsEditorResultsSplitterManagerPtr SplitterManagerPtr;

	//  Tree View Implementation

	void GenerateTreeView();
	void GenerateTreeViewChildren_ModifiedActors(const FLevelSnapshotsEditorResultsRowPtr& ModifiedActorsHeader, const TWeakObjectPtr<ULevelSnapshotFilter>& UserFilters);
	void GenerateTreeViewChildren_AddedActors(const FLevelSnapshotsEditorResultsRowPtr& AddedActorsHeader);
	void GenerateTreeViewChildren_RemovedActors(const FLevelSnapshotsEditorResultsRowPtr& RemovedActorsHeader);
	
	void OnGetRowChildren(FLevelSnapshotsEditorResultsRowPtr Row, TArray<FLevelSnapshotsEditorResultsRowPtr>& OutChildren);
	void OnRowChildExpansionChange(TSharedPtr<FLevelSnapshotsEditorResultsRow> Row, const bool bIsExpanded);
	
	TSharedPtr<STreeView<FLevelSnapshotsEditorResultsRowPtr>> TreeViewPtr;
	
	/** Holds all the actor groups. */
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewRootHeaderObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewModifiedActorGroupObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewAddedActorGroupObjects;
	TArray<FLevelSnapshotsEditorResultsRowPtr> TreeViewRemovedActorGroupObjects;

	FLevelSnapshotsEditorResultsRowPtr DummyRow;
	FFilterListData FilterListData;

};

class SLevelSnapshotsEditorResultsRow : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsRow)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const FLevelSnapshotsEditorResultsRowPtr InRow, const FLevelSnapshotsEditorResultsSplitterManagerPtr& InSplitterManagerPtr);

	~SLevelSnapshotsEditorResultsRow();

	float GetSplitterSlotSize(const int32 SlotIndex) const;

	void SetNameColumnSize(const float InWidth) const;

	void SetSnapshotColumnSize(const float InWidth) const;

	void SetWorldObjectColumnSize(const float InWidth) const;

private:
	
	TSharedPtr<SSplitter> SplitterPtr = nullptr;
	FLevelSnapshotsEditorResultsRowPtr Item;

	/* For splitter sync */
	FLevelSnapshotsEditorResultsSplitterManagerPtr SplitterManagerPtr;
};
