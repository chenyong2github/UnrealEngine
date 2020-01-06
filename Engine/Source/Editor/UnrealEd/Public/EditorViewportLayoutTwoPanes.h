// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "AssetEditorViewportLayout.h"
#include "Widgets/Layout/SSplitter.h"
#include "Types/SlateEnums.h"


template <EOrientation TOrientation>
class TEditorViewportLayoutTwoPanes : public FAssetEditorViewportLayout
{
public:
	/**
	 * Saves viewport layout information between editor sessions
	 */
  	virtual void SaveLayoutString(const FString& LayoutString) const override {}

protected:
	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) override ;


private:
	/** The splitter widget */
	TSharedPtr< class SSplitter > SplitterWidget;
};


// FEditorViewportLayoutTwoPanesVert /////////////////////////////


class FEditorViewportLayoutTwoPanesVert : public TEditorViewportLayoutTwoPanes<EOrientation::Orient_Vertical>
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::TwoPanesVert; }
};


// FEditorViewportLayoutTwoPanesHoriz /////////////////////////////

class FEditorViewportLayoutTwoPanesHoriz : public TEditorViewportLayoutTwoPanes<EOrientation::Orient_Horizontal>
{
public:
	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::TwoPanesHoriz; }
};
