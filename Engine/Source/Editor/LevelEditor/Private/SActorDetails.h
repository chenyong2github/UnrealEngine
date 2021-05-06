// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Containers/ArrayView.h"
#include "EditorUndoClient.h"
#include "Elements/Interfaces/TypedElementDetailsInterface.h"

class AActor;
class FSCSEditorTreeNode;
class FTabManager;
class FUICommandList;
class IDetailsView;
class SBox;
class SSCSEditor;
class SSplitter;
class UBlueprint;
class FDetailsViewObjectFilter;
class UTypedElementSelectionSet;

/**
 * Wraps a details panel customized for viewing actors
 */
class SActorDetails : public SCompoundWidget, public FEditorUndoClient, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SActorDetails) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UTypedElementSelectionSet* InSelectionSet, const FName TabIdentifier, TSharedPtr<FUICommandList> InCommandList, TSharedPtr<FTabManager> InTabManager);
	~SActorDetails();

	/**
	 * Return true if this details panel is observing the given selection set.
	 */
	bool IsObservingSelectionSet(const UTypedElementSelectionSet* InSelectionSet) const;

	/**
	 * Update the view based on our observed selection set.
	 */
	void RefreshSelection(const bool bForceRefresh = false);

	/**
	 * Update the view based on the given set of actors.
	 */
	void OverrideSelection(const TArray<AActor*>& InActors, const bool bForceRefresh = false);

	/** FEditorUndoClient Interface */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	
	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	/**
	 * Sets the filter that should be used to filter incoming actors in or out of the details panel
	 *
	 * @param InFilter	The filter to use or nullptr to remove the active filter
	 */
	void SetActorDetailsRootCustomization(TSharedPtr<FDetailsViewObjectFilter> ActorDetailsObjectFilter, TSharedPtr<class IDetailRootObjectCustomization> ActorDetailsRootCustomization);

	/** Sets the UI customization of the SCSEditor inside this details panel. */
	void SetSCSEditorUICustomization(TSharedPtr<class ISCSEditorUICustomization> ActorDetailsSCSEditorUICustomization);

private:
	void RefreshTopLevelElements(TArrayView<const TTypedElement<UTypedElementDetailsInterface>> InDetailsElements, const bool bForceRefresh, const bool bOverrideLock);
	void RefreshSCSTreeElements(TArrayView<const TSharedPtr<class FSCSEditorTreeNode>> InSelectedNodes, const bool bForceRefresh, const bool bOverrideLock);
	void SetElementDetailsObjects(TArrayView<const TUniquePtr<ITypedElementDetailsObject>> InElementDetailsObjects, const bool bForceRefresh, const bool bOverrideLock);

	AActor* GetActorContext() const;
	bool GetAllowComponentTreeEditing() const;

	void OnComponentsEditedInWorld();
	void OnSCSEditorTreeViewSelectionChanged(const TArray<TSharedPtr<class FSCSEditorTreeNode> >& SelectedNodes);
	void OnSCSEditorTreeViewItemDoubleClicked(const TSharedPtr<class FSCSEditorTreeNode> ClickedNode);
	void OnSCSEditorTreeViewObjectReplaced();
	void UpdateComponentTreeFromEditorSelection();

	bool IsPropertyReadOnly(const struct FPropertyAndParent& PropertyAndParent) const;
	bool IsPropertyEditingEnabled() const;
	EVisibility GetComponentsBoxVisibility() const;
	EVisibility GetUCSComponentWarningVisibility() const;
	EVisibility GetInheritedBlueprintComponentWarningVisibility() const;
	EVisibility GetNativeComponentWarningVisibility() const;
	void OnBlueprintedComponentWarningHyperlinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata);
	void OnNativeComponentWarningHyperlinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata);

	void AddBPComponentCompileEventDelegate(UBlueprint* ComponentBlueprint);
	void RemoveBPComponentCompileEventDelegate();
	void OnBlueprintComponentCompiled(UBlueprint* ComponentBlueprint);
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementObjects);

private:
	TSharedPtr<SSplitter> DetailsSplitter;
	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<SBox> ComponentsBox;
	TSharedPtr<class SSCSEditor> SCSEditor;

	// The selection set this details panel is observing
	UTypedElementSelectionSet* SelectionSet = nullptr;

	// The selection override, if any
	bool bHasSelectionOverride = false;
	TArray<AActor*> SelectionOverrideActors;

	// Array of top-level elements that are currently being edited
	TArray<TUniquePtr<ITypedElementDetailsObject>> TopLevelElements;

	// Array of component elements that are being edited from the SCS tree selection
	TArray<TUniquePtr<ITypedElementDetailsObject>> SCSTreeElements;

	// The current component blueprint selection
	TWeakObjectPtr<UBlueprint> SelectedBPComponentBlueprint;
	bool bSelectedComponentRecompiled = false;

	// Used to prevent reentrant changes
	bool bSelectionGuard = false;
};
