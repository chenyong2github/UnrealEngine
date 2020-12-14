// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "GraphEditorDragDropAction.h"

namespace UE
{
namespace MovieScene
{

struct FFixedObjectBindingID;

} // namespace MovieScene
} // namespace UE

class FSequencerObjectBindingDragDropOp : public FGraphEditorDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE( FSequencerDisplayNodeDragDropOpBase, FGraphEditorDragDropAction )

	virtual TArray<UE::MovieScene::FFixedObjectBindingID> GetDraggedBindings() const = 0;
};