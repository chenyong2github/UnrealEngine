// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPathHelpers.h"
#include "Styling/CoreStyle.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Types/MVVMFieldVariant.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SImage;
class STextBlock;

class SMVVMSourceEntry : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMSourceEntry) :
		_TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT(TOptional<UE::MVVM::FBindingSource>, Source)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void RefreshSource(const TOptional<UE::MVVM::FBindingSource>& Source);

private:
	TSharedPtr<STextBlock> Label;
	TSharedPtr<SImage> Image;
};