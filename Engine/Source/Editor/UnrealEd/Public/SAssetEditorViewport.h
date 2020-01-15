// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "SEditorViewport.h"

class FEditorViewportClient;
class FAssetEditorViewportLayout;

class UNREALED_API SAssetEditorViewport : public SEditorViewport
{
public:

	SLATE_BEGIN_ARGS(SAssetEditorViewport)
		: _ViewportType(LVT_Perspective)
		, _Realtime(false)
	{
	}

	SLATE_ARGUMENT(TWeakPtr<class FEditorModeTools>, EditorModeTools)
		SLATE_ARGUMENT(TSharedPtr<class FAssetEditorViewportLayout>, ParentLayout)
		SLATE_ARGUMENT(TSharedPtr<FEditorViewportClient>, EditorViewportClient)
		SLATE_ARGUMENT(ELevelViewportType, ViewportType)
		SLATE_ARGUMENT(bool, Realtime)
		SLATE_ARGUMENT(FName, ConfigKey)
	SLATE_END_ARGS()


	void OnSetViewportConfiguration(FName ConfigurationName);
	bool IsViewportConfigurationSet(FName ConfigurationName) const;

	void GenerateLayoutMenu(FMenuBuilder& MenuBuilder) const;

	TWeakPtr<class FAssetEditorViewportLayout> ParentLayout;

protected:
	virtual void BindCommands() override;

	TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override {
		EditorViewportClient = MakeShareable(new FEditorViewportClient(nullptr));

		return EditorViewportClient.ToSharedRef();
	};


private:

	// Viewport client
	TSharedPtr<FEditorViewportClient> EditorViewportClient;

};
