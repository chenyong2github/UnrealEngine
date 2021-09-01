// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigLocalVariableDetails.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "SPinTypeSelector.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "LocalVariableDetails"

void LocalVariableDetails_GetCustomizedInfo(TSharedRef<IPropertyHandle> InStructPropertyHandle, UControlRigBlueprint*& OutBlueprint)
{
	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		if (Object->IsA<UControlRigBlueprint>())
		{
			OutBlueprint = Cast<UControlRigBlueprint>(Object);
			if (OutBlueprint)
			{
				break;
			}
		}
	}

	if (OutBlueprint == nullptr)
	{
		TArray<UPackage*> Packages;
		InStructPropertyHandle->GetOuterPackages(Packages);
		for (UPackage* Package : Packages)
		{
			if (Package == nullptr)
			{
				continue;
			}

			TArray<UObject*> SubObjects;
			Package->GetDefaultSubobjects(SubObjects);
			for (UObject* SubObject : SubObjects)
			{
				if (UControlRig* Rig = Cast<UControlRig>(SubObject))
				{
					UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Rig->GetClass()->ClassGeneratedBy);
					if (Blueprint)
					{
						if(Blueprint->GetOutermost() == Package)
						{
							OutBlueprint = Blueprint;
							break;
						}
					}
				}
			}

			if (OutBlueprint)
			{
				break;
			}
		}
	}
}

void FRigVMLocalVariableDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	ObjectsBeingCustomized.Reset();
	
	TArray<UObject*> Objects;
	InStructPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		UDetailsViewWrapperObject* WrapperObject = CastChecked<UDetailsViewWrapperObject>(Object);
		ObjectsBeingCustomized.Add(WrapperObject);
	}

	if(ObjectsBeingCustomized[0].IsValid())
	{
		VariableDescription = *ObjectsBeingCustomized[0]->GetContent<FRigVMGraphVariableDescription>();
		GraphBeingCustomized = ObjectsBeingCustomized[0]->GetTypedOuter<URigVMGraph>();
	}
}

void FRigVMLocalVariableDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructBuilder.GetParentCategory().GetParentLayout().HideCategory("RigVMGraphVariableDescription");
	IDetailCategoryBuilder& Category = StructBuilder.GetParentCategory().GetParentLayout().EditCategory("Local Variable");
		
	NameHandle = InStructPropertyHandle->GetChildHandle(TEXT("Name"));
	TypeHandle = InStructPropertyHandle->GetChildHandle(TEXT("CPPType"));
	TypeObjectHandle = InStructPropertyHandle->GetChildHandle(TEXT("CPPTypeObject"));
	DefaultValueHandle = InStructPropertyHandle->GetChildHandle(TEXT("DefaultValue"));
	
	const UEdGraphSchema* Schema = GetDefault<UControlRigGraphSchema>();

	const FSlateFontInfo DetailFontInfo = IDetailLayoutBuilder::GetDetailFont();
	Category.AddCustomRow( LOCTEXT("LocalVariableName", "Variable Name") )
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LocalVariableName", "Variable Name"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MaxDesiredWidth(250.0f)
	[
		NameHandle->CreatePropertyValueWidget()
	];

	TSharedPtr<IPinTypeSelectorFilter> CustomPinTypeFilter;
	Category.AddCustomRow(LOCTEXT("VariableTypeLabel", "Variable Type"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("VariableTypeLabel", "Variable Type"))
			.Font(DetailFontInfo)
		]
		.ValueContent()
		.MaxDesiredWidth(980.f)
		[
			SNew(SPinTypeSelector, FGetPinTypeTree::CreateUObject(GetDefault<UEdGraphSchema_K2>(), &UEdGraphSchema_K2::GetVariableTypeTree))
			.TargetPinType(this, &FRigVMLocalVariableDetails::OnGetPinInfo)
			.OnPinTypeChanged(this, &FRigVMLocalVariableDetails::HandlePinInfoChanged)
			.Schema(Schema)
			.TypeTreeFilter(ETypeTreeFilter::None)
			.Font(DetailFontInfo)
			.CustomFilter(CustomPinTypeFilter)
		];


	IDetailCategoryBuilder& DefaultValueCategory = StructBuilder.GetParentCategory().GetParentLayout().EditCategory(TEXT("DefaultValueCategory"), LOCTEXT("DefaultValueCategoryHeading", "Default Value"));

	if (!VariableDescription.CPPTypeObject)
	{
		if (VariableDescription.CPPType == "bool")
		{
			DefaultValueCategory.AddCustomRow(LOCTEXT("DefaultValue", "Default Value"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultValue", "Default Value"))
				.Font(DetailFontInfo)
			]
			.ValueContent()
			.MaxDesiredWidth(980.f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &FRigVMLocalVariableDetails::HandleBoolDefaultValueIsChecked)
				.OnCheckStateChanged(this, &FRigVMLocalVariableDetails::OnBoolDefaultValueChanged)
			];
		}
		else
		{
			DefaultValueCategory.AddCustomRow( LOCTEXT("LocalVariableDefaultValue", "Default Value") )
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LocalVariableDefaultValue", "Default Value"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MaxDesiredWidth(250.0f)
			[
				DefaultValueHandle->CreatePropertyValueWidget()
			];
		}
	}
	else
	{
		if(UEnum* Enum = Cast<UEnum>(VariableDescription.CPPTypeObject))
		{
			int32 CurrentValueIndex = 0;
			for (int32 i = 0; i < Enum->NumEnums()-1; i++)
			{
				if (!Enum->HasMetaData(TEXT("Hidden"), i))
				{
					FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
					EnumOptions.Add(MakeShareable(new FString(DisplayName)));
					if (DisplayName == VariableDescription.DefaultValue)
					{
						CurrentValueIndex = i;
					}					
				}
			}
			
			DefaultValueCategory.AddCustomRow(LOCTEXT("VariableReplicationConditionsLabel", "Replication Condition"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LocalVariableDefaultValue", "Default Value"))
				.Font(DetailFontInfo)
			]
			.ValueContent()
			[
				SNew(STextComboBox)
				.OptionsSource(&EnumOptions)
				.InitiallySelectedItem(EnumOptions[CurrentValueIndex])
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InValue, ESelectInfo::Type)
				{
					VariableDescription.DefaultValue = *InValue;
					DefaultValueHandle->SetValue(VariableDescription.DefaultValue);					
				})
			];			
		}
		else if(UScriptStruct* ScriptStruct = Cast<UScriptStruct>(VariableDescription.CPPTypeObject))
		{
			TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(ScriptStruct));
			IDetailPropertyRow* Row = DefaultValueCategory.AddExternalStructureProperty(StructOnScope, NAME_None);
			Row->GetPropertyHandle()->SetValueFromFormattedString(VariableDescription.DefaultValue);
			
			Row->GetPropertyHandle()->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda(
				[this, Row]()
				{
					VariableDescription.DefaultValue.Reset();
					Row->GetPropertyHandle()->GetValueAsFormattedString(VariableDescription.DefaultValue);
					DefaultValueHandle->SetValue(VariableDescription.DefaultValue);
				}
			));
		}
		else
		{
			checkNoEntry();
		}
	}
}

FEdGraphPinType FRigVMLocalVariableDetails::OnGetPinInfo() const
{
	if (!VariableDescription.Name.IsNone())
	{
		return VariableDescription.ToPinType();
	}
	return FEdGraphPinType();
}

void FRigVMLocalVariableDetails::HandlePinInfoChanged(const FEdGraphPinType& PinType)
{
	VariableDescription.ChangeType(PinType);
	TypeHandle->SetValue(VariableDescription.CPPType);
	TypeObjectHandle->SetValue(VariableDescription.CPPTypeObject);
}

ECheckBoxState FRigVMLocalVariableDetails::HandleBoolDefaultValueIsChecked() const
{
	return VariableDescription.DefaultValue == "1" ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FRigVMLocalVariableDetails::OnBoolDefaultValueChanged(ECheckBoxState InCheckBoxState)
{
	VariableDescription.DefaultValue = InCheckBoxState == ECheckBoxState::Checked ? "1" : "0";
	DefaultValueHandle->SetValue(VariableDescription.DefaultValue);
}

#undef LOCTEXT_NAMESPACE
