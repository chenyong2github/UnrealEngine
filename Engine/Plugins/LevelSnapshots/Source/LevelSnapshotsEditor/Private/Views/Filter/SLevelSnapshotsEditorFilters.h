// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class FLevelSnapshotsEditorFilters;
class FLevelSnapshotsEditorFilterClass;
class IDetailsView;
class ULevelSnapshotsEditorData;
class ULevelSnapshotFilter;
class ULevelSnapshotEditorFilterGroup;
class SFavoriteFilterList;
class SLevelSnapshotsEditorFilters;

struct FLevelSnapshotsEditorFilterRowGroup;

struct FLevelSnapshotsEditorFilterRow
{
	enum ENodeType
	{
		Group,
		Field,
		FieldChild
	};

	virtual ~FLevelSnapshotsEditorFilterRow() {}

	/** Get get this node's type. */
	virtual ENodeType GetType() = 0;

	virtual TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> AsGroup() { return nullptr; }

	/** Get this tree node's childen. */
	virtual void GetNodeChildren(TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>& OutChildren) {}
};

struct FLevelSnapshotsEditorFilterRowGroup : public FLevelSnapshotsEditorFilterRow, public TSharedFromThis<FLevelSnapshotsEditorFilterRowGroup>
{
	FLevelSnapshotsEditorFilterRowGroup(ULevelSnapshotEditorFilterGroup* InFilterGroup, const TSharedRef<SLevelSnapshotsEditorFilters>& InOwnerPanel)
		: FilterGroupPtr(InFilterGroup)
		, EditorFiltersPtr(InOwnerPanel)
	{}

	virtual ENodeType GetType() override 
	{
		return ENodeType::Group;
	}

	virtual TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> AsGroup() override;

	ULevelSnapshotEditorFilterGroup* GetFilterGroupObject() const;

private:
	TWeakObjectPtr<ULevelSnapshotEditorFilterGroup> FilterGroupPtr;

	/** This field's owner panel. */
	TWeakPtr<SLevelSnapshotsEditorFilters> EditorFiltersPtr;
};

class SLevelSnapshotsEditorFilterRowGroup : public STableRow<TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>>
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRowGroup)
	{}
	SLATE_END_ARGS()

	void Tick(const FGeometry&, const double, const float);

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorFilters>& OwnerPanel);

};

class SLevelSnapshotsEditorFilters : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorFilters();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilters)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

	const TMap<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>>& GetFilterClasses() const { return FilterClasses; }

	ULevelSnapshotsEditorData* GetEditorData() const;

	TSharedRef<FLevelSnapshotsEditorFilters> GetFiltersModel() const;

	void RemoveFilterRow(TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> InRowGroup);

private:
	/** Generates a tree row. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get this group's children */
	void OnGetGroupChildren(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>& OutRows);

	void OnSelectionChanged(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, ESelectInfo::Type SelectInfo);

	/** Re-create the sections of the view. */
	void Refresh();

	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();

	FReply AddFilterClick();

	void OnSetActiveFilter(ULevelSnapshotFilter* InFilter);

private:
	
	TSharedPtr<SFavoriteFilterList> FavoriteList;
	TSharedPtr<STreeView<TSharedPtr<FLevelSnapshotsEditorFilterRow>>> FilterRowsList;

	TArray<TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>> FilterRowGroups;

	/** Filter input details view*/
	TSharedPtr<IDetailsView> FilterInputDetailsView;

	TMap<UClass*, TSharedPtr<FLevelSnapshotsEditorFilterClass>> FilterClasses;

	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
};
