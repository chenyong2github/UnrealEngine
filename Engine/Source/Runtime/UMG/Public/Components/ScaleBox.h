// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWidget.h"
#include "Components/ContentWidget.h"
#include "Widgets/Layout/SScaleBox.h"
#include "ScaleBox.generated.h"

/**
 * Allows you to place content with a desired size and have it scale to meet the constraints placed on this box's alloted area.  If
 * you needed to have a background image scale to fill an area but not become distorted with different aspect ratios, or if you need
 * to auto fit some text to an area, this is the control for you.
 *
 * * Single Child
 * * Aspect Ratio
 */
UCLASS(config=Engine)
class UMG_API UScaleBox : public UContentWidget
{
	GENERATED_UCLASS_BODY()

public:

	/** The stretching rule to apply when content is stretched */
	UE_DEPRECATED(5.2, "Direct access to Stretch is deprecated. Please use the getter or setter.")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Setter, Getter, BlueprintSetter = "SetStretch", Category = "Stretching")
	TEnumAsByte<EStretch::Type> Stretch;

	/** Controls in what direction content can be scaled */
	UE_DEPRECATED(5.2, "Direct access to StretchDirection is deprecated. Please use the getter or setter.")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Setter, Getter, BlueprintSetter = "SetStretchDirection", Category = "Stretching")
	TEnumAsByte<EStretchDirection::Type> StretchDirection;

	/** Optional scale that can be specified by the User. Used only for UserSpecified stretching. */
	UE_DEPRECATED(5.2, "Direct access to UserSpecifiedScale is deprecated. Please use the getter or setter.")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Setter, Getter, BlueprintSetter = "SetUserSpecifiedScale", Category = "Stretching")
	float UserSpecifiedScale;

	/** Optional bool to ignore the inherited scale. Applies inverse scaling to counteract parents before applying the local scale operation. */
	UE_DEPRECATED(5.2, "Direct access to IgnoreInheritedScale is deprecated. Please use the getter or setter.")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Setter, Getter = "IsIgnoreInheritedScale", BlueprintSetter = "SetIgnoreInheritedScale", Category = "Stretching")
	bool IgnoreInheritedScale;

public:
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetStretch(EStretch::Type InStretch);

	EStretch::Type GetStretch() const;

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetStretchDirection(EStretchDirection::Type InStretchDirection);

	EStretchDirection::Type GetStretchDirection() const;

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetUserSpecifiedScale(float InUserSpecifiedScale);

	float GetUserSpecifiedScale() const;

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetIgnoreInheritedScale(bool bInIgnoreInheritedScale);

	bool IsIgnoreInheritedScale() const;

public:

	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void OnDesignerChanged(const FDesignerChangedEventArgs& EventArgs) override;
#endif

	virtual void Serialize(FArchive& Ar) override;

protected:

	// UPanelWidget
	virtual UClass* GetSlotClass() const override;
	virtual void OnSlotAdded(UPanelSlot* Slot) override;
	virtual void OnSlotRemoved(UPanelSlot* Slot) override;
	// End UPanelWidget

protected:
	TSharedPtr<SScaleBox> MyScaleBox;

#if WITH_EDITOR
	TOptional<FVector2D> DesignerSize;
#endif

protected:
	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget interface
};
