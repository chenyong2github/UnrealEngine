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

	~FDisplayClusterRootActorDetailsCustomization();

protected:
	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder) override;
	// End IDetailCustomization interface

protected:
	void BuildLayout(IDetailLayoutBuilder& InLayoutBuilder);

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Node ID
	//////////////////////////////////////////////////////////////////////////////////////////////
	TSharedRef<SWidget> CreateCustomNodeIdWidget();
	bool RebuildNodeIdOptionsList();
	void UpdateNodeIdSelection();

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

	void OnForcePropertyWindowRebuild(UObject* Object);

protected:
	// ADisplayClusterRootActor on which we're acting
	TWeakObjectPtr<ADisplayClusterRootActor> EditedObject;
	IDetailLayoutBuilder* LayoutBuilder;

	FDelegateHandle ForcePropertyWindowRebuildHandle;
};
