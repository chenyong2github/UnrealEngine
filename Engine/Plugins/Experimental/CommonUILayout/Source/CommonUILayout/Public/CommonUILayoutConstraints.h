// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUILayoutPanelInfo.h"
#include "kiwi/kiwi.h"
#include "Templates/SubclassOf.h"
#include "Types/SlateEnums.h"

#include "CommonUILayoutConstraints.generated.h"

class UWorld;
class UUserWidget;
class UCommonUILayoutConstraintOverrideBase;
struct FCommonUILayoutPanelSlot;


UENUM()
enum class ECommonUILayoutStrength
{
	Weak,
	Medium,
	Strong,
	Required
};

UENUM()
enum class ECommonUILayoutAnchor
{
	TopLeft,
	TopCenter,
	TopRight,

	CenterLeft,
	CenterCenter,
	CenterRight,

	BottomLeft,
	BottomCenter,
	BottomRight
};

UENUM()
enum class ECommonUILayoutSide
{
	Top,
	Bottom,
	Left,
	Right
};

UENUM()
enum class ECommonUILayoutOperator
{
	Addition		UMETA(DisplayName = "+"),
	Substraction	UMETA(DisplayName = "-")
};

UENUM()
enum class ECommonUILayoutComparison
{
	Equal			UMETA(DisplayName = "="),
	LessOrEqual		UMETA(DisplayName = "<="),
	GreaterOrEqual	UMETA(DisplayName = ">=")
};

UCLASS(Abstract, EditInlineNew, CollapseCategories)
class UCommonUILayoutConstraintBase : public UObject
{
	GENERATED_BODY()

public:
	COMMONUILAYOUT_API void SetInfo(const TSoftClassPtr<UUserWidget>& Widget, const FName& UniqueID, TWeakObjectPtr<UWorld> WorldContextObject);

	COMMONUILAYOUT_API void AddConstraints(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize, TWeakObjectPtr<UWorld> WorldContextObject) const;

protected:

	virtual void AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const PURE_VIRTUAL(UDynamicHUDConstraintBase::AddConstraints_Internal, );

	/** Children info. */
	FCommonUILayoutPanelInfo Info;

	/** Offset applied to the widget after the constraint is resolved. */
	UPROPERTY(EditAnywhere, Category = "Dynamic HUD", meta = (editcondition = "bUseOffset"))
	FVector2D Offset = FVector2D::ZeroVector;

	/** Constraints applied instead of the class defined constraint when the override flag is set and the associated condition is met. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD|Override", Instanced, meta = (editcondition = "bUseOverride"))
	TObjectPtr<UCommonUILayoutConstraintOverrideBase> ConstraintOverride;

	/** Flag used to activate the offset variable. */
	UPROPERTY(EditAnywhere, Category = "Dynamic HUD", meta = (InlineEditConditionToggle))
	uint8 bUseOffset : 1;

	/** Flag used to set a potential override of the current constraint. */
	UPROPERTY(EditAnywhere, Category = "Dynamic HUD|Override", meta = (InlineEditConditionToggle))
	uint8 bUseOverride : 1;

};

UCLASS(MinimalAPI, meta = (DisplayName = "Position"))
class UCommonUILayoutConstraintPosition : public UCommonUILayoutConstraintBase
{
	GENERATED_BODY()

protected:
	virtual void AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const override;

	/** Position where the widget will be located. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	FVector2D Position;

	/** By which anchor should the widget be attached to the position. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	ECommonUILayoutAnchor Anchor = ECommonUILayoutAnchor::TopLeft;
};

UCLASS(MinimalAPI, meta = (DisplayName = "Alignment"))
class UCommonUILayoutConstraintAlignment : public UCommonUILayoutConstraintBase
{
	GENERATED_BODY()

protected:
	virtual void AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const override;

	/** Where on the safe frame should the widget be horizontally aligned. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = EHorizontalAlignment::HAlign_Center;

	/** Where on the safe frame should the widget be vertically aligned. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = EVerticalAlignment::VAlign_Center;

	/** By which anchor should the widget be attached to safe frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	ECommonUILayoutAnchor Anchor = ECommonUILayoutAnchor::TopLeft;
};

USTRUCT()
struct FCommonUILayoutConstraintWidgetFallback
{
	GENERATED_BODY()

	/** Fallback widget to attach to. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	TSoftClassPtr<UUserWidget> TargetWidget;

	/** Optional unique ID of the fallback widget to attach to. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	FName TargetUniqueID;
};

UCLASS(MinimalAPI, meta = (DisplayName = "Widget"))
class UCommonUILayoutConstraintWidget : public UCommonUILayoutConstraintBase
{
	GENERATED_BODY()

protected:
	virtual void AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const override;

	/** By which anchor should the source widget be attached to the target widget. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	ECommonUILayoutAnchor Anchor = ECommonUILayoutAnchor::TopLeft;

	/** Widget to attach to. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	TSoftClassPtr<UUserWidget> TargetWidget;

	/** Optional unique ID of the widget to attach to. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	FName TargetUniqueID;

	/** To which anchor should the source widget be attached on the target widget. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD")
	ECommonUILayoutAnchor TargetAnchor = ECommonUILayoutAnchor::TopLeft;

	/** Fallback targets when the primary target is not found. These will be evaluated in order until a valid target is found. */
	UPROPERTY(EditDefaultsOnly, Category = "Dynamic HUD", meta = (editcondition = "bUseFallbacks"))
	TArray<FCommonUILayoutConstraintWidgetFallback> Fallbacks;

	/** Flag used to activate the fallbacks variable. */
	UPROPERTY(EditAnywhere, Category = "Dynamic HUD", meta = (InlineEditConditionToggle))
	uint8 bUseFallbacks : 1;

	/** Strength of this constraint. The higher the strength, the more likely the solver will fulfill it. WARNING: Required constraints could make the solver unable to find a solution! */
	// FIXME: This variable is hard to understand and never used. Hiding for now.
	//UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = "Dynamic HUD")
	ECommonUILayoutStrength Strength = ECommonUILayoutStrength::Strong;
};
