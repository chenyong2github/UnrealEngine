// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"


namespace UE::PoseSearch
{
	class FDatabasePreviewScene;
	class FDatabaseViewportClient;
	class FDatabaseEditorToolkit;
	class SPoseSearchDatabaseViewportToolBar;

	struct FDatabaseViewportRequiredArgs
	{
		FDatabaseViewportRequiredArgs(
			const TSharedRef<FDatabaseEditorToolkit>& InAssetEditorToolkit,
			const TSharedRef<FDatabasePreviewScene>& InPreviewScene)
			: AssetEditorToolkit(InAssetEditorToolkit)
			, PreviewScene(InPreviewScene)
		{
		}

		TSharedRef<FDatabaseEditorToolkit> AssetEditorToolkit;

		TSharedRef<FDatabasePreviewScene> PreviewScene;
	};

	class SDatabaseViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
	{
	public:

		SLATE_BEGIN_ARGS(SDatabaseViewport) {}
		SLATE_END_ARGS();

		void Construct(const FArguments& InArgs, const FDatabaseViewportRequiredArgs& InRequiredArgs);
		virtual ~SDatabaseViewport() {}

		// ~ICommonEditorViewportToolbarInfoProvider interface
		virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
		virtual TSharedPtr<FExtender> GetExtenders() const override;
		virtual void OnFloatingButtonClicked() override;
		// ~End of ICommonEditorViewportToolbarInfoProvider interface

	protected:

		// ~SEditorViewport interface
		virtual void BindCommands() override;
		virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
		virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
		// ~End of SEditorViewport interface

		/** The viewport toolbar */
		TSharedPtr<SPoseSearchDatabaseViewportToolBar> ViewportToolbar;

		/** Viewport client */
		TSharedPtr<FDatabaseViewportClient> ViewportClient;

		/** The preview scene that we are viewing */
		TWeakPtr<FDatabasePreviewScene> PreviewScenePtr;

		/** Asset editor toolkit we are embedded in */
		TWeakPtr<FDatabaseEditorToolkit> AssetEditorToolkitPtr;
	};
}
