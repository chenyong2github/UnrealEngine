// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AMediaPlate;

/**
 * Implements the details panel of the MediaPlate asset editor.
 */
class SMediaPlateEditorDetails
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaPlateEditorDetails) { }
	SLATE_END_ARGS()

public:

	/**
	 * Construct this widget.
	 *
	 * @param InArgs		The declaration data for this widget.
	 * @param InMediaPlate	The MediaPlate to show the details for.
	 * @param InStyleSet	The style set to use.
	 */
	void Construct(const FArguments& InArgs, AMediaPlate& InMediaPlate, const TSharedRef<ISlateStyle>& InStyle);

private:

	/** Pointer to the MediaPlate that is being viewed. */
	TObjectPtr<AMediaPlate> MediaPlate;
};
