// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"

#include "MVVM/ViewModels/ObjectBindingModel.h"

struct FMovieSceneBinding;
struct FMovieScenePossessable;

namespace UE
{
namespace Sequencer
{

class FPossessableModel
	: public FObjectBindingModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FPossessableModel, FObjectBindingModel);

	FPossessableModel(FSequenceModel* OwnerModel, const FMovieSceneBinding& InBinding, const FMovieScenePossessable& InPossessable);
	~FPossessableModel();

	/*~ FObjectBindingModel */
	EObjectBindingType GetType() const override;
	const FSlateBrush* GetIconOverlayBrush() const override;
	const UClass* FindObjectClass() const override;
	bool SupportsRebinding() const override;

	/*~ FViewModel interface */
	void OnConstruct() override;

	/*~ FOutlinerItemModel interface */
	FText GetIconToolTipText() const override;

	/*~ FObjectBindingModel interface */
	void Delete() override;
};

} // namespace Sequencer
} // namespace UE

