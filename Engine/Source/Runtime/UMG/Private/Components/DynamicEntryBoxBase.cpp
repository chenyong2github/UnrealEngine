// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Components/DynamicEntryBoxBase.h"
#include "UMGPrivate.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Editor/WidgetCompilerLog.h"

#define LOCTEXT_NAMESPACE "UMG"

UDynamicEntryBoxBase::UDynamicEntryBoxBase(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, EntryWidgetPool(*this)
{
	Visibility = ESlateVisibility::SelfHitTestInvisible;
	EntrySizeRule.SizeRule = ESlateSizeRule::Automatic;
}

void UDynamicEntryBoxBase::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	EntryWidgetPool.ReleaseSlateResources();
	MyPanelWidget.Reset();
}

void UDynamicEntryBoxBase::ResetInternal(bool bDeleteWidgets)
{
	EntryWidgetPool.ReleaseAll(bDeleteWidgets);

	if (MyPanelWidget.IsValid())
	{
		switch (EntryBoxType)
		{
		case EDynamicBoxType::Horizontal:
		case EDynamicBoxType::Vertical:
			StaticCastSharedPtr<SBoxPanel>(MyPanelWidget)->ClearChildren();
			break;
		case EDynamicBoxType::Wrap:
			StaticCastSharedPtr<SWrapBox>(MyPanelWidget)->ClearChildren();
			break;
		case EDynamicBoxType::Overlay:
			StaticCastSharedPtr<SOverlay>(MyPanelWidget)->ClearChildren();
			break;
		}
	}
}

const TArray<UUserWidget*>& UDynamicEntryBoxBase::GetAllEntries() const
{
	return EntryWidgetPool.GetActiveWidgets();
}

int32 UDynamicEntryBoxBase::GetNumEntries() const
{
	return EntryWidgetPool.GetActiveWidgets().Num();
}

void UDynamicEntryBoxBase::RemoveEntryInternal(UUserWidget* EntryWidget)
{
	if (EntryWidget)
	{
		if (MyPanelWidget.IsValid())
		{
			TSharedPtr<SWidget> CachedEntryWidget = EntryWidget->GetCachedWidget();
			if (CachedEntryWidget.IsValid())
			{
				switch (EntryBoxType)
				{
				case EDynamicBoxType::Horizontal:
				case EDynamicBoxType::Vertical:
					StaticCastSharedPtr<SBoxPanel>(MyPanelWidget)->RemoveSlot(CachedEntryWidget.ToSharedRef());
					break;
				case EDynamicBoxType::Wrap:
					StaticCastSharedPtr<SWrapBox>(MyPanelWidget)->RemoveSlot(CachedEntryWidget.ToSharedRef());
					break;
				case EDynamicBoxType::Overlay:
					StaticCastSharedPtr<SOverlay>(MyPanelWidget)->RemoveSlot(CachedEntryWidget.ToSharedRef());
					break;
				}
			}
		}
		EntryWidgetPool.Release(EntryWidget);
	}
}

void UDynamicEntryBoxBase::SetEntrySpacing(const FVector2D& InEntrySpacing)
{
	EntrySpacing = InEntrySpacing;

	if (MyPanelWidget.IsValid())
	{
		if (EntryBoxType == EDynamicBoxType::Wrap)
		{
			// Wrap boxes can change their widget spacing on the fly
			StaticCastSharedPtr<SWrapBox>(MyPanelWidget)->SetInnerSlotPadding(EntrySpacing);
		}
		else if (EntryBoxType == EDynamicBoxType::Overlay)
		{
			TPanelChildren<SOverlay::FOverlaySlot>* OverlayChildren = static_cast<TPanelChildren<SOverlay::FOverlaySlot>*>(MyPanelWidget->GetChildren());
			for (int32 ChildIdx = 0; ChildIdx < OverlayChildren->Num(); ++ChildIdx)
			{
				FMargin Padding;
				if (SpacingPattern.Num() > 0)
				{
					FVector2D Spacing(0.f, 0.f);

					// First establish the starting location
					for (int32 CountIdx = 0; CountIdx < ChildIdx; ++CountIdx)
					{
						int32 PatternIdx = CountIdx % SpacingPattern.Num();
						Spacing += SpacingPattern[PatternIdx];
					}
					
					// Negative padding is no good, so negative spacing is expressed as positive spacing on the opposite side
					if (Spacing.X >= 0.f)
					{
						Padding.Left = Spacing.X;
					}
					else
					{
						Padding.Right = -Spacing.X;
					}
					if (Spacing.Y >= 0.f)
					{
						Padding.Top = Spacing.Y;
					}
					else
					{
						Padding.Bottom = -Spacing.Y;
					}
				}
				else
				{
					if (EntrySpacing.X >= 0.f)
					{
						Padding.Left = ChildIdx * EntrySpacing.X;
					}
					else
					{
						Padding.Right = ChildIdx * -EntrySpacing.X;
					}

					if (EntrySpacing.Y >= 0.f)
					{
						Padding.Top = ChildIdx * EntrySpacing.Y;
					}
					else
					{
						Padding.Bottom = ChildIdx * -EntrySpacing.Y;
					}
				}
				SOverlay::FOverlaySlot& OverlaySlot = (*OverlayChildren)[ChildIdx];
				OverlaySlot.SlotPadding = Padding;
			}
		}
		else
		{
			// Vertical & Horizontal have to manually update the padding on each slot
			const bool bIsHBox = EntryBoxType == EDynamicBoxType::Horizontal;
			TPanelChildren<SBoxPanel::FSlot>* BoxChildren = static_cast<TPanelChildren<SBoxPanel::FSlot>*>(MyPanelWidget->GetChildren());
			for (int32 ChildIdx = 0; ChildIdx < BoxChildren->Num(); ++ChildIdx)
			{
				const bool bIsFirstChild = ChildIdx == 0;

				FMargin Padding;
				Padding.Top = bIsHBox || bIsFirstChild ? 0.f : EntrySpacing.Y;
				Padding.Left = bIsHBox && !bIsFirstChild ? EntrySpacing.X : 0.f;

				SBoxPanel::FSlot& BoxSlot = (*BoxChildren)[ChildIdx];
				BoxSlot.SlotPadding = Padding;
			}
		}
	}
}

#if WITH_EDITOR

const FText UDynamicEntryBoxBase::GetPaletteCategory()
{
	return LOCTEXT("Advanced", "Advanced");
}
#endif

TSharedRef<SWidget> UDynamicEntryBoxBase::RebuildWidget()
{
	TSharedPtr<SWidget> EntryBoxWidget;
	switch (EntryBoxType)
	{
	case EDynamicBoxType::Horizontal:
		EntryBoxWidget = SAssignNew(MyPanelWidget, SHorizontalBox);
		break;
	case EDynamicBoxType::Vertical:
		EntryBoxWidget = SAssignNew(MyPanelWidget, SVerticalBox);
		break;
	case EDynamicBoxType::Wrap:
		EntryBoxWidget = SAssignNew(MyPanelWidget, SWrapBox)
			.UseAllottedWidth(true)
			.InnerSlotPadding(EntrySpacing);
		break;
	case EDynamicBoxType::Overlay:
		EntryBoxWidget = SAssignNew(MyPanelWidget, SOverlay)
			.Clipping(EWidgetClipping::ClipToBounds);
		break;
	}

	if (!IsDesignTime())
	{
		// Populate now with all the entries that have been created so far
		for (UUserWidget* ActiveWidget : EntryWidgetPool.GetActiveWidgets())
		{
			AddEntryChild(*ActiveWidget);
		}
	}

	return EntryBoxWidget.ToSharedRef();
}

#if WITH_EDITOR
void UDynamicEntryBoxBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (MyPanelWidget.IsValid() && PropertyChangedEvent.GetPropertyName() == TEXT("EntryBoxType"))
	{
		MyPanelWidget.Reset();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDynamicEntryBoxBase::SynchronizeProperties()
{
	Super::SynchronizeProperties();

#if WITH_EDITORONLY_DATA
	if (IsDesignTime())
	{
		SetEntrySpacing(EntrySpacing);
	}
#endif
}

UUserWidget* UDynamicEntryBoxBase::CreateEntryInternal(TSubclassOf<UUserWidget> InEntryClass)
{
	UUserWidget* NewEntryWidget = EntryWidgetPool.GetOrCreateInstance(InEntryClass);
	if (MyPanelWidget.IsValid())
	{
		// If we've already been constructed, immediately add the child to our panel widget
		AddEntryChild(*NewEntryWidget);
	}
	return NewEntryWidget;
}

FMargin UDynamicEntryBoxBase::BuildEntryPadding(const FVector2D& DesiredSpacing)
{
	FMargin EntryPadding;
	if (DesiredSpacing.X >= 0.f)
	{
		EntryPadding.Left = DesiredSpacing.X;
	}
	else
	{
		EntryPadding.Right = -DesiredSpacing.X;
	}

	if (DesiredSpacing.Y >= 0.f)
	{
		EntryPadding.Top = DesiredSpacing.Y;
	}
	else
	{
		EntryPadding.Bottom = -DesiredSpacing.Y;
	}

	return EntryPadding;
}

void UDynamicEntryBoxBase::AddEntryChild(UUserWidget& ChildWidget)
{
	FSlotBase* NewSlot = nullptr;
	if (EntryBoxType == EDynamicBoxType::Wrap)
	{
		NewSlot = &StaticCastSharedPtr<SWrapBox>(MyPanelWidget)->AddSlot()
			.FillEmptySpace(false)
			.HAlign(EntryHorizontalAlignment)
			.VAlign(EntryVerticalAlignment);
	}
	else if (EntryBoxType == EDynamicBoxType::Overlay)
	{
		const int32 ChildIdx = MyPanelWidget->GetChildren()->Num();
		SOverlay::FOverlaySlot& OverlaySlot = (SOverlay::FOverlaySlot&)StaticCastSharedPtr<SOverlay>(MyPanelWidget)->AddSlot();

		EHorizontalAlignment HAlign = EntryHorizontalAlignment;
		EVerticalAlignment VAlign = EntryVerticalAlignment;

		FVector2D TargetSpacing = FVector2D::ZeroVector;
		if (SpacingPattern.Num() > 0)
		{
			for (int32 CountIdx = 0; CountIdx < ChildIdx; ++CountIdx)
			{
				const int32 PatternIdx = CountIdx % SpacingPattern.Num();
				TargetSpacing += SpacingPattern[PatternIdx];
			}
		}
		else
		{
			TargetSpacing = EntrySpacing * ChildIdx;
			HAlign = EntrySpacing.X >= 0.f ? EHorizontalAlignment::HAlign_Left : EHorizontalAlignment::HAlign_Right;
			VAlign = EntrySpacing.Y >= 0.f ? EVerticalAlignment::VAlign_Top : EVerticalAlignment::VAlign_Bottom;
		}
		
		OverlaySlot.HAlignment = HAlign;
		OverlaySlot.VAlignment = VAlign;
		OverlaySlot.SlotPadding = BuildEntryPadding(TargetSpacing);

		NewSlot = &OverlaySlot;
	}
	else
	{
		const bool bIsHBox = EntryBoxType == EDynamicBoxType::Horizontal;
		const bool bIsFirstChild = MyPanelWidget->GetChildren()->Num() == 0;

		SBoxPanel::FSlot& BoxPanelSlot = bIsHBox ? (SBoxPanel::FSlot&)StaticCastSharedPtr<SHorizontalBox>(MyPanelWidget)->AddSlot().MaxWidth(MaxElementSize) : (SBoxPanel::FSlot&)StaticCastSharedPtr<SVerticalBox>(MyPanelWidget)->AddSlot().MaxHeight(MaxElementSize);
		BoxPanelSlot.HAlignment = EntryHorizontalAlignment;
		BoxPanelSlot.VAlignment = EntryVerticalAlignment;
		BoxPanelSlot.SizeParam = UWidget::ConvertSerializedSizeParamToRuntime(EntrySizeRule);

		FMargin Padding;
		Padding.Top = bIsHBox || bIsFirstChild ? 0.f : EntrySpacing.Y;
		Padding.Left = bIsHBox && !bIsFirstChild ? EntrySpacing.X : 0.f;
		BoxPanelSlot.SlotPadding = Padding;

		NewSlot = &BoxPanelSlot;
	}

	if (ensure(NewSlot))
	{
		NewSlot->AttachWidget(ChildWidget.TakeWidget());
	}
}

#undef LOCTEXT_NAMESPACE