// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/Extensions/IGeometryExtension.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/Extensions/ISortableExtension.h"

namespace UE
{
namespace Sequencer
{

class SEQUENCERCORE_API FOutlinerSpacer
	: public FViewModel
	, public FGeometryExtensionShim
	, public FOutlinerExtensionShim
	, public ISortableExtension
	, public IOutlinerDropTargetOutlinerExtension
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FOutlinerSpacer, FViewModel, IGeometryExtension, IOutlinerExtension, ISortableExtension, IOutlinerDropTargetOutlinerExtension);

	FOutlinerSpacer(float InDesiredSpacerHeight);
	~FOutlinerSpacer();

	void SetDesiredHeight(float InDesiredSpacerHeight)
	{
		DesiredSpacerHeight = InDesiredSpacerHeight;
	}

public:

	/*~ IOutlinerExtension */
	bool HasBackground() const override;
	FName GetIdentifier() const override;
	FOutlinerSizing GetOutlinerSizing() const override;
	TSharedRef<SWidget> CreateOutlinerView(const FCreateOutlinerViewParams& InParams) override;
	TSharedPtr<SWidget> CreateContextMenuWidget(const FCreateOutlinerContextMenuWidgetParams& InParams) override;

	/*~ ISortableExtension */
	virtual void SortChildren() override;
	virtual FSortingKey GetSortingKey() const override;
	virtual void SetCustomOrder(int32 InCustomOrder) override;

	/*~ IOutlinerDropTargetOutlinerExtension */
	TOptional<EItemDropZone> CanAcceptDrop(const FViewModelPtr& TargetModel, const FDragDropEvent& DragDropEvent, EItemDropZone InItemDropZone) override;
	void PerformDrop(const FViewModelPtr& TargetModel, const FDragDropEvent& DragDropEvent, EItemDropZone InItemDropZone) override;

private:

	float DesiredSpacerHeight;
	int32 CustomOrder;
};

} // namespace Sequencer
} // namespace UE

