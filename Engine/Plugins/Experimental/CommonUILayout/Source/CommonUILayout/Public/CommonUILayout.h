// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUILayoutConstraints.h"
#include "CommonUILayoutZOrder.h"
#include "Engine/DataAsset.h"

#include "CommonUILayout.generated.h"

class UUserWidget;
struct FPropertyChangedChainEvent;

USTRUCT()
struct FCommonUILayoutWidget 
{
	GENERATED_BODY()

	/** Widget to allow on screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (AllowAbstract = "false"))
	TSoftClassPtr<UUserWidget> Widget;

	/** Z order used to draw this widget. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout")
	ECommonUILayoutZOrder ZOrder = ECommonUILayoutZOrder::Middle;

	/** Custom z order used to draw this widget. (Higher numbered z order widget are drawn in front of lower numbered one) */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (EditCondition = "ZOrder == EDynamicHUDZOrder::Custom"))
	int32 CustomZOrder = (int32)ECommonUILayoutZOrder::Custom;

	/** Flag to use the unique ID system. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (InlineEditConditionToggle))
	bool bIsUnique = false;

	/** Unique id is used to make this widget a separate instance from other widget of the same class. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (EditCondition = "bIsUnique"))
	FName UniqueID;

	/** Is this widget using the safe zone? */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout")
	bool bUseSafeZone = true;

	/** Layout constraint to apply to this widget when putting it on screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", Instanced)
	TObjectPtr<UCommonUILayoutConstraintBase> LayoutConstraint = nullptr;
};

USTRUCT()
struct FCommonUILayoutWidgetUnallowed
{
	GENERATED_BODY()

	/** Widget to unallow. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (AllowAbstract = "false"))
	TSoftClassPtr<UUserWidget> Widget;

	/** Flag to use the unique ID system. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (InlineEditConditionToggle))
	bool bUseUniqueID = false;

	/** Will only unallow the widget using this unique ID & class. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout", meta = (EditCondition = "bUseUniqueID"))
	FName UniqueID;

	/** Will unallow all widget matching this class regardless of their unique ID usage. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout")
	bool bIncludeAll = false;
};

/**
 * A Layout defines a list of widgets that are allowed & unallowed to be
 * visible when this scene is added to the active layouts stack in the LayoutManager.
 *
 * A widget needs to be allowed at least once to be visible. However, adding a
 * widget in the unallowed list will prevent it from being visible regardless of how many
 * other active scenes allowed it to be. A widget that is not allowed or unallowed
 * will not be allowed to be visible.
 */
UCLASS(BlueprintType)
class COMMONUILAYOUT_API UCommonUILayout : public UDataAsset
{
	GENERATED_BODY()

public:
	/** List of widgets that are allowed to be on screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout")
	TArray<FCommonUILayoutWidget> Widgets;

	/** List of widgets that are unallowed to be on screen. */
	UPROPERTY(EditDefaultsOnly, Category = "Common UI Layout")
	TArray<FCommonUILayoutWidgetUnallowed> UnallowedWidgets;

private:
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
