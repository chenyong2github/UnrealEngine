// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithImportInfoCustomization.h"

#include "DatasmithAssetImportData.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "AssetImportDataCustomization"

TSharedRef<IPropertyTypeCustomization> FDatasmithImportInfoCustomization::MakeInstance()
{
	return MakeShareable(new FDatasmithImportInfoCustomization);
}

void FDatasmithImportInfoCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	FSlateFontInfo Font = IDetailLayoutBuilder::GetDetailFont();

	FDatasmithImportInfo* Info = GetEditStruct();
	if (Info)
	{
		const FText SourceUriText = LOCTEXT("SourceUri", "Source Uri");
		const FText SourceHashText = LOCTEXT("SourceHash", "Source Hash");

		FText SourceUriLabel = SourceUriText;

		ChildBuilder.AddCustomRow(SourceUriLabel)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(SourceUriLabel)
			.Font(Font)
		]
		.ValueContent()
		.HAlign(HAlign_Fill)
		.MaxDesiredWidth(TOptional<float>())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.IsReadOnly(false)
				.Text(this, &FDatasmithImportInfoCustomization::GetUriText)
				.ToolTipText(this, &FDatasmithImportInfoCustomization::GetUriText)
				.OnTextCommitted(this, &FDatasmithImportInfoCustomization::OnSourceUriChanged)
				.Font(Font)
			]
		];
	}
}

FText FDatasmithImportInfoCustomization::GetUriText() const
{
	FDatasmithImportInfo* Info = GetEditStruct();
	if (Info)
	{
		return FText::FromString(Info->SourceUri);
	}
	return LOCTEXT("NoUriFound", "No Source Uri Set");
}

FDatasmithImportInfo* FDatasmithImportInfoCustomization::GetEditStruct() const
{
	TArray<FDatasmithImportInfo*> AssetImportInfo;

	if (PropertyHandle->IsValidHandle())
	{
		PropertyHandle->AccessRawData(reinterpret_cast<TArray<void*>&>(AssetImportInfo));
	}

	if (AssetImportInfo.Num() == 1)
	{
		return AssetImportInfo[0];
	}
	return nullptr;
}


UObject* FDatasmithImportInfoCustomization::GetOuterClass() const
{
	static TArray<UObject*> OuterObjects;
	OuterObjects.Reset();

	PropertyHandle->GetOuterObjects(OuterObjects);

	return OuterObjects.Num() ? OuterObjects[0] : nullptr;
}


class FImportDataSourceFileTransactionScope
{
public:
	FImportDataSourceFileTransactionScope(FText TransactionName, UObject* InOuterObject)
	{
		check(InOuterObject);
		OuterObject = InOuterObject;
		FScopedTransaction Transaction(TransactionName);

		bIsTransactionnal = (OuterObject->GetFlags() & RF_Transactional) > 0;
		if (!bIsTransactionnal)
		{
			OuterObject->SetFlags(RF_Transactional);
		}

		OuterObject->Modify();
	}

	~FImportDataSourceFileTransactionScope()
	{
		if (!bIsTransactionnal)
		{
			//Restore initial transactional flag value.
			OuterObject->ClearFlags(RF_Transactional);
		}
		OuterObject->MarkPackageDirty();
	}
private:
	bool bIsTransactionnal;
	UObject* OuterObject;
};

void FDatasmithImportInfoCustomization::OnSourceUriChanged(const FText& NewText, ETextCommit::Type) const
{
	UObject* OuterObject = GetOuterClass();
	FDatasmithImportInfo* Info = GetEditStruct();

	if (Info && OuterObject)
	{
		FImportDataSourceFileTransactionScope TransactionScope(LOCTEXT("SourceUriChanged", "Change Source URI"), OuterObject);

		Info->SourceUri = NewText.ToString();
		Info->SourceHash.Empty();

		// Broadcasting property change to force refresh the asset registry tag and notify systems monitoring the URI.
		FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(OuterObject, EmptyPropertyChangedEvent);
	}
}


#undef LOCTEXT_NAMESPACE
