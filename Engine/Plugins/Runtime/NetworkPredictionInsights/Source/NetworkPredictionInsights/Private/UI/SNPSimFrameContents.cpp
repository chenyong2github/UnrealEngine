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
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(2.0f)
			.AutoHeight()
			[
				SAssignNew(ContentsHBoxPtr, SHorizontalBox)
			]

			+SVerticalBox::Slot()
			.Padding(2.0f)
			.AutoHeight()
			[
				SAssignNew(SystemFaultsVBoxPtr, SVerticalBox)
			]
		]
	];
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
		FString UserStr;
		uint64 EngineFrameHyperLink = 0;
		if (Info.State)
		{
			// style=\"Hyperlink\"
			HeaderText = FString::Printf(TEXT("%s [Sim: %d. <a id=\"engine\">Engine: %d</>. %s]"), Info.Heading, Info.State->SimFrame, Info.State->EngineFrame, LexToString(Info.State->Source));
			UserStr = Info.State->UserStr;
			if (Info.State->OOBStr)
			{
				UserStr += FString::Printf(TEXT("\nOOB Mod:\n%s"), Info.State->OOBStr);
			}

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
	//	End of Frame
	// -------------------------------------------------------------
	uint64 EngineFrame = (InContent.SimTick ? InContent.SimTick->EngineFrame : (InContent.NetRecv ? InContent.NetRecv->EngineFrame : 0));
	const FSimulationData::FEngineFrame* EOFState = nullptr;
	if (EngineFrame > 0)
	{
		for (auto It = InContent.SimView->EOFState.GetIteratorFromEnd(); It; --It)
		{
			if (It->EngineFrame == EngineFrame)
			{
				EOFState = &*It;
				break;
			}
		}

		if (EOFState)
		{
			FString EOFText = FString::Format(TEXT(
			"EngineFrame {0}\n"
			"EngineFrameDeltaTime: {1}\n"
			"BufferSize: {2}\n"
			"PendingTickFrame: {3}\n"
			"LatestInputFrame: {4}\n"
			"TotalSimTime: {5}\n"
			"AllowedSimTime: {6}"),
			{
				EOFState->EngineFrame,
				EOFState->EngineFrameDeltaTime,
				EOFState->BufferSize,
				EOFState->PendingTickFrame,
				EOFState->LatestInputFrame,
				EOFState->TotalSimTime,
				EOFState->AllowedSimTime
			});

			/*
			for (const FSimulationData::FSystemFault& Fault : EOFState->SystemFaults)
			{
				EOFText.Append(Fault.Str);
				EOFText.Append(TEXT("\n"));
			}
			*/

			VertBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(EOFText))
			];
		}
	}

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


		// Input/Output states
		
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


	// -------------------------------------
	// System Faults
	// -------------------------------------

	SystemFaultsVBoxPtr->ClearChildren();

	if (InContent.NetRecv && InContent.NetRecv->SystemFaults.Num() > 0)
	{
		FString FaultString = TEXT("NetRecv Faults:\n");

		for (const FSimulationData::FSystemFault& Fault : InContent.NetRecv->SystemFaults)
		{
			FaultString.Append(Fault.Str);
			FaultString.Append(TEXT("\n"));
		}

		SystemFaultsVBoxPtr->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0f, 4.0f, 4.0f, 4.0f)
		[
			SNew(SEditableText)
			.IsReadOnly(true)
			.Text(FText::FromString(FaultString))
		];
	}

	if (EOFState && EOFState->SystemFaults.Num() > 0)
	{
		FString FaultString = TEXT("Engine Frame Faults:\n");

		for (const FSimulationData::FSystemFault& Fault : EOFState->SystemFaults)
		{
			FaultString.Append(Fault.Str);
			FaultString.Append(TEXT("\n"));
		}

		SystemFaultsVBoxPtr->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0f, 4.0f, 4.0f, 4.0f)
		[
			SNew(SEditableText)
			.IsReadOnly(true)
			.Text(FText::FromString(FaultString))
		];
	}

}



#undef LOCTEXT_NAMESPACE