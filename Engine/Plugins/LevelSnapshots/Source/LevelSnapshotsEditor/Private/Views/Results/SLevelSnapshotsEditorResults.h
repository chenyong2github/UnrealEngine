// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "UObject/StrongObjectPtr.h"

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class SLevelSnapshotsEditorResultsRowGroup;
class FLevelSnapshotsEditorResults;
class SLevelSnapshotsEditorResults;
class ULevelSnapshot;
struct SLevelSnapshotsEditorResultsField;
struct FLevelSnapshotsEditorViewBuilder;
class IPropertyRowGenerator;

enum class ECheckBoxState : uint8;

struct FLevelSnapshotsEditorResultsRow
{
	enum ENodeType
	{
		Group,
		Field,
		FieldChild
	};

	virtual ~FLevelSnapshotsEditorResultsRow() {}

	/** Get get this node's type. */
	virtual ENodeType GetType() = 0;

	virtual TSharedPtr<SLevelSnapshotsEditorResultsField> AsField() { return nullptr; }

	virtual TSharedPtr<SWidget> AsFieldChild() { return nullptr; }

	virtual TSharedPtr<struct FLevelSnapshotsEditorFilterRowGroup> AsGroup() { return nullptr; }

	/** Get this tree node's children. */
	virtual void GetNodeChildren(TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutChildren) {}
};

struct SLevelSnapshotsEditorResultsChildField : public SCompoundWidget, public FLevelSnapshotsEditorResultsRow
{

};

struct SLevelSnapshotsEditorResultsField : public SCompoundWidget, public FLevelSnapshotsEditorResultsRow
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsField)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SWidget>& InContent);

	virtual ENodeType GetType() override { return ENodeType::Field; }

	virtual TSharedPtr<SLevelSnapshotsEditorResultsField> AsField() override;

	TSharedPtr<SCheckBox> CheckboxPtr;

//public:
//	FString ActorObjectPath;
};

struct FLevelSnapshotsEditorResultsRowGroup : public FLevelSnapshotsEditorResultsRow, public TSharedFromThis<FLevelSnapshotsEditorResultsRowGroup>
{
	FLevelSnapshotsEditorResultsRowGroup(const FString& InObjectPath, const FLevelSnapshot_Actor& InActorSnapshot)
		: ObjectPath(InObjectPath)
		, ActorSnapshot(InActorSnapshot)
	{}
	
	virtual ENodeType GetType() override { return ENodeType::Group; }

	virtual void GetNodeChildren(TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutChildren) override;

	void AddCurrentPropertyWidget(const TSharedPtr<IPropertyHandle> InPropertyHandle);

public:
	FString ObjectPath;
	FLevelSnapshot_Actor ActorSnapshot;

	/** This group's fields' widget. */
	TArray<TSharedPtr<SLevelSnapshotsEditorResultsField>> Fields;

	TArray<TSharedRef<SHorizontalBox>> FieldWidgetHBoxes;
	
};

struct FActorSnapshotCounterpartInfo
{
	FActorSnapshotCounterpartInfo() = default;

	FActorSnapshotCounterpartInfo(const FString PropertyName, const TSharedPtr<IPropertyHandle> InHandle)
	{
		CurrentLevelProperties.Add(PropertyName, InHandle);
	}
	
public:

	// Property name and handle
	TMap<FString,TSharedPtr<IPropertyHandle>> CurrentLevelProperties;
};

class SLevelSnapshotsEditorResultsRowGroup : public STableRow<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>>
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResultsRowGroup)
	{}
	SLATE_END_ARGS()

	void Tick(const FGeometry&, const double, const float);

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorResults>& OwnerPanel);

	// A pointer to the expand/collapse caret
	TSharedPtr<SExpanderArrow> ExpanderArrowPtr;
	// A pointer to the group checkbox (not field checkbox)
	TSharedPtr<SCheckBox> CheckboxPtr;
};

class SLevelSnapshotsEditorResults : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorResults();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorResults)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorResults>& InEditorResults, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

private:
	/** Generates a tree row. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get this group's children */
	void OnGetGroupChildren(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& OutRows);

	void OnSelectionChanged(TSharedPtr<FLevelSnapshotsEditorResultsRow> InResultsRow, ESelectInfo::Type SelectInfo);

	void Refresh();

	void RefreshGroups();
	
	void OnSnapshotSelected(ULevelSnapshot* InLevelSnapshot);

	// Called by OnSnapshotSelected - separated out to allow it to be called for the selected snapshot and a transient snapshot for the current level
	void ProvisionSnapshotActors(ULevelSnapshot* InLevelSnapshot, FPropertyEditorModule& PropertyEditorModule, bool bFromCurrentLevel);
	
	// For the Select/Deselect All buttons
	FReply SetAllGroupsSelected();
	FReply SetAllGroupsUnselected();
	FReply SetAllGroupsCollapsed();

	// Show/hide unchanged groups
	void OnCheckedStateChange_ShowUnchangedSnapshotActors(ECheckBoxState NewState);
	void SetShowUnchangedSnapshotGroups(bool bShowGroups);
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>> DetermineUnchangedGroupsFromLevelSnapshot(ULevelSnapshot* InLevelSnapshot);

	TSharedRef<SWidget> MakeAddFilterMenu();

	// When a ULevelSnapshot is selected in the UI, let's save a reference to it in case we need it for diffing or other things
	TWeakObjectPtr<ULevelSnapshot> SelectedLevelSnapshotPtr;

	// Actor Unique ID and Counterpart Info struct
	TMap<int32, FActorSnapshotCounterpartInfo> ActorSnapshotCounterpartMap;

private:
	TWeakPtr<FLevelSnapshotsEditorResults> EditorResultsPtr;

	/** Holds all the field groups. */
	TArray<TSharedPtr<FLevelSnapshotsEditorResultsRowGroup>> FieldGroups;

	TArray<TSharedPtr<IPropertyRowGenerator>> Generators;

	/** Holds the section list view. */
	TSharedPtr<STreeView<TSharedPtr<FLevelSnapshotsEditorResultsRow>>> ResultList;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;

	TArray<TStrongObjectPtr<UObject>> ActorObjects;

	TSharedPtr<SCheckBox> FilterButtonPtr;
};
