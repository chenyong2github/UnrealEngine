// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/CursorReply.h"
#include "SequencerSelectedKey.h"
#include "ISequencerHotspot.h"

class FMenuBuilder;
class ISequencer;
class ISequencerEditToolDragOperation;
class FSequencerTrackNode;
class ISequencerSection;

enum class ESequencerEasingType
{
	In, Out
};

/** A hotspot representing a key */
struct FKeyHotspot
	: ISequencerHotspot
{
	FKeyHotspot(TArray<FSequencerSelectedKey> InKeys)
		: Keys(MoveTemp(InKeys))
	{ }

	virtual ESequencerHotspot GetType() const override { return ESequencerHotspot::Key; }
	virtual void UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const override;
	virtual TOptional<FFrameNumber> GetTime() const override;
	virtual bool PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& Sequencer, FFrameTime MouseDownTime) override;

	/** The keys that are part of this hotspot */
	TArray<FSequencerSelectedKey> Keys;
};


/** A hotspot representing a section */
struct FSectionHotspot
	: ISequencerHotspot
{
	FSectionHotspot(UMovieSceneSection* InSection)
		: WeakSection(InSection)
	{ }

	virtual ESequencerHotspot GetType() const override { return ESequencerHotspot::Section; }
	virtual void UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const override;
	virtual TOptional<FFrameNumber> GetTime() const override;
	virtual TOptional<FFrameTime> GetOffsetTime() const override;
	virtual TSharedPtr<ISequencerEditToolDragOperation> InitiateDrag(ISequencer&) override { return nullptr; }
	virtual bool PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& Sequencer, FFrameTime MouseDownTime) override;

	/** The section */
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
};


/** A hotspot representing a resize handle on a section */
struct FSectionResizeHotspot
	: ISequencerHotspot
{
	enum EHandle
	{
		Left,
		Right
	};

	FSectionResizeHotspot(EHandle InHandleType, UMovieSceneSection* InSection) : WeakSection(InSection), HandleType(InHandleType) {}

	virtual ESequencerHotspot GetType() const override { return HandleType == Left ? ESequencerHotspot::SectionResize_L : ESequencerHotspot::SectionResize_R; }
	virtual void UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const override;
	virtual TOptional<FFrameNumber> GetTime() const override;
	virtual TSharedPtr<ISequencerEditToolDragOperation> InitiateDrag(ISequencer& Sequencer) override;
	virtual FCursorReply GetCursor() const { return FCursorReply::Cursor( EMouseCursor::ResizeLeftRight ); }

	/** The section */
	TWeakObjectPtr<UMovieSceneSection> WeakSection;

private:

	EHandle HandleType;
};


/** A hotspot representing a resize handle on a section's easing */
struct FSectionEasingHandleHotspot
	: ISequencerHotspot
{
	FSectionEasingHandleHotspot(ESequencerEasingType InHandleType, UMovieSceneSection* InSection) : WeakSection(InSection), HandleType(InHandleType) {}

	virtual ESequencerHotspot GetType() const override { return HandleType == ESequencerEasingType::In ? ESequencerHotspot::EaseInHandle : ESequencerHotspot::EaseOutHandle; }
	virtual void UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const override;
	virtual bool PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& Sequencer, FFrameTime MouseDownTime) override;
	virtual TOptional<FFrameNumber> GetTime() const override;
	virtual TSharedPtr<ISequencerEditToolDragOperation> InitiateDrag(ISequencer& Sequencer) override;
	virtual FCursorReply GetCursor() const { return FCursorReply::Cursor( EMouseCursor::ResizeLeftRight ); }

	/** Handle to the section */
	TWeakObjectPtr<UMovieSceneSection> WeakSection;

private:

	ESequencerEasingType HandleType;
};


struct FEasingAreaHandle
{
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
	ESequencerEasingType EasingType;
};

/** A hotspot representing an easing area for multiple sections */
struct FSectionEasingAreaHotspot
	: FSectionHotspot
{
	FSectionEasingAreaHotspot(const TArray<FEasingAreaHandle>& InEasings, UMovieSceneSection* InVisibleSection) : FSectionHotspot(InVisibleSection), Easings(InEasings) {}

	virtual ESequencerHotspot GetType() const override { return ESequencerHotspot::EasingArea; }
	virtual bool PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& Sequencer, FFrameTime MouseDownTime) override;

	bool Contains(UMovieSceneSection* InSection) const { return Easings.ContainsByPredicate([=](const FEasingAreaHandle& InHandle){ return InHandle.WeakSection == InSection; }); }

	/** Handles to the easings that exist on this hotspot */
	TArray<FEasingAreaHandle> Easings;
};