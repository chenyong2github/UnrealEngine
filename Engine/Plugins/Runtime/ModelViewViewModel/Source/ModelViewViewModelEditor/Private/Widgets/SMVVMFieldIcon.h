// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Types/MVVMFieldVariant.h"
#include "Widgets/SCompoundWidget.h"

class SLayeredImage;

class SMVVMFieldIcon : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMFieldIcon) {}
		SLATE_ARGUMENT(UE::MVVM::FMVVMConstFieldVariant, Field)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void RefreshBinding(const UE::MVVM::FMVVMConstFieldVariant& Field);

private:
	TSharedPtr<SLayeredImage> LayeredImage;
};