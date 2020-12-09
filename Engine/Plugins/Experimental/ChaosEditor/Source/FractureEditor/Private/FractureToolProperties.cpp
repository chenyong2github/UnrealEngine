// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolProperties.h"

#include "GeometryCollection/GeometryCollectionComponent.h"


#define LOCTEXT_NAMESPACE "FractureProperties"


UFractureToolSetInitialDynamicState::UFractureToolSetInitialDynamicState(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	StateSettings = NewObject<UFractureInitialDynamicStateSettings>(GetTransientPackage(), UFractureInitialDynamicStateSettings::StaticClass());
	StateSettings->OwnerTool = this;
}

FText UFractureToolSetInitialDynamicState::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSetInitialDynamicState", "State"));
}

FText UFractureToolSetInitialDynamicState::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolSetInitialDynamicStateToolTip", \
		"Override initial dynamic state for selected bones. If the component's Object Type is set to Dynamic, the solver will use this override state instead. Setting a bone to Kinematic will have the effect of anchoring the bone to world space, for instance."));
}

FText UFractureToolSetInitialDynamicState::GetApplyText() const
{
	return FText(NSLOCTEXT("Fracture", "ExecuteSetInitialDynamicState", "Set State"));
}

FSlateIcon UFractureToolSetInitialDynamicState::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.SetInitialDynamicState");
}

TArray<UObject*> UFractureToolSetInitialDynamicState::GetSettingsObjects() const
{
	TArray<UObject*> Settings;
	Settings.Add(StateSettings);
	return Settings;
}

void UFractureToolSetInitialDynamicState::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "SetInitialDynamicState", "State", \
		"Override initial dynamic state for selected bones. If the component's Object Type is set to Dynamic, the solver will use this override state instead. Setting a bone to Kinematic will have the effect of anchoring the bone to world space, for instance.", \
		EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->SetInitialDynamicState = UICommandInfo;
}

void UFractureToolSetInitialDynamicState::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			FGeometryCollectionEdit GCEdit = GeometryCollectionComponent->EditRestCollection();
			if (UGeometryCollection* GCObject = GCEdit.GetRestCollection())
			{
				TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GCObject->GetGeometryCollection();
				if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
				{
					TManagedArray<int32>& InitialDynamicState = GeometryCollection->GetAttribute<int32>("InitialDynamicState", FGeometryCollection::TransformGroup);

					TArray<int32> SelectedBones = GeometryCollectionComponent->GetSelectedBones();
					for (int32 Index : SelectedBones)
					{
						InitialDynamicState[Index] = static_cast<int32>(StateSettings->InitialDynamicState);
					}
				}
			}
		}
	}
}
			

#undef LOCTEXT_NAMESPACE
