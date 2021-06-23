// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXChannelConnector.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "DragDrop/DMXEntityFixturePatchDragDropOp.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Widgets/SDMXChannel.h"


void SDMXChannelConnector::Construct(const FArguments& InArgs)
{
	OnDragEnterChannel = InArgs._OnDragEnterChannel;
	OnDragLeaveChannel = InArgs._OnDragLeaveChannel;
	OnDropOntoChannel = InArgs._OnDropOntoChannel;

	ChannelID = InArgs._ChannelID;
	UniverseID = InArgs._UniverseID;

	DMXEditorPtr = InArgs._DMXEditor;

	ChildSlot
		[
			SAssignNew(ChannelValueWidget, SDMXChannel)
			.ChannelID(ChannelID)
			.Value(InArgs._Value)	
			.bShowChannelIDBottom(true)
		];
}

FReply SDMXChannelConnector::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if(UDMXEntityFixturePatch* Patch = GetFixturePatch())
		{
			SelectFixturePatch(Patch);

			FReply Reply = FReply::Handled();

			// SelectFixturePatch calls SetKeyboardFocus at some point; the call to SetKeyboardFocus is not enough for Slate: 
			// We must also return the widget using FReply.
			TSharedPtr<SWidget> WidgetThatWasFocusedBy_SelectFixturePatch = FSlateApplication::Get().GetKeyboardFocusedWidget();
			if(WidgetThatWasFocusedBy_SelectFixturePatch.IsValid())
			{
				Reply.SetUserFocus(WidgetThatWasFocusedBy_SelectFixturePatch.ToSharedRef());
			}
			return Reply.DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}
	}
	return FReply::Unhandled();
}

FReply SDMXChannelConnector::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();

	if (DMXLibrary)
	{
		if (UDMXEntityFixturePatch* Patch = GetFixturePatch())
		{
			int32 ChannelOffset = ChannelID - Patch->GetStartingChannel();

			TSharedRef<FDMXEntityFixturePatchDragDropOperation> DragDropOp = MakeShared<FDMXEntityFixturePatchDragDropOperation>(DMXLibrary, TArray<TWeakObjectPtr<UDMXEntity>>{ Patch }, ChannelOffset);

			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Unhandled();
}

void SDMXChannelConnector::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	OnDragEnterChannel.ExecuteIfBound(ChannelValueWidget->GetChannelID(), DragDropEvent);
}

void SDMXChannelConnector::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	OnDragLeaveChannel.ExecuteIfBound(ChannelValueWidget->GetChannelID(), DragDropEvent);
}

FReply SDMXChannelConnector::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return OnDropOntoChannel.Execute(ChannelValueWidget->GetChannelID(), DragDropEvent);
}

void SDMXChannelConnector::SelectFixturePatch(UDMXEntityFixturePatch* FixturePatch)
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = DMXEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());

		if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
		{
			SharedData->AddFixturePatchToSelection(FixturePatch);
		}
		else
		{
			SharedData->SelectFixturePatch(FixturePatch);
		}
	}
}

UDMXEntityFixturePatch* SDMXChannelConnector::GetFixturePatch() const
{
	if (UDMXLibrary* DMXLibrary = GetDMXLibrary())
	{
		TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
		UDMXEntityFixturePatch** PatchOnChannel = FixturePatches.FindByPredicate([this](UDMXEntityFixturePatch* TestedPatch) {
			int32 PatchUniverseID = TestedPatch->GetUniverseID();
			int32 PatchStartingChannel = TestedPatch->GetStartingChannel();
			int32 PatchEndingChannel = PatchStartingChannel + TestedPatch->GetChannelSpan() - 1;
			return
				PatchUniverseID == UniverseID &&
				PatchStartingChannel <= ChannelID &&
				PatchEndingChannel >= ChannelID;
			});

		if (PatchOnChannel)
		{
			return *PatchOnChannel;
		}
	}

	return nullptr;
}

UDMXLibrary* SDMXChannelConnector::GetDMXLibrary() const
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		return DMXEditor->GetDMXLibrary();
	}
	return nullptr;
}
