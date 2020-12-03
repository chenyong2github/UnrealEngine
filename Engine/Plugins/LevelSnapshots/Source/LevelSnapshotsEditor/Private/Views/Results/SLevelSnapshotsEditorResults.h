// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"

#include "UObject/WeakFieldPtr.h"

class FLevelSnapshotsEditorResults;
class SLevelSnapshotsEditorResults;
class SLevelSnapshotsEditorResultsSingleProperty;
class SLevelSnapshotsEditorResultsContainerPropertyGroup;
struct FLevelSnapshotsEditorViewBuilder;

enum class ECheckBoxState : uint8;

class SLevelSnapshotsEditorResultsExpanderArrow : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnArrowClicked, bool)

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsExpanderArrow)
	{}

	SLATE_EVENT(FOnArrowClicked, OnArrowClickedDelegate)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool IsItemExpanded() const;

	void SetItemExpanded(bool bNewExpanded);

	void ToggleItemExpanded();

private:
	
	FOnArrowClicked OnArrowClickedDelegate;

	bool bIsItemExpanded = false;

	TSharedPtr<SButton> ExpanderArrowPtr;
};

class FLevelSnapshotsEditorResultsRow
{
public:
	virtual ~FLevelSnapshotsEditorResultsRow() = default;

	enum ELevelSnapshotsEditorResultsRowType
	{
		ActorGroup,
		ContainerGroup,
		SingleProperty
	};

	/** Get get this node's type. */
	virtual ELevelSnapshotsEditorResultsRowType GetType() const;

	virtual ECheckBoxState GetIsNodeChecked();

	virtual void SetIsNodeChecked(bool bNewChecked);

	virtual bool GetIsItemExpanded();

	virtual void SetIsItemExpanded(bool bNewExpanded);

	virtual uint8 GetIndentationDepth();

	virtual void SetIndentationDepth(const uint8 InDepth);

	virtual float GetNodeIndentationWidth() const;

	virtual bool HasVisibleChildren();

	virtual void SetAllChildContainerGroupsVisibilityBasedOnTheirChildren();

	virtual void GetAllSinglePropertyChildrenRecursively(
		TArray<TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>>& OutSinglePropertyNodeArray);

	virtual void GetAllContainerGroupChildrenRecursively(
		TArray<TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>>& OutContainerGroupNodeArray);

	virtual void GetAllChildRowsRecursively(TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutRowNodeArray);

	// Pure virtual
	virtual void OnNodeCheckStateChanged(const ECheckBoxState NewCheckState) = 0;

public:

	// Any property in this container that isn't a struct/array/map/set or nested within one
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty>> DirectSingleProperties;
	// Any property in this container that is a struct/array/map/set, but not nested
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup>> DirectGroupProperties;
	
protected:
	ELevelSnapshotsEditorResultsRowType NodeType = ELevelSnapshotsEditorResultsRowType::SingleProperty;

	// Number of FLevelSnapshotsEditorResultsRow parents this widget has
	uint8 IndentationDepth = 0;

	TSharedPtr<SCheckBox> CheckboxPtr = nullptr;
	TSharedPtr<SLevelSnapshotsEditorResultsExpanderArrow> ExpanderArrowPtr = nullptr;

private:
	// The amount a child will be indented by within a group (ChildIndentationWidth * IndentationDepth)
	float ChildIndentationWidth = 20.0f;
};

class SLevelSnapshotsEditorResultsActorGroup final : public SCompoundWidget, public FLevelSnapshotsEditorResultsRow
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsActorGroup)
	{}

	SLATE_ATTRIBUTE(FSoftObjectPath, ActorPath)
	
	SLATE_ATTRIBUTE(FName, ActorName)
	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void AddToContents(TSharedRef<SWidget> InContent) const;

	virtual void OnNodeCheckStateChanged(const ECheckBoxState NewCheckState) override {}

private:
	TAttribute<FSoftObjectPath> ActorPath;

	TAttribute<FName> ActorName;
	
	TSharedPtr<SScrollBox> ScrollBoxPtr;
};

// Used to house nested Struct and array properties in results view
class SLevelSnapshotsEditorResultsContainerPropertyGroup final : public SCompoundWidget, public FLevelSnapshotsEditorResultsRow
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsContainerPropertyGroup)
	{}

	SLATE_ATTRIBUTE(FText, ContainerName)

	SLATE_ARGUMENT(uint8, IndentationDepth)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void AddToContents(TSharedRef<SWidget> InContent) const;

	// When a container node is checked, all children should be checked the same way
	virtual void OnNodeCheckStateChanged(const ECheckBoxState NewCheckState) override;

private:

	TAttribute<FText> ContainerName;
	
	TSharedPtr<SVerticalBox> ContentVBoxPtr;
};

class SLevelSnapshotsEditorResultsSingleProperty final : public SCompoundWidget, public FLevelSnapshotsEditorResultsRow
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsSingleProperty)
	{}

	SLATE_ARGUMENT(bool, bIsPropertyDifferentInLevel)
	SLATE_ARGUMENT(FString, SnapshotValueAsString)
	SLATE_ARGUMENT(FString, CounterpartValueAsString)
	SLATE_ARGUMENT(uint8, IndentationDepth)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<SLevelSnapshotsEditorResults> ResultsWidget,
		const FProperty* InProperty, const TSharedPtr<SWidget>& SnapshotActorWidget, const TSharedPtr<SWidget>& CurrentLevelActorWidget);

	void SyncSplitter(const float InWidth, const int32 SlotIndex) const;

	virtual void OnNodeCheckStateChanged(const ECheckBoxState NewCheckState) override {}

	bool GetIsPropertyChangedInLevel() const
	{
		return bIsPropertyChangedInLevel;
	}

private:
	FName PropertyName;

	TSharedPtr<SSplitter> SplitterPtr;

	// Argument Properties
	bool bIsPropertyChangedInLevel = false;
};

class SLevelSnapshotsEditorResults final : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorResults();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FSyncLevelSnapshotsResultsSplitters, float, int32)

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResults)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	// Used for syncing SSplitters on each row
	FSyncLevelSnapshotsResultsSplitters SyncLevelSnapshotsResultsSplittersDelegate;

	void OnColumnResized(const float InWidth, const int32 SlotIndex) const;

private:
	
	void OnSnapshotSelected(ULevelSnapshot* InLevelSnapshot);

	TSharedPtr<SLevelSnapshotsEditorResultsContainerPropertyGroup> GenerateResultsContainerGroupWidget(
		UObject* SnapshotObject, UObject* LevelCounterpartObject, FProperty* SnapshotProperty, const uint8 ParentIndentationDepth = -1,
		void* SpecifiedOuterValue = nullptr, void* SpecifiedCounterpartOuter = nullptr);
	
	TSharedPtr<SLevelSnapshotsEditorResultsSingleProperty> GenerateResultsSinglePropertyWidget(
		UObject* SnapshotObject, UObject* LevelCounterpartObject, FProperty* SnapshotProperty, const int32 ArrayDimIndex = -1, 
		const uint8 ParentIndentationDepth = -1, void* SpecifiedOuterValue = nullptr, void* SpecifiedCounterpartOuter = nullptr);

	TSharedRef<SWidget> LoopSnapshotActors(ULevelSnapshot* InLevelSnapshot);
	
	// For the Select/Deselect All buttons
	FReply SetAllGroupsSelected();
	FReply SetAllGroupsUnselected();
	FReply SetAllGroupsCollapsed();

	// Show/hide unchanged groups
	void OnCheckedStateChange_ShowUnchangedSnapshotActors(ECheckBoxState NewState);
	void SetShowUnchangedSnapshotGroups(bool bShowGroups);

	TSharedRef<SWidget> MakeAddFilterMenu();

	// When a ULevelSnapshot is selected in the UI, let's save a reference to it in case we need it for diffing or other things
	TWeakObjectPtr<ULevelSnapshot> SelectedLevelSnapshotPtr;

	TWeakPtr<FLevelSnapshotsEditorResults> EditorResultsPtr;

	TSharedPtr<SBox> ResultsBoxPtr;

	/** Holds all the actor groups. */
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsActorGroup>> ActorGroups;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;

	TSharedPtr<SCheckBox> FilterButtonPtr;
	TSharedPtr<SCheckBox> ShowUnchangedCheckboxPtr;
};
