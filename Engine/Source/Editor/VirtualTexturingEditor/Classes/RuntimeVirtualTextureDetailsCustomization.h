// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Reply.h"

/** UI customization for URuntimeVirtualTexture */
class FRuntimeVirtualTextureDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FRuntimeVirtualTextureDetailsCustomization();

	/** Callback for updating values after and edit. */
	void RefreshDetails();

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	class URuntimeVirtualTexture* VirtualTexture;

	TSharedPtr<class STextBlock> TileCountText;
	TSharedPtr<class STextBlock> TileSizeText;
	TSharedPtr<class STextBlock> TileBorderSizeText;

	TSharedPtr<class STextBlock> SizeText;
};


/** UI customization for URuntimeVirtualTextureComponent */
class FRuntimeVirtualTextureComponentDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FRuntimeVirtualTextureComponentDetailsCustomization();

	/** Returns true if SetBounds button is enabled */
	bool IsSetBoundsEnabled() const;
	/** Callback for Set Bounds button */
	FReply SetBounds();

	/** Callback for Build button */
	FReply BuildStreamedMips();
	/** Callback for Build Debug button */
	FReply BuildLowMipsDebug();
	/** Shared implementation for Build callbacks */
	FReply BuildStreamedMipsInternal(bool bDebug);

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	class URuntimeVirtualTextureComponent* RuntimeVirtualTextureComponent;
};
