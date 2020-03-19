// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNPSimFrameContents.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SBoxPanel.h"
#include "INetworkPredictionProvider.h"

#define LOCTEXT_NAMESPACE "SNPSimFrameContents"

SNPSimFrameContents::SNPSimFrameContents()
{

}
SNPSimFrameContents::~SNPSimFrameContents()
{

}

void SNPSimFrameContents::Reset()
{

}

void SNPSimFrameContents::Construct(const FArguments& InArgs, TSharedPtr<SNPWindow> InNPWindow)
{
	NPWindow = InNPWindow;
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SAssignNew(ContentsHBoxPtr, SHorizontalBox)
		]
	];

	/*

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SAssignNew(SimInfoTextBlock, STextBlock)
			]
			
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SAssignNew(SimTickTextBlock, STextBlock)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SAssignNew(InputCmdTextBlock, STextBlock)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SAssignNew(SyncStateTextBlock, STextBlock)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SAssignNew(AuxStateTextBlock, STextBlock)
			]
		]
	];
	*/
}

void SNPSimFrameContents::NotifyContentClicked(const FSimContentsView& InContent)
{
	struct FUserStateWidgetInfo
	{
		const FSimulationData::FUserState* State = nullptr;
		const TCHAR* Heading = nullptr;
		FLinearColor Color = FLinearColor::White;
	};

	auto MakeUserStateWidget = [&](const FUserStateWidgetInfo& Info)
	{
		FString HeaderText;
		const TCHAR* UserStr = nullptr;
		uint64 EngineFrameHyperLink = 0;
		if (Info.State)
		{
			// style=\"Hyperlink\"
			HeaderText = FString::Printf(TEXT("%s [Sim: %d. <a id=\"engine\">Engine: %d</>. %s]"), Info.Heading, Info.State->SimFrame, Info.State->EngineFrame, LexToString(Info.State->Source));
			UserStr = Info.State->UserStr;
			EngineFrameHyperLink = Info.State->EngineFrame;
		}
		else
		{
			HeaderText = FString::Printf(TEXT("%s [State not found!]"), Info.Heading);
		}
		
		const FTextBlockStyle& TextStyle = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

		return SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(0.0f, 4.0f, 0.0f, 4.0f)
			[
				/*
				SNew(STextBlock)
				.Text(FText::FromString(HeaderText))
				.ColorAndOpacity(Info.Color)
				*/
				SNew( SRichTextBlock )
				.Text(FText::FromString(HeaderText))
				//.TextStyle(&TextStyle)
				//.AutoWrapText(true)
				//.DecoratorStyleSet( &FEditorStyle::Get() )
				.DecoratorStyleSet( &FCoreStyle::Get() )
				+SRichTextBlock::HyperlinkDecorator( TEXT("engine"), FSlateHyperlinkRun::FOnClick::CreateLambda([this, EngineFrameHyperLink](const FSlateHyperlinkRun::FMetadata& Metadata) { NPWindow->SetEngineFrame(EngineFrameHyperLink, true); } ) )
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SEditableText)
				.IsReadOnly(true)
				.Text(FText::FromString(UserStr))
				.ColorAndOpacity(Info.Color)
			];
	};

	auto AddUserStateVSlots = [&](const TArrayView<const FUserStateWidgetInfo>& Source)
	{
		TSharedPtr<SVerticalBox> VBox;
		ContentsHBoxPtr->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		[
			SAssignNew(VBox, SVerticalBox)
		];

		auto ConditionalMake = [&](const FUserStateWidgetInfo& Info)
		{
			if (Info.State)
			{
				VBox->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				.Padding(4.0f, 4.0f, 4.0f, 4.0f)
				[
					MakeUserStateWidget(Info)
				];
			}
		};

		for (const FUserStateWidgetInfo& Info : Source)
		{
			ConditionalMake(Info);
		}
	};

	ContentsHBoxPtr->ClearChildren();
	
	// -------------------------------------------------------------
	// Simulation Info
	// -------------------------------------------------------------
	FString GroupNameStr = InContent.SimView->ConstData.GroupName.ToString();
	FString NetRoleStr = LexToString(InContent.SimView->SparseData->NetRole);

	FString SimInfoText;
	SimInfoText += FString::Format(TEXT(
		"{0}\n"
		"Sim Group: {1}\n"
		"NetGUID: {2}\n"
		"NetRole: {3}"),
	{
		*InContent.SimView->ConstData.DebugName,
		*GroupNameStr,
		InContent.SimView->ConstData.ID.NetGUID,
		*NetRoleStr
	});

	TSharedPtr<SVerticalBox> VertBox;

	ContentsHBoxPtr->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Left)
	.Padding(4.0f, 4.0f, 4.0f, 4.0f)
	[
		SAssignNew(VertBox, SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(4.0f, 4.0f, 4.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(SimInfoText))
		]
	];

	// -------------------------------------------------------------
	//	Simulation Tick
	// -------------------------------------------------------------
	if (InContent.SimTick)
	{
		FString TickText;

		const FSimulationData::FTick& SimTick = *InContent.SimTick;

		TickText += FString::Format(TEXT(
			"Simulation Frame {0}\n"
			"Start Simulation MS: {1}\n"
			"End Simulation MS: {2}\n"
			"Delta Simulation MS: {3}\n"
			"Repredict: {4}\n"
			"Confirmed Engine Frame: {5}\n"
			"Trashed Engine Frame: {6}\n"
			"GFrameNumber: {7}\n\n"),
		{
			SimTick.OutputFrame,
			SimTick.StartMS,
			SimTick.EndMS,
			(SimTick.EndMS - SimTick.StartMS),
			SimTick.bRepredict,
			SimTick.ConfirmedEngineFrame,
			SimTick.TrashedEngineFrame,
			SimTick.EngineFrame
		});

		VertBox->AddSlot()

		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0f, 4.0f, 4.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TickText))
		];		
		
		const int32 InputFrame = SimTick.OutputFrame - 1;

		const FSimulationData::FUserState* InputCmd = InContent.SimView->UserData.Get(ENP_UserState::Input, InputFrame, InContent.SimTick->EngineFrame, (uint8)ENP_UserStateSource::NetRecv);
		
		const FSimulationData::FUserState* InSyncState = InContent.SimView->UserData.Get(ENP_UserState::Sync, InputFrame, InContent.SimTick->EngineFrame, (uint8)ENP_UserStateSource::NetRecv);
		const FSimulationData::FUserState* InAuxState = InContent.SimView->UserData.Get(ENP_UserState::Aux, InputFrame, InContent.SimTick->EngineFrame, (uint8)ENP_UserStateSource::NetRecv);

		const FSimulationData::FUserState* OutSyncState = InContent.SimView->UserData.Get(ENP_UserState::Sync, SimTick.OutputFrame, InContent.SimTick->EngineFrame, (uint8)ENP_UserStateSource::NetRecv);
		const FSimulationData::FUserState* OutAuxState = InContent.SimView->UserData.Get(ENP_UserState::Aux, SimTick.OutputFrame, InContent.SimTick->EngineFrame, (uint8)ENP_UserStateSource::NetRecv);
				

		AddUserStateVSlots( { FUserStateWidgetInfo{ InputCmd, TEXT("Input Cmd"), FLinearColor::White } } );
		AddUserStateVSlots( { FUserStateWidgetInfo{ InSyncState, TEXT("In Sync"), FLinearColor::White }, FUserStateWidgetInfo{ InAuxState, TEXT("In Aux"), FLinearColor::White } });

		if (InAuxState != OutAuxState)
		{
			AddUserStateVSlots( { FUserStateWidgetInfo{ OutSyncState, TEXT("Out Sync"), FLinearColor::White }, FUserStateWidgetInfo{ OutAuxState, TEXT("Out Aux"), FLinearColor::White } } );
		}
		else
		{
			AddUserStateVSlots( { FUserStateWidgetInfo{ OutSyncState, TEXT("Out Sync"), FLinearColor::White }, FUserStateWidgetInfo{ OutAuxState, TEXT("Out Aux (No Change)"), FLinearColor::Gray} } );
		}
	}

	// -------------------------------------------------------------
	//	Net Receive
	// -------------------------------------------------------------

	if (InContent.NetRecv)
	{
		const FSimulationData::FNetSerializeRecv& NetRecv = *InContent.NetRecv;

		FString NetRecvText = FString::Format(TEXT(
			"Net Receive:\n"
			"SimFrame: {0}\n"
			"SimTime MS: {1}\n"
			"Status:  {2}\n"
			"GFrameNumber: {3}\n"),
		{
			NetRecv.Frame,
			NetRecv.SimTimeMS,
			LexToString(NetRecv.Status),
			NetRecv.EngineFrame
		});

		ContentsHBoxPtr->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0f, 0.0f, 4.0f, 0.0f)
		[
			SAssignNew(VertBox, SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(NetRecvText))
			]
		];


		uint8 Mask = ~((uint8)ENP_UserStateSource::NetRecv | (uint8)ENP_UserStateSource::NetRecvCommit);
		const FSimulationData::FUserState* RecvInputCmd = InContent.SimView->UserData.Get(ENP_UserState::Input, InContent.NetRecv->Frame, InContent.NetRecv->EngineFrame, Mask);
		const FSimulationData::FUserState* RecvSyncState = InContent.SimView->UserData.Get(ENP_UserState::Sync, InContent.NetRecv->Frame, InContent.NetRecv->EngineFrame, Mask);
		const FSimulationData::FUserState* RecvAuxState = InContent.SimView->UserData.Get(ENP_UserState::Aux, InContent.NetRecv->Frame, InContent.NetRecv->EngineFrame, Mask);

		auto SelectColor = [](const FSimulationData::FUserState* UserState)
		{
			return UserState && UserState->Source == ENP_UserStateSource::NetRecvCommit ? FLinearColor::White : FLinearColor::Gray;
		};

		AddUserStateVSlots( { 
			FUserStateWidgetInfo{ RecvInputCmd, TEXT("Recv InputCmd"), SelectColor(RecvInputCmd) },
			FUserStateWidgetInfo{ RecvSyncState, TEXT("Recv Sync"), SelectColor(RecvSyncState) }, 
			FUserStateWidgetInfo{ RecvAuxState, TEXT("Recv Aux"), SelectColor(RecvAuxState) } } 
		);
	}

}



#undef LOCTEXT_NAMESPACE