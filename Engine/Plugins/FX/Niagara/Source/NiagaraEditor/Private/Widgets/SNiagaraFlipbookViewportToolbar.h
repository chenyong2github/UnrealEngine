// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SNiagaraFlipbookViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class SNiagaraFlipbookViewportToolbar : public SViewportToolBar//SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SNiagaraFlipbookViewportToolbar) {}
		SLATE_ARGUMENT(TWeakPtr<class FNiagaraFlipbookViewModel>, WeakViewModel)
		SLATE_ARGUMENT(TWeakPtr<class SNiagaraFlipbookViewport>, WeakViewport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedRef<SWidget> GenerateOptionsMenu() const;
	TSharedRef<SWidget> GenerateCameraMenu() const;

private:
	TWeakPtr<class FNiagaraFlipbookViewModel>	WeakViewModel;
	TWeakPtr<class SNiagaraFlipbookViewport>	WeakViewport;
};
