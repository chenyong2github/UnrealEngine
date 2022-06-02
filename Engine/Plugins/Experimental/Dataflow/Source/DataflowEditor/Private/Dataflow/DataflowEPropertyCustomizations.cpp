// Copyright Epic Games, Inc. All Rights Reserved.
#include "Dataflow/DataflowEPropertyCustomizations.h"

#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowProperty.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Dataflow/DataflowEPropertyFactories.h"

#define LOCTEXT_NAMESPACE "DataflowEProprty_Customizations"

DEFINE_LOG_CATEGORY_STATIC(LogCDataflowEProprtyCustomizations, Log, All);

void UDataflowSEditorObject::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UE_LOG(LogCDataflowEProprtyCustomizations, Verbose, TEXT("Here"));
}

TSharedRef<IDetailCustomization> FDataflowSEditorCustomization::MakeInstance()
{
	return MakeShareable(new FDataflowSEditorCustomization);
}

void FDataflowSEditorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Dataflow");

	TArray< TWeakObjectPtr<UObject> > OutObjects;
	DetailBuilder.GetObjectsBeingCustomized(OutObjects);
	for (TWeakObjectPtr<UObject> Obj : OutObjects)
	{
		if (UDataflowSEditorObject* DataflowSObj = Cast< UDataflowSEditorObject>(Obj))
		{
			if (TSharedPtr<Dataflow::FNode> Node = DataflowSObj->Node)
			{
				if (UDataflow* Graph = DataflowSObj->Graph)
				{
					for (Dataflow::FProperty* Property : Node->GetProperties())
					{
						IDetailPropertyRow& PropertyRow = Category.AddProperty(FName("CustomProperty"));
						TSharedPtr<IPropertyHandle> PropertyHandle = PropertyRow.GetPropertyHandle();

						switch (Property->GetType())
						{
						case Dataflow::FProperty::EType::BOOL:
							Dataflow::PropertyWidgetFactory<bool>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<bool>*)Property);
							break;
						case Dataflow::FProperty::EType::INT:
							Dataflow::PropertyWidgetFactory<int>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<int>*)Property);
							break;
						case Dataflow::FProperty::EType::FLOAT:
							Dataflow::PropertyWidgetFactory<float>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<float>*)Property);
							break;
						case Dataflow::FProperty::EType::DOUBLE:
							Dataflow::PropertyWidgetFactory<double>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<double>*)Property);
							break;
						case Dataflow::FProperty::EType::STRING:
							Dataflow::PropertyWidgetFactory<FString>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<FString>*)Property);
							break;
						case Dataflow::FProperty::EType::NAME:
							Dataflow::PropertyWidgetFactory<FName>(DetailBuilder, PropertyHandle, Graph, Node, (Dataflow::TProperty<FName>*)Property);
							break;
						default:
							ensureMsgf(true, TEXT("Missing slate property convert."));
						}
						UE_LOG(LogCDataflowEProprtyCustomizations, Verbose, TEXT("FFloatObjectDetails::CustomizeDetails"));
					}
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
