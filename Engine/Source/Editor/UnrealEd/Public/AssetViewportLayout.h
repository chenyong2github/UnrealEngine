// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class IAssetViewportLayoutEntity : public TSharedFromThis<IAssetViewportLayoutEntity>
{
public:
	/** Virtual destruction */
	virtual ~IAssetViewportLayoutEntity() {};

	/** Return a widget that is represents this entity */
	virtual TSharedRef<SWidget> AsWidget() const = 0;

	/** Set keyboard focus to this viewport entity */
	virtual void SetKeyboardFocus() = 0;

	/** Called when the parent layout is being destroyed */
	virtual void OnLayoutDestroyed() = 0;

	/** Called to save this item's settings in the specified config section */
	virtual void SaveConfig(const FString& ConfigSection) = 0;

	/** Get the type of this viewport as a name */
	virtual FName GetType() const = 0;

	/** Take a high res screen shot of viewport entity */
	virtual void TakeHighResScreenShot() const = 0;
};

class FAssetViewportLayout 
{
public:
	/** Virtual destruction */
	virtual ~FAssetViewportLayout() {};

	/**
	* @return All the viewports in this configuration
	*/
	const TMap< FName, TSharedPtr<IAssetViewportLayoutEntity> >& GetViewports() const { return Viewports; }

	/**
	* Saves viewport layout information between editor sessions
	*/
	virtual void SaveLayoutString(const FString& LayoutString) const = 0;

	virtual const FName& GetLayoutTypeName() const = 0;

protected:
	/** List of all of the viewports in this layout, keyed on their config key */
	TMap< FName, TSharedPtr< IAssetViewportLayoutEntity > > Viewports;


};
