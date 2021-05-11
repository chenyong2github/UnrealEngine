// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UWrapBox

UWrapBox::UWrapBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsVariable = false;
	Visibility = ESlateVisibility::SelfHitTestInvisible;
	WrapSize = 500;
	bExplicitWrapSize = false;
	HorizontalAlignment = HAlign_Left;
	Orientation = EOrientation::Orient_Horizontal;
}

void UWrapBox::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (WrapWidth_DEPRECATED != 0.0f)
	{
		WrapSize = WrapWidth_DEPRECATED;
		WrapWidth_DEPRECATED = 0.0f;
	}

	if (bExplicitWrapWidth_DEPRECATED)
	{
		bExplicitWrapSize = bExplicitWrapWidth_DEPRECATED;
		bExplicitWrapWidth_DEPRECATED = false;
	}
#endif
}

void UWrapBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyWrapBox.Reset();
}

UClass* UWrapBox::GetSlotClass() const
{
	return UWrapBoxSlot::StaticClass();
}

void UWrapBox::OnSlotAdded(UPanelSlot* InSlot)
{
	// Add the child to the live canvas if it already exists
	if ( MyWrapBox.IsValid() )
	{
		CastChecked<UWrapBoxSlot>(InSlot)->BuildSlot(MyWrapBox.ToSharedRef());
	}
}

void UWrapBox::OnSlotRemoved(UPanelSlot* InSlot)
{
	// Remove the widget from the live slot if it exists.
	if ( MyWrapBox.IsValid() && InSlot->Content)
	{
		TSharedPtr<SWidget> Widget = InSlot->Content->GetCachedWidget();
		if ( Widget.IsValid() )
		{
			MyWrapBox->RemoveSlot(Widget.ToSharedRef());
		}
	}
}

UWrapBoxSlot* UWrapBox::AddChildWrapBox(UWidget* Content)
{
	return AddChildToWrapBox(Content);
}

UWrapBoxSlot* UWrapBox::AddChildToWrapBox(UWidget* Content)
{
	return Cast<UWrapBoxSlot>(Super::AddChild(Content));
}

TSharedRef<SWidget> UWrapBox::RebuildWidget()
{
	MyWrapBox = SNew(SWrapBox)
		.UseAllottedSize(!bExplicitWrapSize)
		.PreferredSize(WrapSize)
		.HAlign(HorizontalAlignment)
		.Orientation(Orientation);

	for ( UPanelSlot* PanelSlot : Slots )
	{
		if ( UWrapBoxSlot* TypedSlot = Cast<UWrapBoxSlot>(PanelSlot) )
		{
			TypedSlot->Parent = this;
			TypedSlot->BuildSlot(MyWrapBox.ToSharedRef());
		}
	}

	return MyWrapBox.ToSharedRef();
}

void UWrapBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyWrapBox->SetInnerSlotPadding(InnerSlotPadding);
	MyWrapBox->SetUseAllottedSize(!bExplicitWrapSize);
	MyWrapBox->SetWrapSize(WrapSize);
	MyWrapBox->SetHorizontalAlignment(HorizontalAlignment);
	MyWrapBox->SetOrientation(Orientation);
}

void UWrapBox::SetInnerSlotPadding(FVector2D InPadding)
{
	InnerSlotPadding = InPadding;

	if ( MyWrapBox.IsValid() )
	{
		MyWrapBox->SetInnerSlotPadding(InPadding);
	}
}

void UWrapBox::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;

	if (MyWrapBox.IsValid())
	{
		MyWrapBox->SetHorizontalAlignment(InHorizontalAlignment);
	}
}

#if WITH_EDITOR

const FText UWrapBox::GetPaletteCategory()
{
	return LOCTEXT("Panel", "Panel");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
