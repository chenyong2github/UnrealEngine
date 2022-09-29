// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingDataModel.h"

#include "DisplayClusterRootActor.h"

#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

TMap<FName, FGetColorGradingDataModelGenerator> FDisplayClusterColorGradingDataModel::RegisteredDataModelGenerators;

FDisplayClusterColorGradingDataModel::FDisplayClusterColorGradingDataModel()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FPropertyRowGeneratorArgs Args;
	PropertyRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(Args);
	PropertyRowGenerator->OnRowsRefreshed().AddRaw(this, &FDisplayClusterColorGradingDataModel::OnPropertyRowGeneratorRefreshed);
}

TArray<TWeakObjectPtr<UObject>> FDisplayClusterColorGradingDataModel::GetObjects() const
{
	if (PropertyRowGenerator.IsValid())
	{
		return PropertyRowGenerator->GetSelectedObjects();
	}

	return TArray<TWeakObjectPtr<UObject>>();
}

void FDisplayClusterColorGradingDataModel::SetObjects(const TArray<UObject*>& InObjects)
{
	// Only update the data model if the objects being set are new
	bool bUpdateDataModel = false;
	if (PropertyRowGenerator.IsValid())
	{
		const TArray<TWeakObjectPtr<UObject>>& CurrentObjects = PropertyRowGenerator->GetSelectedObjects();

		if (CurrentObjects.Num() != InObjects.Num())
		{
			bUpdateDataModel = true;
		}
		else
		{
			for (UObject* NewObject : InObjects)
			{
				if (!CurrentObjects.Contains(NewObject))
				{
					bUpdateDataModel = true;
					break;
				}
			}
		}
	}

	if (bUpdateDataModel)
	{
		Reset();

		if (PropertyRowGenerator.IsValid())
		{
			PropertyRowGenerator->SetObjects(InObjects);
		}

		SelectedColorGradingGroupIndex = ColorGradingGroups.Num() ? 0 : INDEX_NONE;
		SelectedColorGradingElementIndex = 0;
	}
}

bool FDisplayClusterColorGradingDataModel::HasObjectOfType(const UClass* InClass) const
{
	if (PropertyRowGenerator.IsValid())
	{
		const TArray<TWeakObjectPtr<UObject>> SelectedObjects = PropertyRowGenerator->GetSelectedObjects();

		for (const TWeakObjectPtr<UObject>& Object : SelectedObjects)
		{
			if (Object.IsValid() && Object->GetClass()->IsChildOf(InClass))
			{
				return true;
			}
		}
	}

	return false;
}

void FDisplayClusterColorGradingDataModel::Reset()
{
	DataModelGeneratorInstances.Empty();
	ColorGradingGroups.Empty();
	DetailsSections.Empty();
	SelectedColorGradingGroupIndex = INDEX_NONE;
	SelectedColorGradingElementIndex = INDEX_NONE;
	ColorGradingGroupToolBarWidget = nullptr;
	bShowColorGradingGroupToolBar = false;

	if (PropertyRowGenerator.IsValid())
	{
		PropertyRowGenerator->SetObjects(TArray<UObject*>());
	}
}

FDisplayClusterColorGradingDataModel::FColorGradingGroup* FDisplayClusterColorGradingDataModel::GetSelectedColorGradingGroup()
{
	if (SelectedColorGradingGroupIndex > INDEX_NONE && SelectedColorGradingGroupIndex < ColorGradingGroups.Num())
	{
		return &ColorGradingGroups[SelectedColorGradingGroupIndex];
	}

	return nullptr;
}

void FDisplayClusterColorGradingDataModel::SetSelectedColorGradingGroup(int32 InColorGradingGroupIndex)
{
	SelectedColorGradingGroupIndex = InColorGradingGroupIndex <= ColorGradingGroups.Num() ? InColorGradingGroupIndex : INDEX_NONE;
	OnColorGradingGroupSelectionChangedDelegate.Broadcast();
}

FDisplayClusterColorGradingDataModel::FColorGradingElement* FDisplayClusterColorGradingDataModel::GetSelectedColorGradingElement()
{
	if (SelectedColorGradingGroupIndex > INDEX_NONE && SelectedColorGradingGroupIndex < ColorGradingGroups.Num())
	{
		FDisplayClusterColorGradingDataModel::FColorGradingGroup& SelectedGroup = ColorGradingGroups[SelectedColorGradingGroupIndex];
		if (SelectedColorGradingElementIndex > INDEX_NONE && SelectedColorGradingElementIndex < SelectedGroup.ColorGradingElements.Num())
		{
			return &SelectedGroup.ColorGradingElements[SelectedColorGradingElementIndex];
		}
	}

	return nullptr;
}

void FDisplayClusterColorGradingDataModel::SetSelectedColorGradingElement(int32 InColorGradingElementIndex)
{
	SelectedColorGradingElementIndex = InColorGradingElementIndex;
	OnColorGradingElementSelectionChangedDelegate.Broadcast();
}

TSharedPtr<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingDataModel::GetDataModelGenerator(const UClass* InClass)
{
	const UClass* CurrentClass = InClass;
	while (CurrentClass)
	{
		const FName ClassName = CurrentClass->GetFName();

		if (DataModelGeneratorInstances.Contains(ClassName))
		{
			return DataModelGeneratorInstances[ClassName];
		}
		else if (RegisteredDataModelGenerators.Contains(ClassName))
		{
			if (RegisteredDataModelGenerators[ClassName].IsBound())
			{
				TSharedRef<IDisplayClusterColorGradingDataModelGenerator> Generator = RegisteredDataModelGenerators[ClassName].Execute();
				DataModelGeneratorInstances.Add(ClassName, Generator);

				return Generator;
			}
		}

		CurrentClass = CurrentClass->GetSuperClass();
	}

	return nullptr;
}

void FDisplayClusterColorGradingDataModel::OnPropertyRowGeneratorRefreshed()
{
	ColorGradingGroups.Empty();
	DetailsSections.Empty();

	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyRowGenerator->GetSelectedObjects();

	if (SelectedObjects.Num() == 1)
	{
		TWeakObjectPtr<UObject> SelectedObject = SelectedObjects[0];

		if (SelectedObject.IsValid())
		{
			if (TSharedPtr<IDisplayClusterColorGradingDataModelGenerator> Generator = GetDataModelGenerator(SelectedObject->GetClass()))
			{
				Generator->GenerateDataModel(*PropertyRowGenerator, *this);
			}
		}
	}

	// TODO: Figure out what needs to be done to support multiple disparate types of objects being color graded at the same time

	OnDataModelGeneratedDelegate.Broadcast();
}

#undef LOCTEXT_NAMESPACE