// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"

#include "LensFile.h"
#include "UObject/StrongObjectPtr.h"


/**
 * Placeholder for camera calibration tools tab
 */
class SNodalOffsetToolPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNodalOffsetToolPanel) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, ULensFile* InLensFile);


private:

	/** Holds the preset asset. */
	TStrongObjectPtr<ULensFile> LensFile;
};
