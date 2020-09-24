// Copyright Epic Games, Inc. All Rights Reserved.
 
#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingTypes.h"

class STextBlock;
class SUniformGridPanel;

class DMXPIXELMAPPINGEDITORWIDGETS_API SDMXPixelMappingScreenLayout 
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingScreenLayout) 
		: _NumXPanels(1)
		, _NumYPanels(1)
		, _Distribution(EDMXPixelsDistribution::TopLeftToRight)
		, _PixelFormat(EDMXPixelFormat::PF_RGB)
		, _bShowAddresses(false)
		, _bShowUniverse(false)
		, _RemoteUniverse(1)
		, _StartAddress(1)
	{}
		
		SLATE_ARGUMENT(int32, NumXPanels)

		SLATE_ARGUMENT(int32, NumYPanels)

		SLATE_ARGUMENT(EDMXPixelsDistribution, Distribution)

		SLATE_ARGUMENT(EDMXPixelFormat, PixelFormat)
		
		SLATE_ARGUMENT(bool, bShowAddresses)

		SLATE_ARGUMENT(bool, bShowUniverse)

		SLATE_ARGUMENT(int32, RemoteUniverse)

		SLATE_ARGUMENT(int32, StartAddress)

		SLATE_ATTRIBUTE(const FSlateBrush*, Brush)

	SLATE_END_ARGS()

	//~ Begin SCompoundWidget interface
	void Construct(const FArguments& InArgs);
	//~ Begin SCompoundWidget interface

private:
	int32 NumXPanels;

	int32 NumYPanels;

	EDMXPixelsDistribution Distribution;

	EDMXPixelFormat PixelFormat;

	bool bShowAddresses;

	bool bShowUniverse;

	int32 RemoteUniverse;

	int32 StartAddress;

	TAttribute<const FSlateBrush*> Brush;

	TSharedPtr<STextBlock> InfoTextBlock;

	TSharedPtr<SUniformGridPanel> GridPanel;

	TArray<TPair<int32, int32>> UnorderedList;

	TArray<TPair<int32, int32>> SortedList;
};

class DMXPIXELMAPPINGEDITORWIDGETS_API SDMXPixelMappingSimpleScreenLayout
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingSimpleScreenLayout)
		: _NumXPanels(1)
		, _NumYPanels(1)
		, _RemoteUniverse(1)
		, _StartAddress(1)
	{}

		SLATE_ARGUMENT(int32, NumXPanels)

		SLATE_ARGUMENT(int32, NumYPanels)

		SLATE_ARGUMENT(int32, RemoteUniverse)

		SLATE_ARGUMENT(int32, StartAddress)

		SLATE_ATTRIBUTE(const FSlateBrush*, Brush)

	SLATE_END_ARGS()

	//~ Begin SCompoundWidget interface
	void Construct(const FArguments& InArgs);
	//~ Begin SCompoundWidget interface

private:
	int32 RemoteUniverse;

	int32 StartAddress;

	int32 NumXPanels;

	int32 NumYPanels;

	TAttribute<const FSlateBrush*> Brush;
};

class DMXPIXELMAPPINGEDITORWIDGETS_API SDMXPixelMappingPixel
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingPixel)
		: _PixelIndex(1)
	{}

		SLATE_ARGUMENT(int32, PixelIndex)

		SLATE_ATTRIBUTE(const FSlateBrush*, Brush)

	SLATE_END_ARGS()

	//~ Begin SCompoundWidget interface
	void Construct(const FArguments& InArgs);
	//~ Begin SCompoundWidget interface

public:
	TAttribute<const FSlateBrush*> Brush;

private:
	int32 PixelIndex;
};

