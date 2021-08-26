// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRigSpacePickerWidget.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "SRigHierarchyTreeView.h"

#define LOCTEXT_NAMESPACE "SRigSpacePickerWidget"

//////////////////////////////////////////////////////////////
/// SRigSpacePickerWidget
///////////////////////////////////////////////////////////

void SRigSpacePickerWidget::Construct(const FArguments& InArgs)
{
	Hierarchy = InArgs._Hierarchy;
	ControlKey = InArgs._SelectedControl;
	
	ChildSlot
	[
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		[
			SAssignNew(ListBox, SVerticalBox)
			
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0, 0.0, 0.0, 2.0)
			[
				SNew( STextBlock )
				.Text( LOCTEXT("Space", "Space") )
				.Font( IDetailLayoutBuilder::GetDetailFontBold() )
			]
		]
	];

	AddSpacePickerButton(
		FEditorStyle::GetBrush("EditorViewport.RelativeCoordinateSystem_Local"),
		LOCTEXT("Parent", "Parent"),
		FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleLocalSpaceClicked)
	);
	
	AddSpacePickerButton(
		FEditorStyle::GetBrush("EditorViewport.RelativeCoordinateSystem_World"),
		LOCTEXT("World", "World"),
		FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleWorldSpaceClicked)
	);

	if(const FRigControlElement* Control = Hierarchy->Find<FRigControlElement>(ControlKey))
	{
		TArray<FRigElementKey> Favorites = Control->Settings.SpaceFavorites;

		for(const FRigElementKey& Key : Favorites)
		{
			if(Key.IsValid() && Hierarchy->Contains(Key))
			{
				AddSpacePickerButton(
					SRigHierarchyItem::GetBrushForElementType(Hierarchy, Key),
					FText::FromName(Key.Name),
					FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleElementSpaceClicked, Key)
				);
			}
		}
	}
}

SRigSpacePickerWidget::~SRigSpacePickerWidget()
{
}

class SRigSpacePickerWindow : public SWindow
{
public:

	virtual bool OnIsActiveChanged(const FWindowActivateEvent& ActivateEvent) override
	{
		if(ActivateEvent.GetActivationType() == FWindowActivateEvent::EA_Deactivate)
		{
			DeactivatedDelegate.ExecuteIfBound();
		}
		return SWindow::OnIsActiveChanged(ActivateEvent);
	}

	FSimpleDelegate& OnDeactivated() { return DeactivatedDelegate; }

private:

	FSimpleDelegate DeactivatedDelegate;
}; 

SRigSpacePickerWidget::FResult SRigSpacePickerWidget::InvokeDialog()
{
	PickedKey = FRigElementKey();
	
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	TSharedRef<SRigSpacePickerWindow> Window = SNew(SRigSpacePickerWindow)
	.Title( LOCTEXT("SRigSpacePickerWidgetPickSpace", "Pick a new space") )
	.CreateTitleBar(false)
	.Type(EWindowType::Menu)
	.SizingRule( ESizingRule::Autosized )
	.ScreenPosition(CursorPos)
	.FocusWhenFirstShown(true)
	.ActivationPolicy(EWindowActivationPolicy::FirstShown)
	[
		AsShared()
	];
	
	Window->SetWidgetToFocusOnActivate(AsShared());
	Window->OnDeactivated().BindSP(this, &SRigSpacePickerWidget::CloseDialog);
	
	PickerWindow = Window;

	Window->MoveWindowTo(CursorPos);
	GEditor->EditorAddModalWindow(Window);

	FResult Result;
	Result.Reply = PickedKey.IsValid() ? FReply::Handled() : FReply::Unhandled();
	Result.Key = PickedKey;
	return Result;
}

void SRigSpacePickerWidget::CloseDialog()
{
	if ( PickerWindow.IsValid() )
	{
		PickerWindow.Pin()->RequestDestroyWindow();
	}
}

FReply SRigSpacePickerWidget::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		PickedKey = FRigElementKey();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SRigSpacePickerWidget::AddSpacePickerButton(const FSlateBrush* InBush, const FText& InTitle,
	FOnClicked OnClickedDelegate)
{
	ListBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Fill)
	.Padding(24.f, 0.0f, 0.0f, 2.0f)
	[
		SNew( SButton )
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnClicked(OnClickedDelegate)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew(SImage)
				.Image(InBush)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew( STextBlock )
				.Text( InTitle )
				.Font( IDetailLayoutBuilder::GetDetailFont() )
			]

			+ SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
		]
	];
}

FReply SRigSpacePickerWidget::HandleLocalSpaceClicked()
{
	const FRigElementKey DefaultParent = Hierarchy->GetFirstParent(ControlKey);
	return HandleElementSpaceClicked(DefaultParent);
}

FReply SRigSpacePickerWidget::HandleWorldSpaceClicked()
{
	return HandleElementSpaceClicked(Hierarchy->GetWorldSpaceSocketKey());
}

FReply SRigSpacePickerWidget::HandleElementSpaceClicked(FRigElementKey InKey)
{
	PickedKey = InKey;
	CloseDialog();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
