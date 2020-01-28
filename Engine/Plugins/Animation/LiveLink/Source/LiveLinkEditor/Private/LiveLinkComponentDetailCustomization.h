// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"
#include "LiveLinkRole.h"
#include "Styling/SlateColor.h"
#include "Templates/SubclassOf.h"

#include "LiveLinkComponentController.h"


class IDetailLayoutBuilder;
class IPropertyHandle;
class SWidget;


/**
* Customizes a ULiveLinkComponentController details
*/
class FLiveLinkComponentDetailCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FLiveLinkComponentDetailCustomization>();
	}

	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization interface

protected:
	void OnSubjectRepresentationPropertyChanged();
	TSharedRef<SWidget> HandleControllerComboButton(TSharedPtr<IPropertyHandle> KeyHandle) const;
	void HandleControllerSelection(TSubclassOf<ULiveLinkRole> RoleClass, TWeakObjectPtr<UClass> SelectedControllerClass) const;
	bool IsControllerItemSelected(FName Item, TSubclassOf<ULiveLinkRole> RoleClass) const;
	FSlateColor HandleControllerStatusColorAndOpacity(TSubclassOf<ULiveLinkRole> RoleClassEntry) const;
	FText HandleControllerStatusText(TSubclassOf<ULiveLinkRole> RoleClassEntry) const;
	FText HandleControllerStatusToolTipText(TSubclassOf<ULiveLinkRole> RoleClassEntry) const;
	TSharedRef<SWidget> BuildControllerNameWidget(TSharedPtr<IPropertyHandle> ControllersProperty, TSubclassOf<ULiveLinkRole> RoleClass) const;
	TSharedRef<SWidget> BuildControllerValueWidget(TSharedPtr<IPropertyHandle> RoleKeyPropertyHandle, TSubclassOf<ULiveLinkRole> RoleClass, const FText& ControllerName) const;
	void ForceRefreshDetails();

protected:
	/** LiveLinkComponent on which we're acting */
	TWeakObjectPtr<ULiveLinkComponentController> EditedObject;

	/** Keep a reference to force refresh the layout */
	IDetailLayoutBuilder* DetailLayout = nullptr;
};
