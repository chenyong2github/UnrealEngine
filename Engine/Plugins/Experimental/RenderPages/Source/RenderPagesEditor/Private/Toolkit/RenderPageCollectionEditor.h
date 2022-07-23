// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRenderPageCollectionEditor.h"


class FSpawnTabArgs;
class IToolkitHost;
class SDockTab;
class URenderPageCollection;
class URenderPage;
class URenderPagesBlueprint;
class URenderPagesMoviePipelineRenderJob;

namespace UE::RenderPages::Private
{
	class FRenderPagesBlueprintEditorToolbar;
}


namespace UE::RenderPages::Private
{
	DECLARE_MULTICAST_DELEGATE_TwoParams(FRenderPagesEditorClosed, const UE::RenderPages::IRenderPageCollectionEditor*, URenderPagesBlueprint*);


	/**
	 * The render page editor implementation.
	 */
	class FRenderPageCollectionEditor : public IRenderPageCollectionEditor
	{
	public:
		void InitRenderPagesEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, URenderPagesBlueprint* InRenderPagesBlueprint);

	public:
		FRenderPageCollectionEditor();
		virtual ~FRenderPageCollectionEditor() override;

		//~ Begin FBlueprintEditor Interface
		virtual void CreateDefaultCommands() override;
		virtual UBlueprint* GetBlueprintObj() const override;
		virtual FGraphAppearanceInfo GetGraphAppearance(UEdGraph* InGraph) const override;
		virtual bool IsInAScriptingMode() const override { return true; }
		virtual void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated) override;
		virtual void SetupGraphEditorEvents(UEdGraph* InGraph, SGraphEditor::FGraphEditorEvents& InEvents) override;
		virtual void RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated = false) override;

		virtual void Compile() override;
		//~ End FBlueprintEditor Interface

		//~ Begin IRenderPageCollectionEditor Interface
		virtual URenderPagesBlueprint* GetRenderPagesBlueprint() const override;
		virtual URenderPageCollection* GetInstance() const override;
		virtual TSharedPtr<FRenderPagesBlueprintEditorToolbar> GetRenderPagesToolbarBuilder() override { return RenderPagesToolbar; }
		virtual bool IsBatchRendering() const override;
		virtual URenderPagesMoviePipelineRenderJob* GetBatchRenderJob() const override { return BatchRenderJob; }
		virtual bool IsPreviewRendering() const override;
		virtual URenderPagesMoviePipelineRenderJob* GetPreviewRenderJob() const override { return PreviewRenderJob; }
		virtual void SetPreviewRenderJob(URenderPagesMoviePipelineRenderJob* Job) override { PreviewRenderJob = Job; }
		virtual void MarkAsModified() override;
		virtual TArray<URenderPage*> GetSelectedRenderPages() const override;
		virtual void SetSelectedRenderPages(const TArray<URenderPage*>& RenderPages) override;
		//~ End IRenderPageCollectionEditor Interface

		//~ Begin IToolkit Interface
		virtual FName GetToolkitFName() const override;
		virtual FText GetBaseToolkitName() const override;
		virtual FString GetDocumentationLink() const override;
		virtual FText GetToolkitToolTipText() const override;
		virtual FLinearColor GetWorldCentricTabColorScale() const override;
		virtual FString GetWorldCentricTabPrefix() const override;
		virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;
		//~ End IToolkit Interface

		//~ Begin FTickableEditorObject Interface
		virtual void Tick(float DeltaTime) override;
		virtual TStatId GetStatId() const override;
		//~ End FTickableEditorObject Interface

		/** Immediately rebuilds the render page collection that is being shown in the editor. */
		void RefreshInstance();

		/** The delegate that will fire when this editor closes. */
		FRenderPagesEditorClosed& OnRenderPageCollectionEditorClosed() { return RenderPagesEditorClosedDelegate; }

	private:
		/** Called whenever the blueprint is structurally changed. */
		virtual void OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled = false) override;

	protected:
		//~ Begin FGCObject Interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		//~ End FGCObject Interface

		//~ Begin FNotifyHook Interface
		virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
		virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
		virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) override;
		//~ End FNotifyHook Interface

	protected:
		/** Binds the FRenderPagesEditorCommands commands to functions in this editor. */
		void BindCommands();

		/** Creates and adds a new render page to the currently viewing render page collection. */
		void AddPageAction();

		/** Copies the selected render page(s) and adds them to the currently viewing render page collection. */
		void CopyPageAction();

		/** Removes the currently selected render page(s) from the currently viewing render page collection. */
		void DeletePageAction();

		/** Renders all the currently enabled render pages. */
		void BatchRenderListAction();

		/** The callback for when the batch render list action finishes. */
		void OnBatchRenderListActionFinished(URenderPagesMoviePipelineRenderJob* RenderJob, bool bSuccess);

		/** Undo the last action. */
		void UndoAction();

		/** Redo the last action that was undone. */
		void RedoAction();

	private:
		/** Extends the menu. */
		void ExtendMenu();

		/** Extends the toolbar. */
		void ExtendToolbar();

		/** Fills the toolbar with content. */
		void FillToolbar(FToolBarBuilder& ToolbarBuilder);

		/** Destroy the render page collection instance that is currently visible in the editor. */
		void DestroyInstance();

		/** Makes a newly compiled/opened render page collection instance visible in the editor. */
		void UpdateInstance(UBlueprint* InBlueprint, bool bInForceFullUpdate);

		/** Wraps the normal blueprint editor's action menu creation callback. */
		FActionMenuContent HandleCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	private:
		/** The delegate that will be fired when this editor closes. */
		FRenderPagesEditorClosed RenderPagesEditorClosedDelegate;

		/** The toolbar builder that is used to customize the toolbar of this editor. */
		TSharedPtr<FRenderPagesBlueprintEditorToolbar> RenderPagesToolbar;

	protected:
		/** The extender to pass to the level editor to extend it's window menu. */
		TSharedPtr<FExtender> MenuExtender;

		/** The toolbar extender of this editor. */
		TSharedPtr<FExtender> ToolbarExtender;

		/** The blueprint instance that's currently visible in the editor. */
		TObjectPtr<URenderPagesBlueprint> PreviewBlueprint;

		/** The current render page collection instance that's visible in the editor. */
		mutable TWeakObjectPtr<URenderPageCollection> RenderPageCollectionWeakPtr;

		/** The IDs of the currently selected render pages. */
		TSet<FGuid> SelectedRenderPagesIds;

		/** True if it should call BatchRenderListAction() next frame. */
		bool bRunRenderNewBatch;

		/** The current batch rendering job, if any. */
		TObjectPtr<URenderPagesMoviePipelineRenderJob> BatchRenderJob;

		/** The current preview rendering job, if any. */
		TObjectPtr<URenderPagesMoviePipelineRenderJob> PreviewRenderJob;
	};
}
