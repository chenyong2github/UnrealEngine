// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeEditorPipelineDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailGroup.h"
#include "Nodes/InterchangeBaseNode.h"
#include "PropertyHandle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "InterchangeEditorPipelineDetails"

FInterchangeBaseNodeDetailsCustomization::FInterchangeBaseNodeDetailsCustomization()
{
	InterchangeBaseNode = nullptr;
	CachedDetailBuilder = nullptr;
}

FInterchangeBaseNodeDetailsCustomization::~FInterchangeBaseNodeDetailsCustomization()
{
}

void FInterchangeBaseNodeDetailsCustomization::RefreshCustomDetail()
{
	if (CachedDetailBuilder)
	{
		CachedDetailBuilder->ForceRefreshDetails();
	}
}

TSharedRef<IDetailCustomization> FInterchangeBaseNodeDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FInterchangeBaseNodeDetailsCustomization);
}

void FInterchangeBaseNodeDetailsCustomization::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	CachedDetailBuilder = &DetailBuilder;
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);
	check(EditingObjects.Num() == 1);

	InterchangeBaseNode = Cast<UInterchangeBaseNode>(EditingObjects[0].Get());

	if (!ensure(InterchangeBaseNode))
	{
		return;
	}

	TArray< UE::Interchange::FAttributeKey> AttributeKeys;
	InterchangeBaseNode->GetAttributeKeys(AttributeKeys);

	TMap<FString, TArray< UE::Interchange::FAttributeKey>> AttributesPerCategory;
	for (UE::Interchange::FAttributeKey& AttributeKey : AttributeKeys)
	{
		if (InterchangeBaseNode->ShouldHideAttribute(AttributeKey))
		{
			//Skip attribute we should hide
			continue;
		}
		const FString CategoryName = InterchangeBaseNode->GetAttributeCategory(AttributeKey);
		TArray< UE::Interchange::FAttributeKey>& CategoryAttributeKeys = AttributesPerCategory.FindOrAdd(CategoryName);
		CategoryAttributeKeys.Add(AttributeKey);
	}

	//Add all categories
	for (TPair<FString, TArray< UE::Interchange::FAttributeKey>>& CategoryAttributesPair : AttributesPerCategory)
	{
		FName CategoryName = FName(*CategoryAttributesPair.Key);
		IDetailCategoryBuilder& AttributeCategoryBuilder = DetailBuilder.EditCategory(CategoryName, FText::GetEmpty());
		for (UE::Interchange::FAttributeKey& AttributeKey : CategoryAttributesPair.Value)
		{
			AddAttributeRow(AttributeKey, AttributeCategoryBuilder);
		}
	}
}

void FInterchangeBaseNodeDetailsCustomization::AddAttributeRow(UE::Interchange::FAttributeKey& AttributeKey, IDetailCategoryBuilder& AttributeCategory)
{
	if (!ensure(InterchangeBaseNode))
	{
		return;
	}
	const UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	switch (AttributeType)
	{
		case UE::Interchange::EAttributeTypes::Bool:
		{
			BuildBoolValueContent(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Double:
		{
			BuildNumberValueContent < double > (AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Float:
		{
			BuildNumberValueContent < float >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Int8:
		{
			BuildNumberValueContent < int8 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Int16:
		{
			BuildNumberValueContent < int16 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Int32:
		{
			BuildNumberValueContent < int32 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Int64:
		{
			BuildNumberValueContent < int64 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::UInt8:
		{
			BuildNumberValueContent < uint8 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::UInt16:
		{
			BuildNumberValueContent < uint16 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::UInt32:
		{
			BuildNumberValueContent < uint32 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::UInt64:
		{
			BuildNumberValueContent < uint64 >(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::String:
		{
			BuildStringValueContent<FString>(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Name:
		{
			BuildStringValueContent<FName>(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Transform:
		{
			BuildTransformValueContent(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Box:
		{
			BuildBoxValueContent(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::SoftObjectPath:
		{
			BuildStringValueContent<FSoftObjectPath>(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::Color:
		{
			BuildColorValueContent(AttributeCategory, AttributeKey);
		}
		break;

		case UE::Interchange::EAttributeTypes::LinearColor:
		{
			BuildLinearColorValueContent(AttributeCategory, AttributeKey);
		}
		break;

		default:
		{
			FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
			AttributeCategory.AddCustomRow(AttributeName)
			.NameContent()
			[
				CreateNameWidget(AttributeKey)
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UnsupportedCustomizationType", "Attribute Type Not Supported"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];
		}
	}
}

void FInterchangeBaseNodeDetailsCustomization::BuildBoolValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	check(AttributeType == UE::Interchange::EAttributeTypes::Bool);
	UE::Interchange::FAttributeStorage::TAttributeHandle<bool> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<bool>(AttributeKey);
	if (!AttributeHandle.IsValid())
	{
		CreateInvalidHandleRow(AttributeCategory, AttributeKey);
		return;
	}
	
	const FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
	FDetailWidgetRow& CustomRow = AttributeCategory.AddCustomRow(AttributeName)
	.NameContent()
	[
		CreateNameWidget(AttributeKey)
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged_Lambda([this, AttributeKey](ECheckBoxState CheckType)
				{
					const bool IsChecked = CheckType == ECheckBoxState::Checked;
					UE::Interchange::FAttributeStorage::TAttributeHandle<bool> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<bool>(AttributeKey);
					if (AttributeHandle.IsValid())
					{
						AttributeHandle.Set(IsChecked);
					}
				})
				.IsChecked_Lambda([this, AttributeKey]()
				{
					bool IsChecked = false;
					UE::Interchange::FAttributeStorage::TAttributeHandle<bool> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<bool>(AttributeKey);
					if (AttributeHandle.IsValid())
					{
						AttributeHandle.Get(IsChecked);
					}
					return IsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
			]
		]
	];
}

template<typename NumericType>
void FInterchangeBaseNodeDetailsCustomization::BuildNumberValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::FAttributeStorage::TAttributeHandle<NumericType> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<NumericType>(AttributeKey);
	if (!AttributeHandle.IsValid())
	{
		CreateInvalidHandleRow(AttributeCategory, AttributeKey);
		return;
	}

	auto GetValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->NumericType
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<NumericType> AttributeHandle = BaseNode->GetAttributeHandle<NumericType>(Key);
		//Prevent returning uninitialize value by setting it to 0
		NumericType Value = static_cast<NumericType>(0);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(Value);
		}
		return Value;
	};

	auto SetValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, NumericType Value, UE::Interchange::FAttributeKey& Key)
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<NumericType> AttributeHandle = BaseNode->GetAttributeHandle<NumericType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Set(Value);
		}
    };

	const FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
	AttributeCategory.AddCustomRow(AttributeName)
	.NameContent()
	[
		CreateNameWidget(AttributeKey)
	]
	.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeNumericWidget<NumericType>(0, GetValue, SetValue, AttributeKey)
		]
	];
}

template<typename StringType>
void FInterchangeBaseNodeDetailsCustomization::BuildStringValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::FAttributeStorage::TAttributeHandle<StringType> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<StringType>(AttributeKey);
	if (!AttributeHandle.IsValid())
	{
		CreateInvalidHandleRow(AttributeCategory, AttributeKey);
		return;
	}

	const FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
	FDetailWidgetRow& CustomRow = AttributeCategory.AddCustomRow(AttributeName)	
    .NameContent()
    [
        CreateNameWidget(AttributeKey)
    ]
    .ValueContent()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text_Lambda([this, AttributeKey]()->FText
            {
				UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
				FText ReturnText;
				if (AttributeType == UE::Interchange::EAttributeTypes::String)
				{
					UE::Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FString>(AttributeKey);
					FString Value;
					if (AttributeHandle.IsValid())
					{
						AttributeHandle.Get(Value);
					}
					ReturnText = FText::FromString(Value);
				}
				else if (AttributeType == UE::Interchange::EAttributeTypes::Name)
				{
					UE::Interchange::FAttributeStorage::TAttributeHandle<FName> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FName>(AttributeKey);
					FName Value;
					if (AttributeHandle.IsValid())
					{
						AttributeHandle.Get(Value);
					}
					ReturnText = FText::FromName(Value);
				}
				else if (AttributeType == UE::Interchange::EAttributeTypes::SoftObjectPath)
				{
					UE::Interchange::FAttributeStorage::TAttributeHandle<FSoftObjectPath> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FSoftObjectPath>(AttributeKey);
					FSoftObjectPath Value;
					if (AttributeHandle.IsValid())
					{
						AttributeHandle.Get(Value);
					}
					ReturnText = FText::FromString(Value.ToString());
				}
                return ReturnText;
            })
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
    ];
}

void FInterchangeBaseNodeDetailsCustomization::BuildTransformValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	check(AttributeType == UE::Interchange::EAttributeTypes::Transform);

	{
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FTransform>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			CreateInvalidHandleRow(AttributeCategory, AttributeKey);
			return;
		}
	}

	const bool bAdvancedProperty = false;
	const FString GroupName = InterchangeBaseNode->GetKeyDisplayName(AttributeKey);
	IDetailGroup& Group = AttributeCategory.AddGroup(FName(*GroupName), FText::FromString(GroupName), bAdvancedProperty);
	FDetailWidgetRow& GroupHeaderRow = Group.HeaderRow();
	GroupHeaderRow.NameContent().Widget = SNew(SBox)
	[
		CreateNameWidget(AttributeKey)
	];

	auto GetRotationValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->FQuat
	{
		FTransform TransformValue;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
		}
		return TransformValue.GetRotation();
	};

	auto SetRotationValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key, const FQuat& Value)
	{
		FTransform TransformValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
			TransformValue.SetRotation(Value);
			AttributeHandle.Set(TransformValue);
		}
	};

	auto GetTranslationValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->FVector
	{
		FTransform TransformValue;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
		}
		return TransformValue.GetTranslation();
	};

	auto SetTranslationValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key, const FVector& Value)
	{
		FTransform TransformValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
			TransformValue.SetTranslation(Value);
			AttributeHandle.Set(TransformValue);
		}
	};

	auto GetScale3DValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->FVector
	{
		FTransform TransformValue;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
		}
		return TransformValue.GetScale3D();
	};

	auto SetScale3DValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key, const FVector& Value)
	{
		FTransform TransformValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FTransform> AttributeHandle = BaseNode->GetAttributeHandle<FTransform>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(TransformValue);
			TransformValue.SetScale3D(Value);
			AttributeHandle.Set(TransformValue);
		}
	};

	const FString TranslationName = TEXT("Translation");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(TranslationName)
    ]
    .ValueContent()
    [
        CreateVectorWidget(GetTranslationValue, SetTranslationValue, AttributeKey)
    ];

	const FString RotationName = TEXT("Rotation");
	Group.AddWidgetRow()
	.NameContent()
	[
		CreateSimpleNameWidget(RotationName)
	]
	.ValueContent()
	[
		CreateQuaternionWidget(GetRotationValue, SetRotationValue, AttributeKey)
	];

	const FString Scale3DName = TEXT("Scale3D");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(Scale3DName)
    ]
    .ValueContent()
    [
        CreateVectorWidget(GetScale3DValue, SetScale3DValue, AttributeKey)
    ];
}

void FInterchangeBaseNodeDetailsCustomization::BuildColorValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	if (AttributeType == UE::Interchange::EAttributeTypes::Color)
	{
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FColor> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FColor>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			CreateInvalidHandleRow(AttributeCategory, AttributeKey);
			return;
		}
	}
	else
	{
		ensure(AttributeType == UE::Interchange::EAttributeTypes::Color);
		CreateInvalidHandleRow(AttributeCategory, AttributeKey);
		return;
	}
	InternalBuildColorValueContent<FColor, uint8>(AttributeCategory, AttributeKey, 255);
}

void FInterchangeBaseNodeDetailsCustomization::BuildLinearColorValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	if (AttributeType == UE::Interchange::EAttributeTypes::LinearColor)
	{
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FColor> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FColor>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			CreateInvalidHandleRow(AttributeCategory, AttributeKey);
			return;
		}
	}
	else
	{
		ensure(AttributeType == UE::Interchange::EAttributeTypes::LinearColor);
		CreateInvalidHandleRow(AttributeCategory, AttributeKey);
		return;
	}
	InternalBuildColorValueContent<FLinearColor, float>(AttributeCategory, AttributeKey, 1.0f);
}

template<typename AttributeType, typename NumericType>
void FInterchangeBaseNodeDetailsCustomization::InternalBuildColorValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey, NumericType DefaultTypeValue)
{
	const bool bAdvancedProperty = false;
	const FString GroupName = InterchangeBaseNode->GetKeyDisplayName(AttributeKey);
	IDetailGroup& Group = AttributeCategory.AddGroup(FName(*GroupName), FText::FromString(GroupName), bAdvancedProperty);
	FDetailWidgetRow& GroupHeaderRow = Group.HeaderRow();
	GroupHeaderRow.NameContent().Widget = SNew(SBox)
	[
		CreateNameWidget(AttributeKey)
	];

	auto GetRValue = [DefaultTypeValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->NumericType
	{
		AttributeType Value;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(Value);
		}
		else
		{
			//Error return white color
			return DefaultTypeValue;
		}
		return Value.R;
	};

	auto SetRValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, const NumericType& Value, UE::Interchange::FAttributeKey& Key)
	{
		AttributeType ColorValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(ColorValue);
			ColorValue.R = Value;
			AttributeHandle.Set(ColorValue);
		}
	};

	auto GetGValue = [DefaultTypeValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->NumericType
	{
		AttributeType Value;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(Value);
		}
		else
		{
			//Error return white color
			return DefaultTypeValue;
		}
		return Value.G;
	};

	auto SetGValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, const NumericType& Value, UE::Interchange::FAttributeKey& Key)
	{
		AttributeType ColorValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(ColorValue);
			ColorValue.G = Value;
			AttributeHandle.Set(ColorValue);
		}
	};

	auto GetBValue = [DefaultTypeValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->NumericType
	{
		AttributeType Value;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(Value);
		}
		else
		{
			//Error return white color
			return DefaultTypeValue;
		}
		return Value.B;
	};

	auto SetBValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, const NumericType& Value, UE::Interchange::FAttributeKey& Key)
	{
		AttributeType ColorValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(ColorValue);
			ColorValue.B = Value;
			AttributeHandle.Set(ColorValue);
		}
	};

	auto GetAValue = [DefaultTypeValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->NumericType
	{
		AttributeType Value;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(Value);
		}
		else
		{
			//Error return white color
			return DefaultTypeValue;
		}
		return Value.A;
	};

	auto SetAValue = [](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, const NumericType& Value, UE::Interchange::FAttributeKey& Key)
	{
		AttributeType ColorValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<AttributeType> AttributeHandle = BaseNode->GetAttributeHandle<AttributeType>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(ColorValue);
			ColorValue.A = Value;
			AttributeHandle.Set(ColorValue);
		}
	};

	const FString RName = TEXT("Red");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(RName)
    ]
    .ValueContent()
    [
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeNumericWidget<NumericType>(0, GetRValue, SetRValue, AttributeKey)
		]
    ];
	const FString GName = TEXT("Green");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(GName)
    ]
    .ValueContent()
    [
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeNumericWidget<NumericType>(0, GetGValue, SetGValue, AttributeKey)
		]
    ];

	const FString BName = TEXT("Blue");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(BName)
    ]
    .ValueContent()
    [
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeNumericWidget<NumericType>(0, GetBValue, SetBValue, AttributeKey)
		]
    ];

	const FString AName = TEXT("Alpha");
	Group.AddWidgetRow()
    .NameContent()
    [
        CreateSimpleNameWidget(AName)
    ]
    .ValueContent()
    [
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeNumericWidget<NumericType>(0, GetAValue, SetAValue, AttributeKey)
		]
    ];
}

void FInterchangeBaseNodeDetailsCustomization::BuildBoxValueContent(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey)
{
	UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	check(AttributeType == UE::Interchange::EAttributeTypes::Box);

	{
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FBox> AttributeHandle = InterchangeBaseNode->GetAttributeHandle<FBox>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			CreateInvalidHandleRow(AttributeCategory, AttributeKey);
			return;
		}
	}

	const bool bAdvancedProperty = false;
	const FString GroupName = InterchangeBaseNode->GetKeyDisplayName(AttributeKey);
	IDetailGroup& Group = AttributeCategory.AddGroup(FName(*GroupName), FText::FromString(GroupName), bAdvancedProperty);
	FDetailWidgetRow& GroupHeaderRow = Group.HeaderRow();
	GroupHeaderRow.NameContent().Widget = SNew(SBox)
	[
		CreateNameWidget(AttributeKey)
	];

	auto GetMinimumValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->FVector
	{
		FBox BoxValue;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FBox> AttributeHandle = BaseNode->GetAttributeHandle<FBox>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(BoxValue);
		}
		return BoxValue.Min;
	};

	auto SetMinimumValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key, const FVector& Value)
	{
		FBox BoxValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FBox> AttributeHandle = BaseNode->GetAttributeHandle<FBox>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(BoxValue);
			BoxValue.Min = Value;
			AttributeHandle.Set(BoxValue);
		}
	};

	auto GetMaximumValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->FVector
	{
		FBox BoxValue;
		const UE::Interchange::FAttributeStorage::TAttributeHandle<FBox> AttributeHandle = BaseNode->GetAttributeHandle<FBox>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(BoxValue);
		}
		return BoxValue.Max;
	};

	auto SetMaximumValue = [](UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key, const FVector& Value)
	{
		FBox BoxValue;
		UE::Interchange::FAttributeStorage::TAttributeHandle<FBox> AttributeHandle = BaseNode->GetAttributeHandle<FBox>(Key);
		if (AttributeHandle.IsValid())
		{
			AttributeHandle.Get(BoxValue);
			BoxValue.Max = Value;
			AttributeHandle.Set(BoxValue);
		}
	};

	const FString MinimumVectorName = TEXT("Minimum");
	Group.AddWidgetRow()
	.NameContent()
	[
		CreateSimpleNameWidget(MinimumVectorName)
	]
	.ValueContent()
	[
		CreateVectorWidget(GetMinimumValue, SetMinimumValue, AttributeKey)
	];

	const FString MaximumVectorName = TEXT("Maximum");
	Group.AddWidgetRow()
	.NameContent()
	[
		CreateSimpleNameWidget(MaximumVectorName)
	]
	.ValueContent()
	[
		CreateVectorWidget(GetMaximumValue, SetMaximumValue, AttributeKey)
	];
}

void FInterchangeBaseNodeDetailsCustomization::CreateInvalidHandleRow(IDetailCategoryBuilder& AttributeCategory, UE::Interchange::FAttributeKey& AttributeKey) const
{
	const FString InvalidAttributeHandle = TEXT("Invalid Attribute Handle!");
	const FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
	AttributeCategory.AddCustomRow(AttributeName)
	.NameContent()
	[
		CreateNameWidget(AttributeKey)
	]
	.ValueContent()
	[
		CreateSimpleNameWidget(InvalidAttributeHandle)
	];
}

TSharedRef<SWidget> FInterchangeBaseNodeDetailsCustomization::CreateNameWidget(UE::Interchange::FAttributeKey& AttributeKey) const
{
	const UE::Interchange::EAttributeTypes AttributeType = InterchangeBaseNode->GetAttributeType(AttributeKey);
	const FText AttributeName = FText::FromString(InterchangeBaseNode->GetKeyDisplayName(AttributeKey));
	const FString AttributeTooltipString = TEXT("Attribute Type: ") + UE::Interchange::AttributeTypeToString(AttributeType);
	const FText AttributeTooltip = FText::FromString(AttributeTooltipString);
	return SNew(STextBlock)
		.Text(AttributeName)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.ToolTipText(AttributeTooltip);
		
}

TSharedRef<SWidget> FInterchangeBaseNodeDetailsCustomization::CreateSimpleNameWidget(const FString& SimpleName) const
{
	const FText SimpleNameText = FText::FromString(SimpleName);
	return SNew(STextBlock)
		.Text(SimpleNameText)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

template<typename FunctorGet, typename FunctorSet>
TSharedRef<SWidget> FInterchangeBaseNodeDetailsCustomization::CreateVectorWidget(FunctorGet GetValue, FunctorSet SetValue, UE::Interchange::FAttributeKey& AttributeKey)
{
	auto GetComponentValue = [&GetValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->float
	{
		FVector Value = GetValue(BaseNode, Key);
		return Value[ComponentIndex];
	};

	auto SetComponentValue = [&GetValue, &SetValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, float ComponentValue, UE::Interchange::FAttributeKey& Key)
	{
		FVector Value = GetValue(BaseNode, Key);
		Value[ComponentIndex] = ComponentValue;
		SetValue(BaseNode, Key, Value);
    };
	
	//Create a horizontal layout with the 3 floats components
	return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
			MakeNumericWidget<float>(0, GetComponentValue, SetComponentValue, AttributeKey)
        ]
		+ SHorizontalBox::Slot()
	    .AutoWidth()
	    [
	        MakeNumericWidget<float>(1, GetComponentValue, SetComponentValue, AttributeKey)
	    ]
		+ SHorizontalBox::Slot()
	    .AutoWidth()
	    [
	        MakeNumericWidget<float>(2, GetComponentValue, SetComponentValue, AttributeKey)
	    ];
}

template<typename FunctorGet, typename FunctorSet>
TSharedRef<SWidget> FInterchangeBaseNodeDetailsCustomization::CreateQuaternionWidget(FunctorGet GetValue, FunctorSet SetValue, UE::Interchange::FAttributeKey& AttributeKey)
{
	auto GetComponentValue = [&GetValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, UE::Interchange::FAttributeKey& Key)->float
	{
		FQuat Value = GetValue(BaseNode, Key);
		if(ComponentIndex == 0)
		{
			return Value.X;
		}
		else if(ComponentIndex == 1)
		{
			return Value.Y;
		}
		else if(ComponentIndex == 2)
		{
			return Value.Z;
		}
		else if(ComponentIndex == 3)
		{
			return Value.W;
		}

		//Ensure
		ensure(ComponentIndex >= 0 && ComponentIndex < 4);
		return 0.0f;	
	};

	auto SetComponentValue = [&GetValue, &SetValue](int32 ComponentIndex, UInterchangeBaseNode* BaseNode, float ComponentValue, UE::Interchange::FAttributeKey& Key)
	{
		FQuat Value = GetValue(BaseNode, Key);
		if(ComponentIndex == 0)
		{
			Value.X = ComponentValue;
		}
		else if(ComponentIndex == 1)
		{
			Value.Y = ComponentValue;
		}
		else if(ComponentIndex == 2)
		{
			Value.Z = ComponentValue;
		}
		else if(ComponentIndex == 3)
		{
			Value.W = ComponentValue;
		}
		//Ensure
		if(ensure(ComponentIndex >= 0 && ComponentIndex < 4))
		{
			SetValue(BaseNode, Key, Value);
		}
	};
	
	//Create a horizontal layout with the 4 floats components
	return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            MakeNumericWidget<float>(0, GetComponentValue, SetComponentValue, AttributeKey)
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            MakeNumericWidget<float>(1, GetComponentValue, SetComponentValue, AttributeKey)
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            MakeNumericWidget<float>(2, GetComponentValue, SetComponentValue, AttributeKey)
        ]
		+ SHorizontalBox::Slot()
	    .AutoWidth()
	    [
	        MakeNumericWidget<float>(3, GetComponentValue, SetComponentValue, AttributeKey)
	    ];
}

template<typename NumericType, typename FunctorGet, typename FunctorSet>
TSharedRef<SWidget> FInterchangeBaseNodeDetailsCustomization::MakeNumericWidget(int32 ComponentIndex, FunctorGet GetValue, FunctorSet SetValue, UE::Interchange::FAttributeKey& AttributeKey)
{
	//The 3 Lambda functions need to call other lambda function having AttributeKey reference (&AttributeKey).
	//The per value capture prevent us the AttributeKey by reference 
	//We copy the key only one time here and pass a reference of this copy

	auto SetValueCommittedLambda = [this, &SetValue, ComponentIndex, AttributeKey](const NumericType Value, ETextCommit::Type CommitType)
	{
		UE::Interchange::FAttributeKey Key = AttributeKey;
		SetValue(ComponentIndex, InterchangeBaseNode, Value, Key);
	};
	auto SetValueChangedLambda = [this, &SetValue, ComponentIndex, AttributeKey](const NumericType Value)
	{
		UE::Interchange::FAttributeKey Key = AttributeKey;
		SetValue(ComponentIndex, InterchangeBaseNode, Value, Key);
	};
	
	return
		SNew(SNumericEntryBox<NumericType>)
			.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
			.Value_Lambda([this, &GetValue, ComponentIndex, AttributeKey]()
			{
				UE::Interchange::FAttributeKey Key = AttributeKey;
				return GetValue(ComponentIndex, InterchangeBaseNode, Key);
			})
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.OnValueCommitted_Lambda(SetValueCommittedLambda)
			.OnValueChanged_Lambda(SetValueChangedLambda)
			.AllowSpin(false);
}

#undef LOCTEXT_NAMESPACE
