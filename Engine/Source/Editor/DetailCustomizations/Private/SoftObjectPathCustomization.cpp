// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoftObjectPathCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/GameViewportClient.h"
#include "AssetData.h"
#include "PropertyHandle.h"
#include "PropertyCustomizationHelpers.h"

void FSoftObjectPathCustomization::CustomizeHeader( TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
	StructPropertyHandle = InStructPropertyHandle;
	
	HeaderRow
	.NameContent()
	[
		InStructPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	.MaxDesiredWidth(0.0f)
	[
		// Add an object entry box.  Even though this isn't an object entry, we will simulate one
		SNew( SObjectPropertyEntryBox )
		.PropertyHandle( InStructPropertyHandle )
		.ThumbnailPool( StructCustomizationUtils.GetThumbnailPool() )
	];

	// This avoids making duplicate reset boxes
	StructPropertyHandle->MarkResetToDefaultCustomized();
}

void FSoftObjectPathCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}
