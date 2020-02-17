// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FDataprepSchemaActionContext;
struct FTextBlockStyle;

/**
 * The SDataprepActionBlock is the base class that is used for the block that compose a dataprep action
 */
class SDataprepActionBlock : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDataprepActionBlock) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, const TSharedRef<FDataprepSchemaActionContext>& DataprepActionContext);

	// SWidget Interface
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();
		return FVector2D( 280.f, ChildSize.Y );
	}
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// End of SWidget Interface

public:

	/** Return the block title */
	TSharedRef<SWidget> GetBlockTitleWidget();

#ifndef NO_BLUEPRINT
	// Temporary boolean to indicate if the widget has been instantiated in a regular BP graph or the simplified one
	bool bIsSimplifiedGraph;
#endif

protected:

	/**
	* Return the outline color
	*/
	virtual FLinearColor GetOutlineColor() const;

	/**
	 * Return the block title.
	 * This is used in the implementation of SDataprepActionBlock::GetTitleWidget
	 */
	virtual FText GetBlockTitle() const;

	/**
	 * Return the title widget
	 */
 	virtual TSharedRef<SWidget> GetTitleWidget();

	/**
	 * Get title background widget
	 */
	virtual TSharedRef<SWidget> GetTitleBackgroundWidget();

	/**
	 * Return the content widget
	 */
	virtual TSharedRef<SWidget> GetContentWidget();

	/**
	 * Get content background widget
	 */
	virtual TSharedRef<SWidget> GetContentBackgroundWidget();

	/**
	 * Populate the right click menu by overriding this function.
	 * Call the parent implementation if you want the full menu.
	 */
	virtual void PopulateMenuBuilder(class FMenuBuilder& MenuBuilder);

private:

	/**
	 * Delete this step
	 */
	void DeleteStep();

	TSharedPtr<FDataprepSchemaActionContext> DataprepActionContext;
};
