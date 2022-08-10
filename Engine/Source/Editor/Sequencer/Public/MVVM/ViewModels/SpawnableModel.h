// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"

#include "MVVM/ViewModels/ObjectBindingModel.h"

struct FMovieSceneBinding;
struct FMovieSceneSpawnable;

namespace UE
{
namespace Sequencer
{

class FSpawnableModel
	: public FObjectBindingModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FSpawnableModel, FObjectBindingModel);

	FSpawnableModel(FSequenceModel* InOwnerModel, const FMovieSceneBinding& InBinding, const FMovieSceneSpawnable& InSpawnable);
	~FSpawnableModel();

	/*~ FObjectBindingModel */
	EObjectBindingType GetType() const override;
	const FSlateBrush* GetIconOverlayBrush() const override;
	FText GetTooltipForSingleObjectBinding() const override;
	const UClass* FindObjectClass() const override;

	/*~ FViewModel interface */
	void OnConstruct() override;

	/*~ FOutlinerItemModel interface */
	FText GetIconToolTipText() const override;

	/*~ FObjectBindingModel interface */
	void Delete() override;
};

} // namespace Sequencer
} // namespace UE

