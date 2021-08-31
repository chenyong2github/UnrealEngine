// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUILayoutConstraints.h"
#include "CommonUILayoutPanelSlot.h"
#include "CommonUILayoutConstraintOverride.h"

#include "Blueprint/UserWidget.h"
#include "Widgets/SWidget.h"

namespace
{
	double ConvertStrength(const ECommonUILayoutStrength Strength)
	{
		switch (Strength)
		{
			case ECommonUILayoutStrength::Weak:			return kiwi::strength::weak;
			case ECommonUILayoutStrength::Medium:		return kiwi::strength::medium;
			case ECommonUILayoutStrength::Strong:		return kiwi::strength::strong;
			case ECommonUILayoutStrength::Required:		return kiwi::strength::required;
		}

		return kiwi::strength::weak;
	}

	FVector2D ConvertAnchorToVector2D(const ECommonUILayoutAnchor Anchor)
	{
		switch (Anchor)
		{
			case ECommonUILayoutAnchor::TopLeft:		return FVector2D(0.0f, 0.0f);
			case ECommonUILayoutAnchor::TopCenter:		return FVector2D(0.5f, 0.0f);
			case ECommonUILayoutAnchor::TopRight:		return FVector2D(1.0f, 0.0f);

			case ECommonUILayoutAnchor::CenterLeft:		return FVector2D(0.0f, 0.5f);
			case ECommonUILayoutAnchor::CenterCenter:	return FVector2D(0.5f, 0.5f);
			case ECommonUILayoutAnchor::CenterRight:	return FVector2D(1.0f, 0.5f);

			case ECommonUILayoutAnchor::BottomLeft:		return FVector2D(0.0f, 1.0f);
			case ECommonUILayoutAnchor::BottomCenter:	return FVector2D(0.5f, 1.0f);
			case ECommonUILayoutAnchor::BottomRight:	return FVector2D(1.0f, 1.0f);
		}

		return FVector2D();
	}

	FVector2D ConvertAlignmentToVector2D(const EHorizontalAlignment HorizontalAlignment, const EVerticalAlignment VerticalAlignment)
	{
		FVector2D ConvertedValue(0.0f, 0.0f);

		switch (HorizontalAlignment)
		{
			case HAlign_Fill:		ConvertedValue.X = 0.0f;	break;
			case HAlign_Left:		ConvertedValue.X = 0.0f;	break;
			case HAlign_Center:		ConvertedValue.X = 0.5f;	break;
			case HAlign_Right:		ConvertedValue.X = 1.0f;	break;
		}

		switch (VerticalAlignment)
		{
			case VAlign_Fill:		ConvertedValue.Y = 0.0f;	break;
			case VAlign_Top:		ConvertedValue.Y = 0.0f;	break;
			case VAlign_Center:		ConvertedValue.Y = 0.5f;	break;
			case VAlign_Bottom:		ConvertedValue.Y = 1.0f;	break;
		}

		return ConvertedValue;
	}

	float ConvertSideToOffset(const ECommonUILayoutSide Side)
	{
		switch (Side)
		{
			case ECommonUILayoutSide::Top:		return 0.0f;
			case ECommonUILayoutSide::Bottom:	return 1.0f;
			case ECommonUILayoutSide::Left:		return 0.0f;
			case ECommonUILayoutSide::Right:	return 1.0f;
		}

		return 0.0f;
	}

	bool AreSidesCompatible(const ECommonUILayoutSide A, const ECommonUILayoutSide B)
	{
		return ((A == ECommonUILayoutSide::Top || A == ECommonUILayoutSide::Bottom) && (B == ECommonUILayoutSide::Top || B == ECommonUILayoutSide::Bottom)) ||
			((A == ECommonUILayoutSide::Left || A == ECommonUILayoutSide::Right) && (B == ECommonUILayoutSide::Left || B == ECommonUILayoutSide::Right));
	}

	kiwi::Expression GetExpression(const FCommonUILayoutPanelSlot* Slot, const ECommonUILayoutSide Side, const float Offset)
	{
		const SWidget& SlotWidget = Slot->GetWidget().Get();
		const FVector2D Size = SlotWidget.GetDesiredSize();

		switch (Side)
		{
			case ECommonUILayoutSide::Left:
			case ECommonUILayoutSide::Right:
				return Slot->Left + ((Size.X * ConvertSideToOffset(Side)) + Offset);
				break;

			case ECommonUILayoutSide::Top:
			case ECommonUILayoutSide::Bottom:
				return Slot->Top + ((Size.Y * ConvertSideToOffset(Side)) + Offset);
				break;
		}

		return kiwi::Expression();
	}
}

void UCommonUILayoutConstraintBase::SetInfo(const TSoftClassPtr<UUserWidget>& Widget, const FName& UniqueID, TWeakObjectPtr<UWorld> WorldContextObject)
{
	Info = FCommonUILayoutPanelInfo(Widget, UniqueID);
	if (bUseOverride && ConstraintOverride)
	{
		ConstraintOverride->SetInfo(Widget, UniqueID, WorldContextObject);
	}
}

void UCommonUILayoutConstraintBase::AddConstraints(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize, TWeakObjectPtr<UWorld> WorldContextObject) const
{
	if (bUseOverride && ConstraintOverride && ConstraintOverride->TryApplyOverride(Solver, Children, AllottedGeometrySize, WorldContextObject))
	{
		return;
	}
	AddConstraints_Internal(Solver, Children, AllottedGeometrySize);
}

void UCommonUILayoutConstraintPosition::AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const
{
	if (FCommonUILayoutPanelSlot* ChildSlot = Children.FindRef(Info))
	{
		const FVector2D ChildDesiredSize = ChildSlot->GetWidget().Get().GetDesiredSize();
		const FVector2D AnchorAdjustments = ConvertAnchorToVector2D(Anchor);
		const FVector2D AdjustedPosition = Position - (ChildDesiredSize * AnchorAdjustments);

		ChildSlot->Left.setValue(AdjustedPosition.X);
		ChildSlot->Top.setValue(AdjustedPosition.Y);

		Solver.addConstraint(kiwi::Constraint(ChildSlot->Left == AdjustedPosition.X) | ConvertStrength(ECommonUILayoutStrength::Required));
		Solver.addConstraint(kiwi::Constraint(ChildSlot->Top == AdjustedPosition.Y) | ConvertStrength(ECommonUILayoutStrength::Required));

		ChildSlot->bAreConstraintsDirty = true;
	}
}

void UCommonUILayoutConstraintAlignment::AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const
{
	if (FCommonUILayoutPanelSlot* ChildSlot = Children.FindRef(Info))
	{
		const FVector2D ChildDesiredSize = ChildSlot->GetWidget().Get().GetDesiredSize();
		const FVector2D ChildAdjustedSize(
			HorizontalAlignment == EHorizontalAlignment::HAlign_Fill ? AllottedGeometrySize.X : ChildDesiredSize.X,
			VerticalAlignment == EVerticalAlignment::VAlign_Fill ? AllottedGeometrySize.Y : ChildDesiredSize.Y);

		const FVector2D AlignmentAdjustments = ConvertAlignmentToVector2D(HorizontalAlignment, VerticalAlignment);
		const FVector2D AnchorAdjustments = ConvertAnchorToVector2D(Anchor);
		const float Left = HorizontalAlignment == EHorizontalAlignment::HAlign_Fill ? 0.0f : (AllottedGeometrySize.X * AlignmentAdjustments.X) - (ChildAdjustedSize.X * AnchorAdjustments.X);
		const float Top = VerticalAlignment == EVerticalAlignment::VAlign_Fill ? 0.0f : (AllottedGeometrySize.Y * AlignmentAdjustments.Y) - (ChildAdjustedSize.Y * AnchorAdjustments.Y);

		ChildSlot->Left.setValue(Left);
		ChildSlot->Top.setValue(Top);

		Solver.addConstraint(kiwi::Constraint(ChildSlot->Left == Left) | ConvertStrength(ECommonUILayoutStrength::Required));
		Solver.addConstraint(kiwi::Constraint(ChildSlot->Top == Top) | ConvertStrength(ECommonUILayoutStrength::Required));

		ChildSlot->bAreConstraintsDirty = true;
		ChildSlot->SetAdjustedSize(ChildAdjustedSize);
	}
}

void UCommonUILayoutConstraintWidget::AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const
{
	FCommonUILayoutPanelSlot* SourceChildSlot = Children.FindRef(Info);
	FCommonUILayoutPanelSlot* TargetChildSlot = Children.FindRef(FCommonUILayoutPanelInfo(TargetWidget, TargetUniqueID));
	if (SourceChildSlot && TargetChildSlot)
	{
		const FVector2D SourceChildSize = SourceChildSlot->GetWidget().Get().GetDesiredSize();
		const FVector2D SourceAnchorOffset = SourceChildSize * ConvertAnchorToVector2D(Anchor);

		const FVector2D TargetChildSize = TargetChildSlot->GetWidget().Get().GetDesiredSize();
		const FVector2D TargetAnchorOffset = TargetChildSize * ConvertAnchorToVector2D(TargetAnchor);

		Solver.addConstraint(kiwi::Constraint(SourceChildSlot->Left + SourceAnchorOffset.X == TargetChildSlot->Left + TargetAnchorOffset.X) | ConvertStrength(Strength));
		Solver.addConstraint(kiwi::Constraint(SourceChildSlot->Top + SourceAnchorOffset.Y == TargetChildSlot->Top + TargetAnchorOffset.Y) | ConvertStrength(Strength));

		SourceChildSlot->bAreConstraintsDirty = true;
		TargetChildSlot->bAreConstraintsDirty = true;
	}
}

void UCommonUILayoutConstraintComparison::AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const
{
	if (!AreSidesCompatible(Side, TargetSide))
	{
		// TODO: Show error to user
		return;
	}

	FCommonUILayoutPanelSlot* SourceChildSlot = Children.FindRef(Info);
	FCommonUILayoutPanelSlot* TargetChildSlot = Children.FindRef(FCommonUILayoutPanelInfo(TargetWidget, TargetUniqueID));
	if (SourceChildSlot && TargetChildSlot)
	{
		const kiwi::Expression SourceExpression = GetExpression(SourceChildSlot, Side, Offset);
		const kiwi::Expression TargetExpression = GetExpression(TargetChildSlot, TargetSide, TargetOffset);

		switch (Comparison)
		{
			case ECommonUILayoutComparison::Equal:
				Solver.addConstraint(kiwi::Constraint(SourceExpression == TargetExpression) | ConvertStrength(Strength));
				break;

			case ECommonUILayoutComparison::LessOrEqual:
				Solver.addConstraint(kiwi::Constraint(SourceExpression <= TargetExpression) | ConvertStrength(Strength));
				break;

			case ECommonUILayoutComparison::GreaterOrEqual:
				Solver.addConstraint(kiwi::Constraint(SourceExpression >= TargetExpression) | ConvertStrength(Strength));
				break;
		}

		SourceChildSlot->bAreConstraintsDirty = true;
		TargetChildSlot->bAreConstraintsDirty = true;
	}
}

void UCommonUILayoutConstraintEquation::AddConstraints_Internal(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize) const
{
	if (!AreSidesCompatible(Side, TargetSide))
	{
		// TODO: Show error to user
		return;
	}

	FCommonUILayoutPanelSlot* SourceChildSlot = Children.FindRef(Info);
	FCommonUILayoutPanelSlot* TargetChildSlot = Children.FindRef(FCommonUILayoutPanelInfo(TargetWidget, TargetUniqueID));
	if (SourceChildSlot && TargetChildSlot)
	{
		const kiwi::Expression SourceExpression = GetExpression(SourceChildSlot, Side, 0.0f);
		const kiwi::Expression TargetExpression = GetExpression(TargetChildSlot, TargetSide, 0.0f);

		kiwi::Expression ExpressionWithOperation;
		switch (Operator)
		{
			case ECommonUILayoutOperator::Addition:
				ExpressionWithOperation = SourceExpression + TargetExpression;
				break;

			case ECommonUILayoutOperator::Substraction:
				ExpressionWithOperation = SourceExpression - TargetExpression;
				break;
		}

		const kiwi::Expression ExpressionResult(Result);
		switch (Comparison)
		{
			case ECommonUILayoutComparison::Equal:
				Solver.addConstraint(kiwi::Constraint(ExpressionWithOperation == Result) | ConvertStrength(Strength));
				break;

			case ECommonUILayoutComparison::LessOrEqual:
				Solver.addConstraint(kiwi::Constraint(ExpressionWithOperation <= Result) | ConvertStrength(Strength));
				break;

			case ECommonUILayoutComparison::GreaterOrEqual:
				Solver.addConstraint(kiwi::Constraint(ExpressionWithOperation >= Result) | ConvertStrength(Strength));
				break;
		}

		SourceChildSlot->bAreConstraintsDirty = true;
		TargetChildSlot->bAreConstraintsDirty = true;
	}
}
