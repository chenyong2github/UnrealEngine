// Copyright Epic Games, Inc. All Rights Reserved.

#include "SActorEditorContext.h"
#include "IActorEditorContextClient.h"
#include "Subsystems/ActorEditorContextSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "ActorEditorContext"

void SActorEditorContext::Construct(const FArguments& InArgs)
{
	World = (InArgs._World);
	GEditor->GetEditorWorldContext().AddRef(World);
	UActorEditorContextSubsystem::Get()->OnActorEditorContextSubsystemChanged().AddSP(this, &SActorEditorContext::Rebuild);
	FEditorDelegates::MapChange.AddSP(this, &SActorEditorContext::OnEditorMapChange);
	Rebuild();
}

SActorEditorContext::~SActorEditorContext()
{
	GEditor->GetEditorWorldContext().RemoveRef(World);
	FEditorDelegates::MapChange.RemoveAll(this);
	UActorEditorContextSubsystem::Get()->OnActorEditorContextSubsystemChanged().RemoveAll(this);
}

void SActorEditorContext::Rebuild()
{
	TArray<IActorEditorContextClient*> Clients = UActorEditorContextSubsystem::Get()->GetDisplayableClients();
	if (Clients.Num() > 0 && World)
	{
		TSharedPtr<SVerticalBox> VBox;
		ChildSlot.Padding(2, 2, 2, 2)
		[
			SAssignNew(VBox, SVerticalBox)
		];

		for (IActorEditorContextClient* Client : Clients)
		{
			FActorEditorContextClientDisplayInfo Info;
			Client->GetActorEditorContextDisplayInfo(World, Info);

			VBox->AddSlot()
				.AutoHeight()
				.Padding(0, 2)
			[
				SNew(SBorder)
				.Visibility_Lambda([Client, this]()
				{ 
					FActorEditorContextClientDisplayInfo Info;
					return Client->GetActorEditorContextDisplayInfo(World, Info) ? EVisibility::Visible : EVisibility::Collapsed; 
				})
				.BorderImage(FCoreStyle::Get().GetBrush("Docking.Sidebar.Border"))
				.Content()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 1.0f, 2.0f, 1.0f)
						[
							SNew(SImage)
							.Image(Info.Brush ? Info.Brush : FStyleDefaults::GetNoBrush())
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 1.0f, 2.0f, 1.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Info.Title))
							.ShadowOffset(FVector2D(1, 1))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.HAlign(HAlign_Right)
							.Cursor(EMouseCursor::Default)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(0)
							.Visibility_Lambda([Client, this]() { return (Client && Client->CanResetContext(World)) ? EVisibility::Visible : EVisibility::Collapsed; })
							.ToolTipText(FText::Format(LOCTEXT("ResetActorEditorContextTooltip", "Reset {0}"), FText::FromString(Info.Title)))
							.OnClicked_Lambda([Client, this]()
							{
								if (Client && Client->CanResetContext(World))
								{
									UActorEditorContextSubsystem::Get()->ResetContext(Client);
								}
								return FReply::Handled();
							})
							.Content()
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8, 2, 0, 2)
					[
						World ? Client->GetActorEditorContextWidget(World) : SNullWidget::NullWidget
					]
				]
			];
		}
	}
	else
	{
		ChildSlot[SNullWidget::NullWidget];
	}
}

#undef LOCTEXT_NAMESPACE