// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Types/SlateEnums.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class FLandscapeSettingsDetails : public IDetailCustomization
{
public:
	~FLandscapeSettingsDetails();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	/** End IDetailCustomization interface */

	void OnEditorExperimentalSettingsChanged(FName PropertyName);
private:

	/** Use MakeInstance to create an instance of this class */
	FLandscapeSettingsDetails();

	IDetailLayoutBuilder* DetailLayoutBuilder;
};
