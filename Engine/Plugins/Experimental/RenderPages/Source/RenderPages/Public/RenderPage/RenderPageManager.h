// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPageManager.generated.h"


class URenderPage;
class URenderPageCollection;
class URenderPagePropRemoteControl;
class URenderPagesMoviePipelineRenderJob;
class UTexture2D;


/**
 * This struct keeps track of the values of the properties before new values were applied, so we can rollback to the previous state.
 */
USTRUCT()
struct RENDERPAGES_API FRenderPageManagerPreviousPagePropValues
{
	GENERATED_BODY()

public:
	FRenderPageManagerPreviousPagePropValues() = default;
	FRenderPageManagerPreviousPagePropValues(const TMap<TObjectPtr<URenderPagePropRemoteControl>, FRenderPageRemoteControlPropertyData>& InRemoteControlData)
		: RemoteControlData(InRemoteControlData)
	{}

public:
	/** The previous values of the remote control properties. */
	UPROPERTY()
	TMap<TObjectPtr<URenderPagePropRemoteControl>, FRenderPageRemoteControlPropertyData> RemoteControlData;
};


namespace UE::RenderPages
{
	/** A delegate for when FRenderPageManager::RenderPreviewFrame has finished. */
	DECLARE_DELEGATE_OneParam(FRenderPageManagerRenderPreviewFrameArgsCallback, bool /*bSuccess*/);


	/**
	 * The arguments for the FRenderPageManager::RenderPreviewFrame function.
	 */
	struct RENDERPAGES_API FRenderPageManagerRenderPreviewFrameArgs
	{
	public:
		/** Whether it should run invisibly (so without any UI elements popping up during rendering) or not. */
		bool bHeadless = false;

		/** The render page collection of the given render pages that will be rendered. */
		TObjectPtr<URenderPageCollection> PageCollection = nullptr;

		/** The specific render page that will be rendered. */
		TObjectPtr<URenderPage> Page = nullptr;

		/** The specific frame number that will be rendered. */
		TOptional<int32> Frame;

		/** The resolution it will be rendered in. */
		FIntPoint Resolution = FIntPoint(0, 0);

		/** The texture to reuse for rendering (performance optimization, prevents a new UTexture2D from having to be created, will only be used if the resolution of this texture matches the resolution it will be rendering in). */
		TObjectPtr<UTexture2D> ReusingTexture2D = nullptr;

		/** The delegate for when the rendering has finished. */
		FRenderPageManagerRenderPreviewFrameArgsCallback Callback;
	};


	/**
	 * The singleton class that manages the render pages.
	 * 
	 * This functionality is separated from the UI in order to make it is reusable, meaning that it can also be used in other modules.
	 */
	class RENDERPAGES_API FRenderPageManager
	{
	public:
		/** A folder in which rendered frames for temporary use will be placed in. */
		inline static FString TmpRenderedFramesPath = FPaths::AutomationTransientDir() / TEXT("RenderPages");

		/** The number of characters for a generated ID. For example, a value of 4 results in IDs: "0001", "0002", etc. */
		static constexpr int32 GeneratedIdCharacterLength = 4;

	public:
		/** Creates a new page and adds it to the given collection. **/
		URenderPage* AddNewPage(URenderPageCollection* PageCollection);

		/** Copy the given pages in the given collection. **/
		URenderPage* CopyPage(URenderPageCollection* PageCollection, URenderPage* Page);

		/** Delete the given page in the given collection. **/
		void DeletePage(URenderPageCollection* PageCollection, URenderPage* Page);

		/** Relocates the given page in the given collection to the position of the given dropped-on page. **/
		bool DragDropPage(URenderPageCollection* PageCollection, URenderPage* Page, URenderPage* DroppedOnPage, const bool bAfter = true);

		/** Generates a unique page ID by finding the highest page ID and increasing it by one. **/
		FString CreateUniquePageId(URenderPageCollection* PageCollection);

		/** Finds whether given page ID already exists in the collection. **/
		bool DoesPageIdExist(URenderPageCollection* PageCollection, const FString& PageId);

		/** Batch render the current pages of the given collection. **/
		URenderPagesMoviePipelineRenderJob* CreateBatchRenderJob(URenderPageCollection* PageCollection);

		/** Batch render the current pages of the given collection. **/
		URenderPagesMoviePipelineRenderJob* RenderPreviewFrame(const FRenderPageManagerRenderPreviewFrameArgs& Args);

		/** Gets the rendered preview frame (of a rendering in which the frame number was specified). */
		UTexture2D* GetSingleRenderedPreviewFrame(URenderPage* Page, UTexture2D* ReusingTexture2D, bool& bOutReusedGivenTexture2D);

		/** Gets the rendered preview frame (of a rendering in which the frame number was specified). */
		UTexture2D* GetSingleRenderedPreviewFrame(URenderPage* Page);

		/** Gets the rendered preview frame the given frame number (of a rendering in which the frame number was not specified). */
		UTexture2D* GetRenderedPreviewFrame(URenderPage* Page, const int32 Frame, UTexture2D* ReusingTexture2D, bool& bOutReusedGivenTexture2D);

		/** Gets the rendered preview frame the given frame number (of a rendering in which the frame number was not specified). */
		UTexture2D* GetRenderedPreviewFrame(URenderPage* Page, const int32 Frame);

		/** Makes sure that all the data from the current props source is stored in all of the pages of this page collection. */
		void UpdatePagesPropValues(URenderPageCollection* PageCollection);

		/** Applies the props of the given page, also requires the page collection to be given as well (to know what props the page is using). */
		FRenderPageManagerPreviousPagePropValues ApplyPagePropValues(const URenderPageCollection* PageCollection, const URenderPage* Page);

		/** Restores the props that were previously applied, to the values they were before. */
		void RestorePagePropValues(const FRenderPageManagerPreviousPagePropValues& PreviousPropValues);

	private:
		/** The map that stores the start frame (of a render) of each rendered page. */
		TMap<FGuid, int32> StartFrameOfRenders;
	};
}
