// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SNiagaraBakerViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class SNiagaraBakerViewportToolbar : public SViewportToolBar//SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SNiagaraBakerViewportToolbar) {}
		SLATE_ARGUMENT(TWeakPtr<class FNiagaraBakerViewModel>, WeakViewModel)
		SLATE_ARGUMENT(TWeakPtr<class SNiagaraBakerViewport>, WeakViewport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedRef<SWidget> GenerateOptionsMenu() const;
	TSharedRef<SWidget> GenerateCameraMenu() const;

private:
	TWeakPtr<class FNiagaraBakerViewModel>	WeakViewModel;
	TWeakPtr<class SNiagaraBakerViewport>	WeakViewport;
};
