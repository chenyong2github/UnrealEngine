// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SSplitter.h"
#include "AssetEditorViewportLayout.h"

class FEditorViewportLayout2x2 : public FAssetEditorViewportLayout
{
public:

	virtual const FName& GetLayoutTypeName() const override { return EditorViewportConfigurationNames::FourPanes2x2; }
protected:
	/**
	 * Creates the viewports and splitter for the 2x2 layout                   
	 */
	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) override;

private:
	/** The splitter widget */
	TSharedPtr< class SSplitter2x2 > SplitterWidget;
};
