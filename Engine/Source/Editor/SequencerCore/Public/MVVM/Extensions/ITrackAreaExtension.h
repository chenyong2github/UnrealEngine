// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModelTypeID.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "Templates/SharedPointer.h"

namespace UE
{
namespace Sequencer
{

class FViewModel;

enum class ETrackAreaLaneType : uint8
{
	Inline,
	Nested,
	None
};

struct SEQUENCERCORE_API FTrackAreaParameters
{
	ETrackAreaLaneType LaneType;
};

/**
 * This extension can be added to any model in order to provide a list of models that are relevent to the track area
 */
class SEQUENCERCORE_API ITrackAreaExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(ITrackAreaExtension)

	virtual ~ITrackAreaExtension(){}

	virtual FTrackAreaParameters GetTrackAreaParameters() const = 0;
	virtual FViewModelVariantIterator GetTrackAreaModelList() const = 0;

	template<typename T>
	TTypedIterator<T, FViewModelVariantIterator> GetTrackAreaModelListAs() const
	{
		return TTypedIterator<T, FViewModelVariantIterator>(this->GetTrackAreaModelList());
	}
};

} // namespace Sequencer
} // namespace UE

