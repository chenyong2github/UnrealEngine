// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

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

	TSharedPtr<class STextBlock> WidthText;
	TSharedPtr<class STextBlock> HeightText;
	TSharedPtr<class STextBlock> TileSizeText;
	TSharedPtr<class STextBlock> TileBorderSizeText;
	TSharedPtr<class STextBlock> RemoveLowMipsText;

	TSharedPtr<class STextBlock> PageTableTextureMemoryText;
	TSharedPtr<class STextBlock> PhysicalTextureMemoryText;
};


/** UI customization for ARuntimeVirtualTexturePlane */
class FRuntimeVirtualTextureActorDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FRuntimeVirtualTextureActorDetailsCustomization();

	/** Callback for Copy Rotation button */
	FReply SetRotation();
	/** Callback for Copy Bounds button */
	FReply SetTransformToBounds();

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	class ARuntimeVirtualTexturePlane* VirtualTexturePlane;
};
