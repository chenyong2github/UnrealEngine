// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDMXEditor;
class UDMXLibrary;
class UDMXEntityFixturePatch;


/** Visual representation of a connection to a channel in a dmx universe */
class SDMXChannelConnector
	: public SCompoundWidget
{
	DECLARE_DELEGATE_TwoParams(FOnDragOverChannel, int32, const FDragDropEvent&);

	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnDropOntoChannel, int32, const FDragDropEvent&);

public:
	SLATE_BEGIN_ARGS(SDMXChannelConnector)
		: _ChannelID(0)
		, _UniverseID(-1)
		, _Value(0.0f)
		, _OnDragEnterChannel()
		, _OnDragLeaveChannel()
		, _OnDropOntoChannel()
		, _DMXEditor()
	{}
		/** The channel ID this widget stands for */
		SLATE_ARGUMENT(int32, ChannelID)

		/** The Universe ID this widget resides in */
		SLATE_ARGUMENT(int32, UniverseID)

		/** The value of the channel */
		SLATE_ARGUMENT(uint8, Value)

		/** Called when drag enters the widget */
		SLATE_EVENT(FOnDragOverChannel, OnDragEnterChannel)

		/** Called when drag leaves the widget */
		SLATE_EVENT(FOnDragOverChannel, OnDragLeaveChannel)

		/** Called when dropped onto the channel */
		SLATE_EVENT(FOnDropOntoChannel, OnDropOntoChannel)

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Returns the ChannelID of this connector */
	int32 GetChannelID() const { return ChannelID; }

	/** Returns the UniverseID of this connector */
	int32 GetUniverseID() const { return UniverseID; }

	/** Sets the UniverseID of this connector */
	void SetUniverseID(int32 InUniverseID) { UniverseID = InUniverseID; }

protected:
	// Begin SWidget interface	
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;	
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End SWidget interface

	TSharedPtr<class SDMXChannel> ChannelValueWidget;

	int32 ChannelID;

	int32 UniverseID;

protected:
	/** Selects the fixture patch in Shared Data */
	void SelectFixturePatch(UDMXEntityFixturePatch* FixturePatch);

	/** Returns the Fixture Patch residing on this channel */
	UDMXEntityFixturePatch* GetFixturePatch() const;

	/** Returns the DMXLibrary or nullptr if not available */
	UDMXLibrary* GetDMXLibrary() const;

	int32 Column = INDEX_NONE;
	int32 Row = INDEX_NONE;

	FOnDragOverChannel OnDragEnterChannel;
	FOnDragOverChannel OnDragLeaveChannel;
	FOnDropOntoChannel OnDropOntoChannel;

	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
