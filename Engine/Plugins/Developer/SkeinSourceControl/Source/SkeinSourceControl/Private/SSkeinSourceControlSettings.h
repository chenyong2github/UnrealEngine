// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateFwd.h"
#include "ISourceControlOperation.h"
#include "ISourceControlProvider.h"

class SSkeinSourceControlSettings : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SSkeinSourceControlSettings) {}
	
	SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs);

private:

	EVisibility CanUseSkeinCLI() const;
	EVisibility CanUseSkeinProject() const;
};
