// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"


namespace UE::RenderPages::Private
{
	/**
	 * Contains the commands for the render page editor.
	 */
	class FRenderPagesEditorCommands : public TCommands<FRenderPagesEditorCommands>
	{
	public:
		FRenderPagesEditorCommands()
			: TCommands<FRenderPagesEditorCommands>(TEXT("RenderPagesEditor"),
				NSLOCTEXT("Contexts", "RenderPagesEditor", "Render Pages Editor"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{}

		virtual void RegisterCommands() override;

	public:
		/** Adds a new page to the collection. */
		TSharedPtr<FUICommandInfo> AddPage;

		/** Copies the given page and adds it to the collection. */
		TSharedPtr<FUICommandInfo> CopyPage;

		/** Deletes an existing page from the collection. */
		TSharedPtr<FUICommandInfo> DeletePage;

		/** Renders the available page(s) in batch. */
		TSharedPtr<FUICommandInfo> BatchRenderList;
	};
}
