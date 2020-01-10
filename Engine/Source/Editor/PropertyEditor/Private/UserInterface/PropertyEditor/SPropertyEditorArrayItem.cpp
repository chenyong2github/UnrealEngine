// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UserInterface/PropertyEditor/SPropertyEditorArrayItem.h"
#include "UObject/UnrealType.h"
#include "PropertyNode.h"
#include "Widgets/Text/STextBlock.h"
#include "Presentation/PropertyEditor/PropertyEditor.h"

void SPropertyEditorArrayItem::Construct( const FArguments& InArgs, const TSharedRef< class FPropertyEditor>& InPropertyEditor )
{
	static const FName TitlePropertyFName = FName(TEXT("TitleProperty"));

	PropertyEditor = InPropertyEditor;

	ChildSlot
	.Padding( 0.0f, 0.0f, 5.0f, 0.0f )
	[
		SNew( STextBlock )
		.Text( this, &SPropertyEditorArrayItem::GetValueAsString )
		.Font( InArgs._Font )
	];

	SetEnabled( TAttribute<bool>( this, &SPropertyEditorArrayItem::CanEdit ) );

	// if this is a struct property, try to find a representative element to use as our stand in
	if (PropertyEditor->PropertyIsA( FStructProperty::StaticClass() ))
	{
		const FProperty* MainProperty = PropertyEditor->GetProperty();
		const FProperty* ArrayProperty = MainProperty ? MainProperty->GetOwner<const FProperty>() : nullptr;
		if (ArrayProperty) // should always be true
		{
			// see if this structure has a TitleProperty we can use to summarize
			const FString& RepPropertyName = ArrayProperty->GetMetaData(TitlePropertyFName);
			if (!RepPropertyName.IsEmpty())
			{
				TitlePropertyHandle = PropertyEditor->GetPropertyHandle()->GetChildHandle(FName(*RepPropertyName), false);
			}
		}
	}
}

void SPropertyEditorArrayItem::GetDesiredWidth( float& OutMinDesiredWidth, float& OutMaxDesiredWidth )
{
	OutMinDesiredWidth = 130.0f;
	OutMaxDesiredWidth = 500.0f;
}

bool SPropertyEditorArrayItem::Supports( const TSharedRef< class FPropertyEditor >& PropertyEditor )
{
	const TSharedRef< FPropertyNode > PropertyNode = PropertyEditor->GetPropertyNode();
	const FProperty* Property = PropertyEditor->GetProperty();

	if (!CastField<const FClassProperty>(Property) && PropertyNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly))
	{
		if (Property->GetOwner<FArrayProperty>() &&
			!(Property->GetOwner<FArrayProperty>()->PropertyFlags & CPF_EditConst))
		{
			return true;
		}

		if (Property->GetOwner<FMapProperty>() &&
			!(Property->GetOwner<FMapProperty>()->PropertyFlags & CPF_EditConst))
		{
			return true;
		}
	}
	return false;
}

FText SPropertyEditorArrayItem::GetValueAsString() const
{
	if (TitlePropertyHandle.IsValid())
	{
		FText TextOut;
		if (FPropertyAccess::Success == TitlePropertyHandle->GetValueAsDisplayText(TextOut))
		{
			return TextOut;
		}
	}
	
	if( PropertyEditor->GetProperty() && PropertyEditor->PropertyIsA( FStructProperty::StaticClass() ) )
	{
		return FText::Format( NSLOCTEXT("PropertyEditor", "NumStructItems", "{0} members"), FText::AsNumber( PropertyEditor->GetPropertyNode()->GetNumChildNodes() ) );
	}

	return PropertyEditor->GetValueAsDisplayText();
}

bool SPropertyEditorArrayItem::CanEdit() const
{
	return PropertyEditor.IsValid() ? !PropertyEditor->IsEditConst() : true;
}
