// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Color.h"
#include "Math/Vector2D.h"
#include "Math/Range.h"
#include "Containers/ArrayView.h"
#include "Misc/FrameNumber.h"
#include "KeyDrawParams.h"
#include "SectionLayout.h"
#include "DisplayNodes/SequencerKeyTimeCache.h"

struct FSlateBrush;

class FSequencer;
class FWidgetStyle;
class FSequencerSectionPainter;

namespace UE
{
namespace Sequencer
{

/**
 * Paint arguments required for painting keys on a sequencer track
 */
struct FKeyRendererPaintArgs
{
	/** The amount to throb selected keys by */
	float KeyThrobValue = 0.f;

	/** The amount to throb selected sections by */
	float SectionThrobValue = 0.f;

	/** Fixed amount to throb newly created keys by */
	FVector2D ThrobAmount = FVector2D(12.f, 12.f);
};

/**
 * 
 */
struct FKeyRenderer
{
	/** Flag enum signifying how the cache has changed since it was last generated */
	enum class ECacheFlags : uint8
	{
		None             = 0,       // The cache is still entirely valid, simply redraw the keys
		DataChanged      = 1 << 0,  // The underlying keyframes have changed - everything needs regenerating
		KeyStateChanged  = 1 << 1,  // The selection, hover or preview selection state of the keys has changed
		ViewChanged      = 1 << 2,  // The view range has changed - view dependent data needs regenerating, but some cache data may be preserved
		ViewZoomed       = 1 << 3,  // The view range has been zoomed - view dependent data needs regenerating, no key grouping can be preserved

		All = DataChanged | KeyStateChanged | ViewChanged | ViewZoomed,
	};


	/** Flag enum signifying states for a particular key or group of keys */
	enum class EKeyRenderingFlags
	{
		None               = 0,
		PartialKey         = 1 << 0,  // Indicates that this key comprises multiple keys of different types, or a partially keyed collapsed channel
		Selected           = 1 << 1,  // Only if.NumSelected == Key.TotalNumKeys
		PreviewSelected    = 1 << 2,  // Only if NumPreviewSelected == NumKeys
		PreviewNotSelected = 1 << 3,  // Only if NumPreviewNotSelected == NumKeys
		AnySelected        = 1 << 4,  // If any are selected
		Hovered            = 1 << 5,  // Only if NumKeys == NumHovered
		Overlaps           = 1 << 6,  // If NumKeys > 1
		OutOfRange         = 1 << 7,  // If any of the keys fall outside of the valid range
	};


	/**
	 * Paint all the keys for the specified section layout
	 *
	 * @param InSectionLayout     The section layout that defines the key areas for this renderer
	 * @param InWidgetStyle       The widget style for the outer SSequencerSection
	 * @param InArgs              Paint arguments specific to this key renderer
	 * @param InSequencer         The Sequencer object that is painting the keys
	 * @param InPainter           Section painter object
	 */
	void Paint(const FSectionLayout& InSectionLayout, const FWidgetStyle& InWidgetStyle, const FKeyRendererPaintArgs& InArgs, FSequencer* InSequencer, FSequencerSectionPainter& InPainter) const;


private:


	/** Structure that caches the various bits of information upon which our view is dependent */
	struct FCachedState
	{
		/** Construction from a section painter and sequencer object - populates the cached values */
		explicit FCachedState(const FSequencerSectionPainter& InPainter, FSequencer* Sequencer);


		/**
		 * Compare this cache state to another
		 *
		 * @param Other   The other cached state to compare with
		 * @return Flag structure specifying how these two caches vary
		 */
		ECacheFlags CompareTo(const FCachedState& Other) const;


		/** The min/max tick value relating to the FMovieSceneSubSequenceData::ValidPlayRange bounds, or the current playback range */
		FFrameNumber ValidPlayRangeMin, ValidPlayRangeMax;

		/** The current view range +/- the width of a key */
		TRange<double> PaddedViewRange;

		/** The value of FSequencerSelection::GetSerialNumber when this cache was created */
		uint32 SelectionSerial = 0;

		/** The value of FSequencerSelectionPreview::GetSelectionHash when this cache was created */
		uint32 SelectionPreviewHash = 0;
	};


	/** Container that caches the key positions for a given key area, along with those that overlap the current visible range */
	struct FCachedKeyDrawInformation
	{
		/** Construction from the key area this represents */
		FCachedKeyDrawInformation(TSharedRef<IKeyArea> KeyArea)
			: NextUnhandledIndex(0)
			, PreserveToIndex(0)
			, CachedKeyPositions(KeyArea)
		{}


		/**
		 * Attempt to update data that is not dependent upon the current view
		 * @return ECacheFlags::DataChanged if view-indipendent data was updated, ECacheFlags::None otherwise
		 */
		ECacheFlags UpdateViewIndependentData(FFrameRate TickResolution);


		/**
		 * Ensure that view-dependent data (such as which keys need drawing and how) is up to date
		 *
		 * @param VisibleRange    The currently visible range in seconds
		 * @param CacheFlags      Accumulated flags that represent how the view or data has changed since this was last called
		 */
		void CacheViewDependentData(const TRange<double>& VisibleRange, ECacheFlags CacheFlags);

	public:

		/** Index into the array views for the next unhandled key */
		int32 NextUnhandledIndex;

		/** Index into the array views for the first index proceeding a preserved range (TimesInRange.Num() if not) */
		int32 PreserveToIndex;

		/** Cached array view retrieved from CachedKeyPositions for the key times that overlap the current time */
		TArrayView<const double> TimesInRange;
		/** Cached array view retrieved from CachedKeyPositions for the key frames that overlap the current time */
		TArrayView<const FFrameNumber> FramesInRange;
		/** Cached array view retrieved from CachedKeyPositions for the key handles that overlap the current time */
		TArrayView<const FKeyHandle> HandlesInRange;
		/** Draw params for each of the keys visible on screen */
		TArray<FKeyDrawParams> DrawParams;

		/** Construction from the key area this represents */
		FSequencerCachedKeys CachedKeyPositions;
	};


	/** Cached parameters for drawing a single key */
	struct FKey
	{
		/** Paint parameters for this key */
		FKeyDrawParams Params;

		/** The tick range that this key occupies (significant when this FKey represents multiple overlapping keys) */
		FFrameNumber KeyTickStart, KeyTickEnd;

		/** The time in seconds that this key should be drawn - represents the average time for overlapping keys */
		double FinalKeyPositionSeconds;

		/** Flags that specify how to draw this key */
		EKeyRenderingFlags Flags = EKeyRenderingFlags::None;
	};


	struct FPaintStyle
	{
		FPaintStyle(const FWidgetStyle& InWidgetStyle);

		FLinearColor SelectionColor;

		const FSlateBrush* BackgroundTrackTintBrush;
		const FSlateBrush* SelectedTrackTintBrush;
		const FSlateBrush* StripeOverlayBrush;
		const FSlateBrush* HighlightBrush;
	};


	/** A batch of keys for a given section layout element, including all recursive keyframe groups reduced by overlapping state */
	struct FKeyDrawBatch
	{
		/** Construction from a section layout element */
		FKeyDrawBatch(const FSectionLayoutElement& LayoutElement);


		/**
		 * Attempt to update data that is not dependent upon the current view
		 * @return ECacheFlags::DataChanged if view-indipendent data was updated, ECacheFlags::None otherwise
		 */
		ECacheFlags UpdateViewIndependentData(FFrameRate TickResolution);


		/**
		 * Ensure that view-dependent data (such as which keys need drawing and how) is up to date
		 */
		void UpdateViewDependentData(FSequencer* Sequencer, const FSequencerSectionPainter& InPainter, const FCachedState& CachedState, ECacheFlags CacheFlags);


		/**
		 * Draw this batch
		 */
		void Draw(FSequencer* Sequencer, FSequencerSectionPainter& Painter, const FGeometry& KeyGeometry, const FPaintStyle& Style, const FKeyRendererPaintArgs& Args) const;

	private:

		/** Array of cached draw info for each of the key areas that comprise this batch */
		TArray<FCachedKeyDrawInformation, TInlineAllocator<1>> KeyDrawInfo;

		/** Computed final draw info */
		TArray<FKey> PrecomputedKeys;
	};

private:

	void UpdateKeyLayouts(FSequencer* Sequencer, const FSequencerSectionPainter& InPainter, const FSectionLayout& InSectionLayout) const;
	void DrawLayoutElement(FSequencer* Sequencer, const FSequencerSectionPainter& InPainter, const FSectionLayoutElement& LayoutElement, const FPaintStyle& Style, const FKeyRendererPaintArgs& Args) const;

private:

	/** Key funcs for looking up a set of cached keys by its layout element */
	struct FLayoutElementKeyFuncs : FSectionLayoutElementKeyFuncs, BaseKeyFuncs<FKeyDrawBatch, FSectionLayoutElement, false>
	{};

	/** Cache of key area positions */
	mutable TMap<FSectionLayoutElement, FKeyDrawBatch, FDefaultSetAllocator, FLayoutElementKeyFuncs> CachedKeyLayouts;

	mutable TOptional<FCachedState> CachedState;
};

ENUM_CLASS_FLAGS(FKeyRenderer::ECacheFlags);
ENUM_CLASS_FLAGS(FKeyRenderer::EKeyRenderingFlags);

} // namespace Sequencer
} // namespace UE

