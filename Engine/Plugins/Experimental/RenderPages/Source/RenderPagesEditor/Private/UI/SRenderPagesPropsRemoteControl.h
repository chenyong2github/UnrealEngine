// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RemoteControlPreset.h"
#include "UI/SRenderPagesPropsBase.h"
#include "RenderPage/RenderPagePropsSource.h"
#include "Components/SRenderPagesRemoteControlTreeNode.h"

class URenderPage;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}

namespace UE::RenderPages::Private
{
	struct SRenderPagesRemoteControlTreeNode;
	struct FRenderPagesRemoteControlGenerateWidgetArgs;
}


namespace UE::RenderPages::Private
{
	/**
	 * The page props implementation for remote control fields.
	 */
	class SRenderPagesPropsRemoteControl : public SRenderPagesPropsBase
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPropsRemoteControl) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPagePropsSourceRemoteControl* InPropsSource);

		/** Obtain the latest prop values and refreshes the content of this widget. */
		void UpdateStoredValuesAndRefresh();

		/** Refreshes the content of this widget. */
		void Refresh();

	private:
		void OnRemoteControlEntitiesExposed(URemoteControlPreset* Preset, const FGuid& EntityId) { UpdateStoredValuesAndRefresh(); }
		void OnRemoteControlEntitiesUnexposed(URemoteControlPreset* Preset, const FGuid& EntityId) { UpdateStoredValuesAndRefresh(); }
		void OnRemoteControlEntitiesUpdated(URemoteControlPreset* Preset, const TSet<FGuid>& ModifiedEntities) { UpdateStoredValuesAndRefresh(); }
		void OnRemoteControlExposedPropertiesModified(URemoteControlPreset* Preset, const TSet<FGuid>& ModifiedProperties);

	private:
		/** Returns the currently selected page if 1 page is currently selected, returns nullptr otherwise. */
		URenderPage* GetSelectedPage();

		/** Obtains the value (as bytes) of the given prop (the given remote control entity), returns true if it succeeded, returns false otherwise. */
		bool GetSelectedPageFieldValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, TArray<uint8>& OutBinaryArray);

		/** Sets the value of the given prop (the given remote control entity) with the given value (as bytes), returns true if it succeeded, returns false otherwise. */
		bool SetSelectedPageFieldValue(const TSharedPtr<FRemoteControlEntity>& RemoteControlEntity, const TArray<uint8>& BinaryArray);

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** The props source control. */
		TObjectPtr<URenderPagePropsSourceRemoteControl> PropsSource;

		/** The widget that lists the property rows. */
		TSharedPtr<SVerticalBox> RowWidgetsContainer;

		/** The current property rows, needed to be able to refresh them, as well as to prevent garbage collection. */
		TArray<TSharedPtr<SRenderPagesRemoteControlTreeNode>> RowWidgets;

		/** The arguments that were used to create the current property rows, needed to not recreate the property rows unnecessarily. */
		TArray<FRenderPagesRemoteControlGenerateWidgetArgs> RowWidgetsArgs;
	};
}
