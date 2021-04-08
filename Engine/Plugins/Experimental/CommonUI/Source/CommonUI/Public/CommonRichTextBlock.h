// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/RichTextBlock.h"
#include "Styling/SlateBrush.h"

#include "CommonRichTextBlock.generated.h"

class STextScroller;
class UCommonTextStyle;
class UCommonTextScrollStyle;

// Various ways that we display inline icon that have an icon-name association
UENUM(BlueprintType)
enum class ERichTextInlineIconDisplayMode : uint8
{
	// Only show the icon - use when space is limited
	IconOnly,
	// Only show the text - use seldom if ever
	TextOnly,
	// Show both the icon and the text - use whenever there is space
	IconAndText,
	MAX
};


UCLASS()
class COMMONUI_API UCommonRichTextBlock : public URichTextBlock
{
	GENERATED_BODY()

public:
	ETextTransformPolicy GetTextTransformPolicy() const { return TextTransformPolicy; }
	TSubclassOf<UCommonTextStyle> GetDefaultTextStyleClass() const { return DefaultTextStyleOverrideClass; }
	float GetMobileTextBlockScale() const { return MobileTextBlockScale; }

	virtual void Serialize(FArchive& Ar) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void SetText(const FText& InText) override;

#if WITH_EDITOR
	virtual void OnCreationFromPalette() override;
	const FText GetPaletteCategory() override;
	virtual bool CanEditChange(const FProperty* InProperty) const;
#endif

	UPROPERTY(EditAnywhere, Category = InlineIcon)
	ERichTextInlineIconDisplayMode InlineIconDisplayMode;

	/** Toggle it on if the text color should also tint the inline icons. */
	UPROPERTY(EditAnywhere, Category = InlineIcon)
	bool bTintInlineIcon = false;
	static FString EscapeStringForRichText(FString InString);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	virtual void CreateDecorators(TArray<TSharedRef<ITextDecorator>>& OutDecorators) override;

	virtual void ApplyUpdatedDefaultTextStyle() override;

private:
	void RefreshOverrideStyle();
	void ApplyTextBlockScale() const;

private:
	UPROPERTY(EditAnywhere, Category = Appearance, meta = (EditCondition = bOverrideDefaultStyle))
	TSubclassOf<UCommonTextStyle> DefaultTextStyleOverrideClass;

	/** Mobile font size multiplier. Activated by default on mobile. See CVar Mobile_PreviewFontSize */
	UPROPERTY(EditAnywhere, Category = "Mobile", meta = (ClampMin = "0.01", ClampMax = "5"))
	float MobileTextBlockScale = 1.0f;

	/** References the scroll style asset to use, no reference disables scrolling*/
	UPROPERTY(EditAnywhere, Category = Appearance)
	TSubclassOf<UCommonTextScrollStyle> ScrollStyle;

	/** True to always display text in ALL CAPS */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "bDisplayAllCaps is deprecated. Please use TextTransformPolicy instead."))
	bool bDisplayAllCaps_DEPRECATED = false;

	TSharedPtr<STextScroller> MyTextScroller;
};
