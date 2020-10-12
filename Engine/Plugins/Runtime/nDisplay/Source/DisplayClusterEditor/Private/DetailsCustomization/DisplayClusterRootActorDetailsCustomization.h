// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class ADisplayClusterRootActor;
class IDetailCategoryBuilder;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SWidget;
class SSearchableComboBox;


class FDisplayClusterRootActorDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& LayoutBuilder) override;
	// End IDetailCustomization interface

protected:
	void BuildLayout();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Node ID
	//////////////////////////////////////////////////////////////////////////////////////////////
	void AddNodeIdRow();
	bool RebuildNodeIdOptionsList();
	void OnNodeIdSelected(TSharedPtr<FString> NodeId, ESelectInfo::Type SelectInfo);
	FText GetSelectedNodeIdText() const;

protected:
	TSharedPtr<FString> NodeIdOptionNone;
	TSharedPtr<FString> NodeIdOptionAll;

	TSharedPtr<IPropertyHandle>     PropertyNodeId;
	TArray<TSharedPtr<FString>>     NodeIdOptions;
	TSharedPtr<SSearchableComboBox> NodeIdComboBox;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Default camera ID
	//////////////////////////////////////////////////////////////////////////////////////////////
	void AddDefaultCameraRow();
	bool RebuildDefaultCameraOptionsList();
	void OnDefaultCameraSelected(TSharedPtr<FString> CameraId, ESelectInfo::Type SelectInfo);
	FText GetSelectedDefaultCameraText() const;

protected:
	TSharedPtr<IPropertyHandle>     PropertyDefaultCamera;
	TArray<TSharedPtr<FString>>     DefaultCameraOptions;
	TSharedPtr<SSearchableComboBox> DefaultCameraComboBox;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Internals
	//////////////////////////////////////////////////////////////////////////////////////////////

	// Handle config changes
	void OnPreviewConfigChanged();
	// Create combobox widget
	TSharedRef<SWidget> CreateComboWidget(TSharedPtr<FString> InItem);

protected:
	// ADisplayClusterRootActor on which we're acting
	TWeakObjectPtr<ADisplayClusterRootActor> EditedObject;

	// Keep a reference to force refresh the layout
	IDetailLayoutBuilder* LayoutBuilder = nullptr;
	// The layout category we'll be dealing with
	IDetailCategoryBuilder* CategoryPreview;
};
