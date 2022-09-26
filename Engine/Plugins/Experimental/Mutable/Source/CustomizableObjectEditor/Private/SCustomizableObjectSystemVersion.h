// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectSystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class CUSTOMIZABLEOBJECTEDITOR_API SCustomizableObjectSystemVersion : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectSystemVersion)
		{}
	SLATE_END_ARGS()

	/** */
	void Construct( const FArguments& InArgs );

public:

	// SWidget interface
	void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

private:


};

