// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "ISessionSourceFilterService.h"

#include "UObject/GCObject.h"
#include "EditorUndoClient.h"

#include "SourceFilterCollection.h"
#include "TreeViewBuilder.h"
#include "DataSourceFilter.h"
#include "TraceSourceFilteringSettings.h"

namespace Trace
{
	class IAnalysisSession;
	typedef uint64 FSessionHandle;
}

class UDataSourceFilter;
class UDataSourceFilterSet;

/** Editor implementation of ISessionSourceFilterService, interfaces directly with Engine level filtering systems and settings */
class FEditorSessionSourceFilterService : public ISessionSourceFilterService, public FGCObject, public FEditorUndoClient
{
public:
	FEditorSessionSourceFilterService();
	virtual ~FEditorSessionSourceFilterService();

	/** Begin ISessionSourceFilterService overrides */
	virtual void PopulateTreeView(FTreeViewDataBuilder& InBuilder) override;	
	virtual void AddFilter(const FString& FilterClassName) override;
	virtual void AddFilterToSet(TSharedRef<const IFilterObject> FilterSet, const FString& FilterClassName) override;
	virtual void AddFilterToSet(TSharedRef<const IFilterObject> FilterSet, TSharedRef<const IFilterObject> ExistingFilter) override;
	virtual void MakeFilterSet(TSharedRef<const IFilterObject> ExistingFilter, EFilterSetMode Mode) override;
	virtual void MakeFilterSet(TSharedRef<const IFilterObject> ExistingFilter, TSharedRef<const IFilterObject> ExistingFilterOther) override;
	virtual void MakeTopLevelFilter(TSharedRef<const IFilterObject> Filter) override;
	virtual void RemoveFilter(TSharedRef<const IFilterObject> InFilter)  override;
	virtual void SetFilterSetMode(TSharedRef<const IFilterObject> InFilter, EFilterSetMode Mode) override;
	virtual void SetFilterState(TSharedRef<const IFilterObject> InFilter, bool bState) override;
	virtual void ResetFilters() override;	
	virtual const FDateTime& GetTimestamp() const override { return Timestamp; }	
	virtual void UpdateFilterSettings(UTraceSourceFilteringSettings* InSettings) override {}
	virtual UTraceSourceFilteringSettings* GetFilterSettings() override;
	virtual bool IsActionPending() const override;
	virtual TSharedRef<SWidget> GetFilterPickerWidget(FOnFilterClassPicked InFilterClassPicked) override;
	virtual TSharedPtr<FExtender> GetExtender() override;
	virtual void GetWorldObjects(TArray<TSharedPtr<FWorldObject>>& OutWorldObjects) override;
	virtual void SetWorldTraceability(TSharedRef<FWorldObject> InWorldObject, bool bState) override;
	virtual const TArray<TSharedPtr<IWorldTraceFilter>>& GetWorldFilters() override;
	/** End ISessionSourceFilterService overrides */	

	/** Begin FEditorUndoClient overrides*/
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	/** End FEditorUndoClient overrides */

	/** Begin FGCObject overrides*/
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	/** End FGCObject overrides */

protected:
	/** Updates the timestamp provided through GetTimestamp, indicating that the contained data has changed  */
	void UpdateTimeStamp();

	/** Callback for whenever the user opts to save the current filtering state as a preset */
	void OnSaveAsPreset();

	/** Callback for whenever any Filter instance blueprint class gets (re)compiled */
	void OnBlueprintCompiled(UBlueprint* InBlueprint);

	/** Helper function to populate FTreeViewDataBuilder with a specific FilterObject */
	TSharedRef<IFilterObject> AddFilterObjectToDataBuilder(UDataSourceFilter* Filter, FTreeViewDataBuilder& InBuilder);

	void SetupWorldFilters();
protected:
	/** Blueprints on which a OnCompiled calllback has been registered, used to refresh data and repopulate the UI */
	TArray<UBlueprint*> DelegateRegisteredBlueprints;

	/** Filter collection this session represents, retrieved from FTraceSourceFiltering */
	USourceFilterCollection* FilterCollection;

	/** Timestamp used to rely data updates to anyone polling against GetTimeStamp */
	FDateTime Timestamp;
	
	/** UI extender, used to insert preset functionality into STraceSourceFilteringWindow */
	TSharedPtr<FExtender> Extender;

	/** World filtering data */
	TMap<uint32, const UWorld*> HashToWorld;
	TArray<TSharedPtr<IWorldTraceFilter>> WorldFilters;

	/** Transaction context naming for all scoped transactions performed in thsi class */
	static FString TransactionContext;

};

#endif // WITH_ENGINE