// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ICastable.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/Extensions/ICurveEditorTreeItemExtension.h"
#include "MVVM/Extensions/IDimmableExtension.h"
#include "MVVM/Extensions/IGeometryExtension.h"
#include "MVVM/Extensions/IHoveredExtension.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/Extensions/IPinnableExtension.h"
#include "MVVM/Extensions/ISoloableExtension.h"
#include "MVVM/Extensions/IMutableExtension.h"
#include "CurveEditorTypes.h"
#include "Tree/ICurveEditorTreeItem.h"

class UMovieSceneSequence;
class FSequencer;

namespace UE
{
namespace Sequencer
{

class FSequencerEditorViewModel;

class SEQUENCER_API FOutlinerItemModel
	: public FViewModel
	, public FOutlinerExtensionShim
	, public FGeometryExtensionShim
	, public FPinnableExtensionShim
	, public FHoveredExtensionShim
	, public IDimmableExtension
	, public ISoloableExtension
	, public IMutableExtension
	, public FCurveEditorTreeItemExtensionShim
	, public ICurveEditorTreeItem
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FOutlinerItemModel, FViewModel, IOutlinerExtension, IGeometryExtension, IPinnableExtension, IHoveredExtension, IDimmableExtension, IMutableExtension, ISoloableExtension, ICurveEditorTreeItemExtension);

	FOutlinerItemModel();

	TSharedPtr<FSequencerEditorViewModel> GetEditor() const;

	bool IsForceFilteredOut() const;
	void SetForceFilteredOut(bool bInIsForcedFilteredOut);
	
	/*~ IOutlinerExtension */
	FName GetIdentifier() const override;
	bool IsExpanded() const override;
	void SetExpansion(bool bInIsExpanded) override;
	bool IsFilteredOut() const override;
	TSharedPtr<SWidget> CreateContextMenuWidget(const FCreateOutlinerContextMenuWidgetParams& InParams) override;

	/*~ ICurveEditorTreeItemExtension */
	virtual bool HasCurves() const override;
	virtual TSharedPtr<ICurveEditorTreeItem> GetCurveEditorTreeItem() const override;

	/*~ ICurveEditorTreeItem */
	virtual TSharedPtr<SWidget> GenerateCurveEditorTreeWidget(const FName& InColumnName, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& InTableRow) override;
	virtual void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override;
	virtual bool PassesFilter(const FCurveEditorTreeFilter* InFilter) const override;

	/*~ IPinnableExtension */
	bool IsPinned() const override;

	/*~ IDimmableExtension */
	bool IsDimmed() const override;

	/*~ ISoloableExtension */
	bool IsSolo() const override;

	/*~ IMutableExtension */
	bool IsMuted() const override;

protected:

	/** Get context menu contents. */
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder);
	virtual void BuildOrganizeContextMenu(FMenuBuilder& MenuBuilder);

	/** Set identifier for computing node paths */
	void SetIdentifier(FName InNewIdentifier);

	/** Get the default expansion state if it wasn't saved in the movie-scene data */
	virtual bool GetDefaultExpansionState() const;

	/** Set expansion state without saving it in the movie-scene data */
	void SetExpansionWithoutSaving(bool bInIsExpanded);

protected:
	FViewModelListHead OutlinerChildList;
private:

	bool IsRootModelPinned() const;
	void ToggleRootModelPinned();

	bool IsSelectedModelsSolo() const;
	void ToggleSelectedModelsSolo();

	bool IsSelectedModelsMuted() const;
	void ToggleSelectedModelsMuted();

private:

	FName TreeItemIdentifier;
	FCurveEditorTreeItemID CurveEditorItemID;
	mutable bool bInitializedExpansion;
	mutable bool bInitializedPinnedState;
	bool bIsForcedFilteredOut;
};

} // namespace Sequencer
} // namespace UE

