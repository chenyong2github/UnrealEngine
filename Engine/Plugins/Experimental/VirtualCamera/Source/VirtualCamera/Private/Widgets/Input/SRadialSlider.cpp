// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Input/SRadialSlider.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#if WITH_ACCESSIBILITY
#include "Widgets/Accessibility/SlateAccessibleWidgets.h"
#endif

SRadialSlider::SRadialSlider()
{
#if WITH_ACCESSIBILITY
	AccessibleBehavior = EAccessibleBehavior::Summary;
	bCanChildrenBeAccessible = false;
#endif
}

void SRadialSlider::Construct( const SRadialSlider::FArguments& InDeclaration )
{
	check(InDeclaration._Style);
	Style = InDeclaration._Style;

	IndentHandle = InDeclaration._IndentHandle;
	bMouseUsesStep = InDeclaration._MouseUsesStep;
	bRequiresControllerLock = InDeclaration._RequiresControllerLock;
	LockedAttribute = InDeclaration._Locked;
	StepSize = InDeclaration._StepSize;
	ValueAttribute = InDeclaration._Value;
	MinValue = InDeclaration._MinValue;
	MaxValue = InDeclaration._MaxValue;
	SliderHandleStartAngle = InDeclaration._SliderHandleStartAngle;
	SliderHandleEndAngle = InDeclaration._SliderHandleEndAngle;
	AngularOffset = InDeclaration._AngularOffset;
	ValueRemapCurve = InDeclaration._ValueRemapCurve;
	SliderBarColor = InDeclaration._SliderBarColor;
	SliderHandleColor = InDeclaration._SliderHandleColor;
	bIsFocusable = InDeclaration._IsFocusable;
	OnMouseCaptureBegin = InDeclaration._OnMouseCaptureBegin;
	OnMouseCaptureEnd = InDeclaration._OnMouseCaptureEnd;
	OnControllerCaptureBegin = InDeclaration._OnControllerCaptureBegin;
	OnControllerCaptureEnd = InDeclaration._OnControllerCaptureEnd;
	OnValueChanged = InDeclaration._OnValueChanged;

	bControllerInputCaptured = false;
}

int32 SRadialSlider::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	const float AllottedWidth = AllottedGeometry.GetLocalSize().X;
	const float AllottedHeight = AllottedGeometry.GetLocalSize().Y;
	   	 
	FGeometry SliderGeometry = AllottedGeometry;

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// Draw radial slider bar
	const FVector2D HandleSize = GetThumbImage()->ImageSize;
	const FVector2D HalfHandleSize = 0.5f * HandleSize;
	const float SliderRadius = FMath::Min(AllottedWidth, AllottedHeight) * 0.5f - HalfHandleSize.Y;
	FVector2D StartPoint(0.0f, SliderRadius);

	TArray<FVector2D> CirclePoints;
	static const int32 CircleResolution = 100;
	for (int32 i = 0; i <= CircleResolution; i++)
	{
		const float CurrentPointAngle = FMath::Lerp(SliderHandleStartAngle, SliderHandleEndAngle, (float(i) / float(CircleResolution)));
		CirclePoints.Emplace(StartPoint.GetRotated(CurrentPointAngle + AngularOffset));
	}

	const FVector2D SliderMidPoint(AllottedGeometry.GetLocalSize() * 0.5f);
	const FVector2D SliderDiameter(SliderRadius * 2.0f);
	auto BarImage = GetBarImage();

	FSlateDrawElement::MakeLines
	(
		OutDrawElements,
		LayerId,
		SliderGeometry.ToPaintGeometry(SliderMidPoint, SliderDiameter),
		CirclePoints,
		DrawEffects,
		BarImage->GetTint(InWidgetStyle) * SliderBarColor.Get().GetColor(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint(),
		true,
		Style->BarThickness
	);

	++LayerId;	

	// Draw slider thumb
	FVector2D SliderHandleLocation(StartPoint);
	const float SliderPercent = FMath::Clamp(GetNormalizedValue(), 0.0f, 1.0f);
	const float SliderHandleCurrentAngle = FMath::Lerp(SliderHandleStartAngle, SliderHandleEndAngle, SliderPercent);
	SliderHandleLocation = SliderHandleLocation.GetRotated(SliderHandleCurrentAngle + AngularOffset);

	const FVector2D HandleTopLeftPoint = SliderHandleLocation + (AllottedGeometry.GetLocalSize() * 0.5f) - HalfHandleSize;

	auto ThumbImage = GetThumbImage();
	FSlateDrawElement::MakeRotatedBox(
		OutDrawElements,
		LayerId,
		SliderGeometry.ToPaintGeometry(HandleTopLeftPoint, GetThumbImage()->ImageSize),
		ThumbImage,
		DrawEffects,
		(180.0f + SliderHandleCurrentAngle + AngularOffset) * (PI / 180.f),
		HalfHandleSize,
		FSlateDrawElement::RelativeToElement,
		ThumbImage->GetTint(InWidgetStyle) * SliderHandleColor.Get().GetColor(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
	);

	return LayerId;
}

FVector2D SRadialSlider::ComputeDesiredSize( float ) const
{
	static const FVector2D SRadialSliderDesiredSize(16.0f, 16.0f);

	if ( Style == nullptr )
	{
		return SRadialSliderDesiredSize;
	}

	const float Thickness = FMath::Max(
		Style->BarThickness, 
		FMath::Max(Style->NormalThumbImage.ImageSize.Y, Style->HoveredThumbImage.ImageSize.Y)
	);

	return FVector2D(SRadialSliderDesiredSize.X, Thickness);
}

bool SRadialSlider::IsLocked() const
{
	return LockedAttribute.Get();
}

bool SRadialSlider::IsInteractable() const
{
	return IsEnabled() && !IsLocked() && SupportsKeyboardFocus();
}

bool SRadialSlider::SupportsKeyboardFocus() const
{
	return bIsFocusable;
}

void SRadialSlider::ResetControllerState()
{
	if (bControllerInputCaptured)
	{
		OnControllerCaptureEnd.ExecuteIfBound();
		bControllerInputCaptured = false;
	}
}

FNavigationReply SRadialSlider::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (bControllerInputCaptured || !bRequiresControllerLock)
	{
		FNavigationReply Reply = FNavigationReply::Escape();

		float NewValue = ValueAttribute.Get();
		if (InNavigationEvent.GetNavigationType() == EUINavigation::Left)
		{
			NewValue -= StepSize.Get();
			Reply = FNavigationReply::Stop();
		}
		else if (InNavigationEvent.GetNavigationType() == EUINavigation::Right)
		{
			NewValue += StepSize.Get();
			Reply = FNavigationReply::Stop();
		}
		if (ValueAttribute.Get() != NewValue)
		{
			CommitValue(FMath::Clamp(NewValue, MinValue, MaxValue));
			return Reply;
		}
	}

	return SLeafWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

FReply SRadialSlider::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FReply::Unhandled();

	if (IsInteractable())
	{
		// The controller's bottom face button must be pressed once to begin manipulating the slider's value.
		// Navigation away from the widget is prevented until the button has been pressed again or focus is lost.
		// The value can be manipulated by using the game pad's directional arrows ( relative to slider orientation ).
		if (FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept && bRequiresControllerLock)
		{
			if (bControllerInputCaptured == false)
			{
				// Begin capturing controller input and allow user to modify the slider's value.
				bControllerInputCaptured = true;
				OnControllerCaptureBegin.ExecuteIfBound();
				Reply = FReply::Handled();
			}
			else
			{
				ResetControllerState();
				Reply = FReply::Handled();
			}
		}
		else
		{
			Reply = SLeafWidget::OnKeyDown(MyGeometry, InKeyEvent);
		}
	}
	else
	{
		Reply = SLeafWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	return Reply;
}

FReply SRadialSlider::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FReply Reply = FReply::Unhandled();
	if (bControllerInputCaptured)
	{
		Reply = FReply::Handled();
	}
	return Reply;
}

void SRadialSlider::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	if (bControllerInputCaptured)
	{
		// Commit and reset state
		CommitValue(ValueAttribute.Get());
		ResetControllerState();
	}
}

FReply SRadialSlider::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ((MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) && !IsLocked())
	{
		CachedCursor = Cursor.Get().Get(EMouseCursor::Default);
		OnMouseCaptureBegin.ExecuteIfBound();
		CommitValue(PositionToValue(MyGeometry, MouseEvent.GetLastScreenSpacePosition()));
		
		// Release capture for controller/keyboard when switching to mouse.
		ResetControllerState();
		
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SRadialSlider::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ((MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) && HasMouseCaptureByUser(MouseEvent.GetUserIndex(), MouseEvent.GetPointerIndex()))
	{
		SetCursor(CachedCursor);
		OnMouseCaptureEnd.ExecuteIfBound();
		
		// Release capture for controller/keyboard when switching to mouse.
		ResetControllerState();
		
		return FReply::Handled().ReleaseMouseCapture();	
	}

	return FReply::Unhandled();
}

FReply SRadialSlider::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (HasMouseCaptureByUser(MouseEvent.GetUserIndex(), MouseEvent.GetPointerIndex()) && !IsLocked())
	{
		SetCursor(EMouseCursor::GrabHandClosed);
		CommitValue(PositionToValue(MyGeometry, MouseEvent.GetLastScreenSpacePosition()));
		
		// Release capture for controller/keyboard when switching to mouse
		ResetControllerState();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SRadialSlider::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (!IsLocked())
	{
		// Release capture for controller/keyboard when switching to mouse.
		ResetControllerState();

		PressedScreenSpaceTouchDownPosition = InTouchEvent.GetScreenSpacePosition();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SRadialSlider::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (HasMouseCaptureByUser(InTouchEvent.GetUserIndex(), InTouchEvent.GetPointerIndex()))
	{
		CommitValue(PositionToValue(MyGeometry, InTouchEvent.GetScreenSpacePosition()));

		// Release capture for controller/keyboard when switching to mouse
		ResetControllerState();

		return FReply::Handled();
	}
	else if (!HasMouseCapture())
	{
		if (FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(InTouchEvent, PressedScreenSpaceTouchDownPosition, EOrientation::Orient_Horizontal))
		{
			CachedCursor = Cursor.Get().Get(EMouseCursor::Default);
			OnMouseCaptureBegin.ExecuteIfBound();

			CommitValue(PositionToValue(MyGeometry, InTouchEvent.GetScreenSpacePosition()));

			// Release capture for controller/keyboard when switching to mouse
			ResetControllerState();

			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}

	return FReply::Unhandled();
}

FReply SRadialSlider::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (HasMouseCaptureByUser(InTouchEvent.GetUserIndex(), InTouchEvent.GetPointerIndex()))
	{
		SetCursor(CachedCursor);
		OnMouseCaptureEnd.ExecuteIfBound();

		CommitValue(PositionToValue(MyGeometry, InTouchEvent.GetScreenSpacePosition()));

		// Release capture for controller/keyboard when switching to mouse.
		ResetControllerState();

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

void SRadialSlider::CommitValue(float NewValue)
{
	const float OldValue = GetValue();

	if (!ValueAttribute.IsBound())
	{
		ValueAttribute.Set(NewValue);
	}

	Invalidate(EInvalidateWidgetReason::Paint);

	OnValueChanged.ExecuteIfBound(NewValue);
}

float SRadialSlider::PositionToValue( const FGeometry& MyGeometry, const FVector2D& AbsolutePosition )
{
	const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(AbsolutePosition) - MyGeometry.GetLocalSize() * 0.5f;
	const FVector2D MouseDirection = LocalPosition.GetSafeNormal().GetRotated(90.0f - AngularOffset);

	const float NewAngle = 180.0f + (180.0f / PI * FMath::Atan2(MouseDirection.Y, MouseDirection.X));
	const float NormalizedSliderAlpha = FMath::GetMappedRangeValueClamped(FVector2D(SliderHandleStartAngle, SliderHandleEndAngle), FVector2D(0.0f, 1.0f), NewAngle);

	const float NewValue = FMath::Lerp(MinValue, MaxValue, NormalizedSliderAlpha);
	return NewValue;
}

const FSlateBrush* SRadialSlider::GetBarImage() const
{
	if (!IsEnabled() || LockedAttribute.Get())
	{
		return &Style->DisabledBarImage;
	}
	else if (IsHovered())
	{
		return &Style->HoveredBarImage;
	}
	else
	{
		return &Style->NormalBarImage;
	}
}

const FSlateBrush* SRadialSlider::GetThumbImage() const
{
	if (!IsEnabled() || LockedAttribute.Get())
	{
		return &Style->DisabledThumbImage;
	}
	else if (IsHovered())
	{
		return &Style->HoveredThumbImage;
	}
	else
	{
		return &Style->NormalThumbImage;
	}
}

float SRadialSlider::GetValue() const
{
	return ValueAttribute.Get();
}

float SRadialSlider::GetNormalizedValue() const
{
	if (MaxValue == MinValue)
	{
		return 1.0f;
	}
	else
	{
		return (ValueAttribute.Get() - MinValue) / (MaxValue - MinValue);
	}
}

void SRadialSlider::SetValue(const TAttribute<float>& InValueAttribute)
{
	SetAttribute(ValueAttribute, InValueAttribute, EInvalidateWidgetReason::Paint);
}

void SRadialSlider::SetMinAndMaxValues(float InMinValue, float InMaxValue)
{
	MinValue = InMinValue;
	MaxValue = InMaxValue;
	if (MinValue > MaxValue)
	{
		MaxValue = MinValue;
	}
}

void SRadialSlider::SetSliderHandleStartAngleAndSliderHandleEndAngle(float InSliderHandleStartAngle, float InSliderHandleEndAngle)
{
	SliderHandleStartAngle = InSliderHandleStartAngle;
	SliderHandleEndAngle = InSliderHandleEndAngle;
	if (SliderHandleStartAngle > SliderHandleEndAngle)
	{
		SliderHandleEndAngle = SliderHandleStartAngle;
	}
}

void SRadialSlider::SetIndentHandle(const TAttribute<bool>& InIndentHandle)
{
	SetAttribute(IndentHandle, InIndentHandle, EInvalidateWidgetReason::Paint);
}

void SRadialSlider::SetLocked(const TAttribute<bool>& InLocked)
{
	SetAttribute(LockedAttribute, InLocked, EInvalidateWidgetReason::Paint);
}

void SRadialSlider::SetSliderBarColor(FSlateColor InSliderBarColor)
{
	SetAttribute(SliderBarColor, TAttribute<FSlateColor>(InSliderBarColor), EInvalidateWidgetReason::Paint);
}

void SRadialSlider::SetSliderHandleColor(FSlateColor InSliderHandleColor)
{
	SetAttribute(SliderHandleColor, TAttribute<FSlateColor>(InSliderHandleColor), EInvalidateWidgetReason::Paint);
}

float SRadialSlider::GetStepSize() const
{
	return StepSize.Get();
}

void SRadialSlider::SetStepSize(const TAttribute<float>& InStepSize)
{
	StepSize = InStepSize;
}

void SRadialSlider::SetMouseUsesStep(bool MouseUsesStep)
{
	bMouseUsesStep = MouseUsesStep;
}

void SRadialSlider::SetRequiresControllerLock(bool RequiresControllerLock)
{
	bRequiresControllerLock = RequiresControllerLock;
}

#if WITH_ACCESSIBILITY
TSharedRef<FSlateAccessibleWidget> SRadialSlider::CreateAccessibleWidget()
{
	return MakeShareable<FSlateAccessibleWidget>(new FSlateAccessibleSlider(SharedThis(this)));
}
#endif
