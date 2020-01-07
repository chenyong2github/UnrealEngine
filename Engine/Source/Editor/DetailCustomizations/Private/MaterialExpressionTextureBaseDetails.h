// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "IDetailCustomization.h"
#include "PropertyRestriction.h"

class IPropertyHandle;
class IDetailLayoutBuilder;
class SObjectPropertyEntryBox;
struct FAssetData;
class UMaterialExpressionTextureBase;
struct FPropertyChangedEvent;

class FMaterialExpressionTextureBaseDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FMaterialExpressionTextureBaseDetails();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override;

	TSharedPtr<FPropertyRestriction> EnumRestriction;
	TWeakObjectPtr<UMaterialExpressionTextureBase> Expression;
	FDelegateHandle DelegateHandle;

	void OnTextureChanged();
	void OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
};

