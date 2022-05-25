// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModelPtr.h"
#include "MVVM/Extensions/DynamicExtensionContainer.h"
#include "MVVM/Extensions/ICurveEditorTreeItemExtension.h"
#include "CurveEditorTypes.h"
#include "Tree/ICurveEditorTreeItem.h"

namespace UE
{
namespace Sequencer
{

class FSequenceModel;

/**
 * Extension for managing integration between outliner items and the curve editor.
 *
 * It relies on the following:
 *
 * - Extension owner (generally the root view-model) implements ICurveEditorExtension, to get
 *   access to the curve editor itself.
 *
 * - Outliner items implementing ICurveEditorTreeItemExtension (or its default shim) if they 
 *   want to show up in the curve editor.
 */
class FCurveEditorIntegrationExtension : public IDynamicExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID(FCurveEditorIntegrationExtension)

	FCurveEditorIntegrationExtension();

	virtual void OnCreated(TSharedRef<FViewModel> InWeakOwner) override;

	void UpdateCurveEditor();
	void RecreateCurveEditor();

private:

	void OnHierarchyChanged();

	FCurveEditorTreeItemID AddToCurveEditor(TViewModelPtr<ICurveEditorTreeItemExtension> InViewModel, TSharedPtr<FCurveEditor> CurveEditor);

private:

	TWeakPtr<FSequenceModel> WeakOwnerModel;
	TMap<TWeakPtr<FViewModel>, FCurveEditorTreeItemID> ViewModelToTreeItemIDMap;
};

} // namespace Sequencer
} // namespace UE

