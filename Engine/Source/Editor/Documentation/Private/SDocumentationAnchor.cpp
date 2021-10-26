// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDocumentationAnchor.h"

#include "EditorClassUtils.h"
#include "EditorStyleSet.h"
#include "IDocumentation.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SToolTip.h"
#include "SSimpleButton.h"

void SDocumentationAnchor::Construct(const FArguments& InArgs )
{
	Link = InArgs._Link;

	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
		{
			return Link.Get(FString()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
		}));

	TAttribute<FText> ToolTipText = InArgs._ToolTipText;
	if (!ToolTipText.IsBound() && ToolTipText.Get().IsEmpty())
	{
		ToolTipText = NSLOCTEXT("DocumentationAnchor", "DefaultToolTip", "Click to open documentation");
	}

	const FString PreviewLink = InArgs._PreviewLink;
	// All in-editor UDN documents must live under the Shared/ folder
	ensure(PreviewLink.IsEmpty() || PreviewLink.StartsWith(TEXT("Shared/")));

	ChildSlot
	[
		SAssignNew(Button, SSimpleButton)
		.OnClicked(this, &SDocumentationAnchor::OnClicked)
		.Icon(FAppStyle::Get().GetBrush("Icons.Help"))
		.ToolTip(IDocumentation::Get()->CreateToolTip(ToolTipText, nullptr, PreviewLink, InArgs._PreviewExcerptName))
	];
}


FReply SDocumentationAnchor::OnClicked() const
{
	IDocumentation::Get()->Open(Link.Get(FString()), FDocumentationSourceInfo(TEXT("doc_anchors")));
	return FReply::Handled();
}
