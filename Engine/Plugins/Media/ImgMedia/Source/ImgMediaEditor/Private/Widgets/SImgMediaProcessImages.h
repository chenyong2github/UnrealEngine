// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImgMediaProcessImagesOptions.h"
#include "Input/Reply.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;

/**
 * SImgMediaProcessImages provides processing of image sequences.
 */
class SImgMediaProcessImages : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImgMediaProcessImages){}
	SLATE_END_ARGS()

	virtual ~SImgMediaProcessImages();

	void Construct(const FArguments& InArgs);

private:
	/** Called when we click on the process images button. */
	FReply OnProcessImagesClicked();

	/** Holds our details view. */
	TSharedPtr<class IDetailsView> DetailsView;
	/** Object that holds our options. */
	TStrongObjectPtr<UImgMediaProcessImagesOptions> Options;
};
