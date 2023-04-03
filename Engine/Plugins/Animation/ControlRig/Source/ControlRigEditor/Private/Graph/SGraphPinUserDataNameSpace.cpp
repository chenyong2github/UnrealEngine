// Copyright Epic Games, Inc. All Rights Reserved.


#include "Graph/SGraphPinUserDataNameSpace.h"
#include "Graph/ControlRigGraph.h"
#include "ControlRigAssetUserData.h"
#include "Widgets/Layout/SBox.h"
#include "ScopedTransaction.h"

void SGraphPinUserDataNameSpace::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget>	SGraphPinUserDataNameSpace::GetDefaultValueWidget()
{
	TSharedPtr<FString> InitialSelected;
	TArray<TSharedPtr<FString>>& LocalNameSpaces = GetNameSpaces();
	for (TSharedPtr<FString> Item : LocalNameSpaces)
	{
		if (Item->Equals(GetNameSpaceText().ToString()))
		{
			InitialSelected = Item;
		}
	}

	return SNew(SBox)
		.MinDesiredWidth(150)
		.MaxDesiredWidth(400)
		[
			SAssignNew(NameComboBox, SControlRigGraphPinEditableNameValueWidget)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.OptionsSource(&NameSpaces)
				.OnGenerateWidget(this, &SGraphPinUserDataNameSpace::MakeNameSpaceItemWidget)
				.OnSelectionChanged(this, &SGraphPinUserDataNameSpace::OnNameSpaceChanged)
				.OnComboBoxOpening(this, &SGraphPinUserDataNameSpace::OnNameSpaceComboBox)
				.InitiallySelectedItem(InitialSelected)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SGraphPinUserDataNameSpace::GetNameSpaceText)
				]
		];
}

FText SGraphPinUserDataNameSpace::GetNameSpaceText() const
{
	return FText::FromString( GraphPinObj->GetDefaultAsString() );
}

void SGraphPinUserDataNameSpace::SetNameSpaceText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/)
{
	if(!GraphPinObj->GetDefaultAsString().Equals(NewTypeInValue.ToString()))
	{
		const FScopedTransaction Transaction( NSLOCTEXT("GraphEditor", "ChangeNameSpacePinValue", "Change Bone Name Pin Value" ) );
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewTypeInValue.ToString());
	}
}

TSharedRef<SWidget> SGraphPinUserDataNameSpace::MakeNameSpaceItemWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem));// .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
}

void SGraphPinUserDataNameSpace::OnNameSpaceChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();
		SetNameSpaceText(FText::FromString(NewValue), ETextCommit::OnEnter);
	}
}

void SGraphPinUserDataNameSpace::OnNameSpaceComboBox()
{
	TSharedPtr<FString> CurrentlySelected;
	TArray<TSharedPtr<FString>>& LocalNameSpaces = GetNameSpaces();
	for (TSharedPtr<FString> Item : LocalNameSpaces)
	{
		if (Item->Equals(GetNameSpaceText().ToString()))
		{
			CurrentlySelected = Item;
		}
	}

	NameComboBox->SetSelectedItem(CurrentlySelected);
}

TArray<TSharedPtr<FString>>& SGraphPinUserDataNameSpace::GetNameSpaces()
{
	NameSpaces.Reset();

	TArray<FString> NameSpaceLookup;
	if(const UBlueprint* Blueprint = GraphPinObj->GetOwningNode()->GetTypedOuter<UBlueprint>())
	{
		if(const UObject* DebuggedObject = Blueprint->GetObjectBeingDebugged())
		{
			if(DebuggedObject->Implements<UInterface_AssetUserData>())
			{
				const IInterface_AssetUserData* AssetUserDataHost = CastChecked<IInterface_AssetUserData>(DebuggedObject);
				if(const TArray<UAssetUserData*>* UserDataArray = AssetUserDataHost->GetAssetUserDataArray())
				{
					for(const UAssetUserData* UserData : *UserDataArray)
					{
						if(const UNameSpacedUserData* NameSpacedUserData = Cast<UNameSpacedUserData>(UserData))
						{
							const FString& NameSpace = NameSpacedUserData->NameSpace; 
							if(!NameSpaceLookup.Contains(NameSpace))
							{
								NameSpaceLookup.Add(NameSpace);
								NameSpaces.Add(MakeShared<FString>(NameSpace));
							}
						}
					}
				}
			}
		}
	}

	return NameSpaces;
}
