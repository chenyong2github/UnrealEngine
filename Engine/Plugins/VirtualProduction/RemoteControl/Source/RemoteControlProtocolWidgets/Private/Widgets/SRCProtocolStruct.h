// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FProtocolBindingViewModel;

class REMOTECONTROLPROTOCOLWIDGETS_API SRCProtocolStruct : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolStruct)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FProtocolBindingViewModel>& InViewModel);

private:
	TSharedPtr<FProtocolBindingViewModel> ViewModel;

	TSharedRef<SWidget> CreateStructureDetailView();
};
