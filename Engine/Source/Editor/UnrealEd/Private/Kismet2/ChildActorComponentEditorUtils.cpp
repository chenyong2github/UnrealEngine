// Copyright Epic Games, Inc. All Rights Reserved.

#include "Kismet2/ChildActorComponentEditorUtils.h"
#include "Settings/EditorProjectSettings.h"
#include "ToolMenus.h"
#include "SSCSEditor.h"
#include "SSCSEditorMenuContext.h"

#define LOCTEXT_NAMESPACE "ChildActorComponentEditorUtils"

bool FChildActorComponentEditorUtils::IsChildActorNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr)
{
	return InNodePtr.IsValid() && InNodePtr->GetNodeType() == FSCSEditorTreeNode::ENodeType::ChildActorNode;
}

bool FChildActorComponentEditorUtils::IsChildActorSubtreeNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr)
{
	return InNodePtr.IsValid() && IsChildActorNode(InNodePtr->GetActorRootNode());
}

bool FChildActorComponentEditorUtils::ContainsChildActorSubtreeNode(const TArray<TSharedPtr<FSCSEditorTreeNode>>& InNodePtrs)
{
	for (TSharedPtr<FSCSEditorTreeNode> NodePtr : InNodePtrs)
	{
		if (IsChildActorSubtreeNode(NodePtr))
		{
			return true;
		}
	}

	return false;
}

TSharedPtr<FSCSEditorTreeNode> FChildActorComponentEditorUtils::GetOuterChildActorComponentNode(TSharedPtr<const FSCSEditorTreeNode> InNodePtr)
{
	if (InNodePtr.IsValid())
	{
		FSCSEditorActorNodePtrType ActorTreeRootNode = InNodePtr->GetActorRootNode();
		if (IsChildActorNode(ActorTreeRootNode))
		{
			return ActorTreeRootNode->GetParent();
		}
	}

	return nullptr;
}

bool FChildActorComponentEditorUtils::IsChildActorTreeViewExpansionEnabled()
{
	const UBlueprintEditorProjectSettings* EditorProjectSettings = GetDefault<UBlueprintEditorProjectSettings>();
	return EditorProjectSettings->bEnableChildActorExpansionInTreeView;
}

EChildActorComponentTreeViewVisualizationMode FChildActorComponentEditorUtils::GetProjectDefaultTreeViewVisualizationMode()
{
	const UBlueprintEditorProjectSettings* EditorProjectSettings = GetDefault<UBlueprintEditorProjectSettings>();
	return EditorProjectSettings->DefaultChildActorTreeViewMode;
}

void FChildActorComponentEditorUtils::ToggleComponentNodeVisibility(UChildActorComponent* ChildActorComponent, TWeakPtr<SSCSEditor> WeakEditorPtr)
{
	if (ChildActorComponent)
	{
		const bool bIsComponentNodeVisible = ShouldShowComponentNodeInTreeView(ChildActorComponent);

		if (bIsComponentNodeVisible)
		{
			ChildActorComponent->SetEditorTreeViewVisualizationMode(EChildActorComponentTreeViewVisualizationMode::ChildActorOnly);
		}
		else
		{
			ChildActorComponent->SetEditorTreeViewVisualizationMode(EChildActorComponentTreeViewVisualizationMode::ComponentWithChildActor);
		}

		TSharedPtr<SSCSEditor> SCSEditor = WeakEditorPtr.Pin();
		if (SCSEditor.IsValid())
		{
			SCSEditor->UpdateTree();
		}
	}
}

void FChildActorComponentEditorUtils::ToggleChildActorNodeVisibility(UChildActorComponent* ChildActorComponent, TWeakPtr<SSCSEditor> WeakEditorPtr)
{
	if (ChildActorComponent)
	{
		const bool bIsChildActorNodeVisible = ShouldShowChildActorNodeInTreeView(ChildActorComponent);

		if (bIsChildActorNodeVisible)
		{
			ChildActorComponent->SetEditorTreeViewVisualizationMode(EChildActorComponentTreeViewVisualizationMode::ComponentOnly);
		}
		else
		{
			ChildActorComponent->SetEditorTreeViewVisualizationMode(EChildActorComponentTreeViewVisualizationMode::ComponentWithChildActor);
		}

		TSharedPtr<SSCSEditor> SCSEditor = WeakEditorPtr.Pin();
		if (SCSEditor.IsValid())
		{
			SCSEditor->UpdateTree();
		}
	}
}

bool FChildActorComponentEditorUtils::ShouldShowComponentNodeInTreeView(UChildActorComponent* ChildActorComponent)
{
	if (ChildActorComponent)
	{
		if (!IsChildActorTreeViewExpansionEnabled())
		{
			// Always show the component node when tree view expansion is disabled.
			return true;
		}

		EChildActorComponentTreeViewVisualizationMode CurrentMode = ChildActorComponent->GetEditorTreeViewVisualizationMode();
		if (CurrentMode == EChildActorComponentTreeViewVisualizationMode::UseDefault)
		{
			CurrentMode = GetProjectDefaultTreeViewVisualizationMode();
		}

		return CurrentMode != EChildActorComponentTreeViewVisualizationMode::ChildActorOnly;
	}

	return false;
}

bool FChildActorComponentEditorUtils::ShouldShowChildActorNodeInTreeView(UChildActorComponent* ChildActorComponent)
{
	if (ChildActorComponent)
	{
		if (!IsChildActorTreeViewExpansionEnabled())
		{
			// Never show the child actor node when tree view expansion is disabled.
			return false;
		}

		EChildActorComponentTreeViewVisualizationMode CurrentMode = ChildActorComponent->GetEditorTreeViewVisualizationMode();
		if (CurrentMode == EChildActorComponentTreeViewVisualizationMode::UseDefault)
		{
			CurrentMode = GetProjectDefaultTreeViewVisualizationMode();
		}

		return CurrentMode != EChildActorComponentTreeViewVisualizationMode::ComponentOnly;
	}

	return false;
}

void FChildActorComponentEditorUtils::FillComponentContextMenuOptions(UToolMenu* Menu, UChildActorComponent* ChildActorComponent)
{
	if (!IsChildActorTreeViewExpansionEnabled())
	{
		return;
	}

	if (!ChildActorComponent)
	{
		return;
	}

	TWeakPtr<SSCSEditor> WeakEditorPtr;
	if (USSCSEditorMenuContext* MenuContext = Menu->FindContext<USSCSEditorMenuContext>())
	{
		WeakEditorPtr = MenuContext->SCSEditor;
	}

	FToolMenuSection& Section = Menu->AddSection("ChildActorComponent", LOCTEXT("ChildActorComponentHeading", "Child Actor Component"));
	{
		FText ShowOrHideItemText;
		if (ShouldShowChildActorNodeInTreeView(ChildActorComponent))
		{
			ShowOrHideItemText = LOCTEXT("HideChildActorNode_Label", "Hide Child Actor Node");
		}
		else
		{
			ShowOrHideItemText = LOCTEXT("ShowChildActorNode_Label", "Show Child Actor Node");
		}

		Section.AddMenuEntry(
			"ToggleChildActorNode",
			ShowOrHideItemText,
			LOCTEXT("ToggleChildActorNode_ToolTip", "Toggle visibility of this component's child actor node."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FChildActorComponentEditorUtils::ToggleChildActorNodeVisibility, ChildActorComponent, WeakEditorPtr),
				FCanExecuteAction()
			)
		);
	}
}

void FChildActorComponentEditorUtils::FillChildActorContextMenuOptions(UToolMenu* Menu, TSharedPtr<const FSCSEditorTreeNode> InNodePtr)
{
	if (!IsChildActorTreeViewExpansionEnabled())
	{
		return;
	}

	if (!IsChildActorNode(InNodePtr))
	{
		return;
	}

	TSharedPtr<const FSCSEditorTreeNodeChildActor> ChildActorNodePtr = StaticCastSharedPtr<const FSCSEditorTreeNodeChildActor>(InNodePtr);
	check(ChildActorNodePtr.IsValid());

	UChildActorComponent* ChildActorComponent = ChildActorNodePtr->GetChildActorComponent();
	if (!ChildActorComponent)
	{
		return;
	}

	TWeakPtr<SSCSEditor> WeakEditorPtr;
	if (USSCSEditorMenuContext* MenuContext = Menu->FindContext<USSCSEditorMenuContext>())
	{
		WeakEditorPtr = MenuContext->SCSEditor;
	}

	FToolMenuSection& Section = Menu->AddSection("ChildActor", LOCTEXT("ChildActorHeading", "Child Actor"));
	{
		FText ShowOrHideItemText;
		if (ShouldShowComponentNodeInTreeView(ChildActorComponent))
		{
			ShowOrHideItemText = LOCTEXT("HideChildActorComponentNode_Label", "Hide Child Actor Component Node");
		}
		else
		{
			ShowOrHideItemText = LOCTEXT("ShowChildActorComponentNode_Label", "Show Child Actor Component Node");
		}

		Section.AddMenuEntry(
			"ToggleChildActorComponentNode",
			ShowOrHideItemText,
			LOCTEXT("ToggleChildActorComponentNode_ToolTip", "Toggle visibility of this child actor's outer component node."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FChildActorComponentEditorUtils::ToggleComponentNodeVisibility, ChildActorComponent, WeakEditorPtr),
				FCanExecuteAction()
			)
		);
	}
}

#undef LOCTEXT_NAMESPACE
