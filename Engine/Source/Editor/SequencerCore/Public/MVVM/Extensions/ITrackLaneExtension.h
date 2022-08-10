// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModelTypeID.h"
#include "MVVM/ViewModels/EditorViewModel.h"
#include "Widgets/SCompoundWidget.h"

struct FGeometry;

class SWidget;
class ISequencer;
class FArrangedWidget;
struct FTimeToPixel;

namespace UE
{
namespace Sequencer
{

struct FTrackLaneScreenAlignment;

struct SEQUENCERCORE_API FTrackLaneVerticalArrangement
{
	float Offset = 0;
	float Height = 0;
};

struct SEQUENCERCORE_API FTrackLaneVerticalAlignment
{
	enum class ESizeMode : uint8
	{
		Proportional, Fixed
	};

	float              VSizeParam = 1.f;
	float              VPadding   = 0.f;
	EVerticalAlignment VAlign     = VAlign_Center;
	ESizeMode          Mode       = ESizeMode::Proportional;

	FTrackLaneVerticalArrangement ArrangeWithin(float LayoutHeight) const;
};

struct SEQUENCERCORE_API FTrackLaneVirtualAlignment
{
	TRange<FFrameNumber> Range;
	FTrackLaneVerticalAlignment VerticalAlignment;

	bool IsVisible() const
	{
		return !Range.IsEmpty() && !Range.IsDegenerate();
	}

	static FTrackLaneVirtualAlignment Fixed(const TRange<FFrameNumber>& InRange, float InFixedHeight, EVerticalAlignment InVAlign = VAlign_Center)
	{
		return FTrackLaneVirtualAlignment { InRange, { InFixedHeight, 0.f, InVAlign, FTrackLaneVerticalAlignment::ESizeMode::Fixed } };
	}
	static FTrackLaneVirtualAlignment Proportional(const TRange<FFrameNumber>& InRange, float InStretchFactor, EVerticalAlignment InVAlign = VAlign_Center)
	{
		return FTrackLaneVirtualAlignment { InRange, { InStretchFactor, 0.f, InVAlign, FTrackLaneVerticalAlignment::ESizeMode::Proportional } };
	}

	TOptional<FFrameNumber> GetFiniteLength() const;

	FTrackLaneScreenAlignment ToScreen(const FTimeToPixel& TimeToPixel, const FGeometry& ParentGeometry) const;
};

struct SEQUENCERCORE_API FTrackLaneScreenAlignment
{
	float LeftPosPx = 0.f;
	float WidthPx = 0.f;
	FTrackLaneVerticalAlignment VerticalAlignment;

	bool IsVisible() const
	{
		return WidthPx > 0.f;
	}

	FArrangedWidget ArrangeWidget(TSharedRef<SWidget> InWidget, const FGeometry& ParentGeometry) const;
};

struct SEQUENCERCORE_API FArrangedVirtualEntity
{
	TRange<FFrameNumber> Range;
	float VirtualTop, VirtualBottom;
};

/** Base interface for track-area lanes */
class SEQUENCERCORE_API ITrackLaneWidget
{
public:

	/**
	 * Retrieve this interface as a widget
	 */
	virtual TSharedRef<const SWidget> AsWidget() const = 0;

	TSharedRef<SWidget> AsWidget()
	{
		TSharedRef<const SWidget> ConstWidget = const_cast<const ITrackLaneWidget*>(this)->AsWidget();
		return ConstCastSharedRef<SWidget>(ConstWidget);
	}

	/**
	 * Arrange this widget within its parent slot
	 */
	virtual FTrackLaneScreenAlignment GetAlignment(const FTimeToPixel& TimeToPixel, const FGeometry& InParentGeometry) const = 0;

	/**
	 * Gets this widget's overlap priority
	 */
	virtual int32 GetOverlapPriority() const { return 0; }

	/**
	 * Receive parent geometry for this lane in Desktop space
	 */
	virtual void ReportParentGeometry(const FGeometry& InDesktopSpaceParentGeometry) {}

	/**
	 * Whether this track lane accepts child widgets
	 */
	virtual bool AcceptsChildren() const { return false; }

	/**
	 * Add a new child to this lane
	 */
	virtual void AddChildLane(TSharedPtr<ITrackLaneWidget> ChildWidget) {}
};

/** Parameters for creating a track lane widget */
struct SEQUENCERCORE_API FCreateTrackLaneViewParams
{
	FCreateTrackLaneViewParams(const TSharedPtr<FEditorViewModel> InEditor)
		: Editor(InEditor)
	{}

	const TSharedPtr<FEditorViewModel> Editor;
};

/** Extension for view-models that can create track lanes in the track area */
class SEQUENCERCORE_API ITrackLaneExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(ITrackLaneExtension)

	virtual ~ITrackLaneExtension(){}

	virtual TSharedPtr<ITrackLaneWidget> CreateTrackLaneView(const FCreateTrackLaneViewParams& InParams) = 0;
	virtual FTrackLaneVirtualAlignment ArrangeVirtualTrackLaneView() const = 0;
};

} // namespace Sequencer
} // namespace UE

