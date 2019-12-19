// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "EditorViewportLayout.h"
#include "AssetEditorViewportLayout.h"

class SHorizontalBox;

class FEditorViewportLayoutOnePane : public FAssetEditorViewportLayout
{
public:
	virtual const FName& GetLayoutTypeName() const override{ return EditorViewportConfigurationNames::OnePane; }
protected:
	/**
	* Creates the viewport for the single pane
	*/
	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) override;

protected:
	/** The viewport widget parent box */
	TSharedPtr< SHorizontalBox > ViewportBox;
};
