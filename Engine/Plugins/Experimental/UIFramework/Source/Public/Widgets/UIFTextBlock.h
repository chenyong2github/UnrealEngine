// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Text/TextLayout.h"
#include "UIFWidget.h"

#include "UIFTextBlock.generated.h"

class UTextBlock;

/**
 *
 */
UCLASS(DisplayName = "TextBlock UIFramework")
class UIFRAMEWORK_API UUIFrameworkTextBlock : public UUIFrameworkWidget
{
	GENERATED_BODY()

public:
	UUIFrameworkTextBlock();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetText(FText Text);

	UFUNCTION(BlueprintCallable, Category = "UI Framework")
	FText GetText() const
	{
		return Text;
	}
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetJustification(ETextJustify::Type Justification);

	UFUNCTION(BlueprintCallable, Category = "UI Framework")
	ETextJustify::Type GetJustification() const
	{
		return Justification;
	}

	virtual void LocalOnUMGWidgetCreated() override;

private:
	UFUNCTION()
	void OnRep_Text();

	UFUNCTION()
	void OnRep_Justification();

private:
	UPROPERTY(/*ExposeOnSpawn, */ReplicatedUsing=OnRep_Text)
	FText Text;

	UPROPERTY(ReplicatedUsing=OnRep_Justification)
	TEnumAsByte<ETextJustify::Type> Justification;
};
