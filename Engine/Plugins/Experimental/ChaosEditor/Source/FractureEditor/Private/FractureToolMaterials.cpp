// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolMaterials.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"

#include "FractureEngineMaterials.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FractureToolMaterials)

#define LOCTEXT_NAMESPACE "FractureToolMaterials"

void UFractureMaterialsSettings::AddMaterialSlot()
{
	UFractureToolMaterials* MaterialsTool = Cast<UFractureToolMaterials>(OwnerTool.Get());
	MaterialsTool->AddMaterialSlot();
}

void UFractureMaterialsSettings::RemoveMaterialSlot()
{
	UFractureToolMaterials* MaterialsTool = Cast<UFractureToolMaterials>(OwnerTool.Get());
	MaterialsTool->RemoveMaterialSlot();
}

void UFractureToolMaterials::RemoveMaterialSlot()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	FScopedTransaction Transaction(LOCTEXT("RemoveMaterialSlot", "Remove Material from Geometry Collection(s)"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit Edit(GeometryCollectionComponent, GeometryCollection::EEditUpdate::Rest);
		UGeometryCollection* Collection = Edit.GetRestCollection();
		if (Collection->RemoveLastMaterialSlot())
		{
			GeometryCollectionComponent->MarkRenderDynamicDataDirty();
			GeometryCollectionComponent->MarkRenderStateDirty();
		}
	}
	UpdateActiveMaterialsList();
}

void UFractureToolMaterials::AddMaterialSlot()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	FScopedTransaction Transaction(LOCTEXT("AddMaterialSlot", "Add Material to Geometry Collection(s)"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit Edit(GeometryCollectionComponent, GeometryCollection::EEditUpdate::Rest);
		UGeometryCollection* Collection = Edit.GetRestCollection();

		int32 NewSlotIdx = Collection->AddNewMaterialSlot();
		if (NewSlotIdx > 0)
		{
			// copy an adjacent material into the new slot on the component as well
			GeometryCollectionComponent->SetMaterial(NewSlotIdx, GeometryCollectionComponent->GetMaterial(NewSlotIdx - 1));
		}

		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();
	}
	UpdateActiveMaterialsList();
}

UFractureToolMaterials::UFractureToolMaterials(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	MaterialsSettings = NewObject<UFractureMaterialsSettings>(GetTransientPackage(), UFractureMaterialsSettings::StaticClass());
	MaterialsSettings->OwnerTool = this;
}

bool UFractureToolMaterials::CanExecute() const
{
	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	return true;
}

FText UFractureToolMaterials::GetDisplayText() const
{
	return FText(LOCTEXT("FractureToolMaterials", "Edit geometry collection materials and default material assignments for new faces"));
}

FText UFractureToolMaterials::GetTooltipText() const
{
	return FText(LOCTEXT("FractureToolMaterialsTooltip", "Allows direct editing of materials on a geometry collection, as well as editing of the default handling."));
}

FSlateIcon UFractureToolMaterials::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.ToMesh");
}

void UFractureToolMaterials::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "Material", "Material", "Update geometry materials, especially for new internal faces resulting from fracture.", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->Materials = UICommandInfo;
}

TArray<UObject*> UFractureToolMaterials::GetSettingsObjects() const
{
	TArray<UObject*> Settings;
	Settings.Add(MaterialsSettings);
	return Settings;
}

void UFractureToolMaterials::FractureContextChanged()
{
}

void UFractureToolMaterials::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
}

void UFractureToolMaterials::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// update any cached data 
}

TArray<FFractureToolContext> UFractureToolMaterials::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	// A context is gathered for each selected GeometryCollection component
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FFractureToolContext FullSelection(GeometryCollectionComponent);
		FullSelection.ConvertSelectionToRigidNodes();
		Contexts.Add(FullSelection);
		// TODO: consider also visualizing which faces will be updated
	}

	return Contexts;
}


int32 UFractureToolMaterials::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();

		

		// Get ID to set and make sure it's valid
		int32 MatID = MaterialsSettings->GetAssignMaterialID();
		if (MatID == INDEX_NONE || MatID >= FractureContext.GetGeometryCollectionComponent()->GetNumMaterials())
		{
			return INDEX_NONE;
		}

		// convert enum to matching fracture engine materials enum
		FFractureEngineMaterials::ETargetFaces TargetFaces =
			(MaterialsSettings->ToFaces == EMaterialAssignmentTargets::AllFaces) ? FFractureEngineMaterials::ETargetFaces::AllFaces :
			(MaterialsSettings->ToFaces == EMaterialAssignmentTargets::OnlyInternalFaces) ? FFractureEngineMaterials::ETargetFaces::InternalFaces :
			FFractureEngineMaterials::ETargetFaces::ExternalFaces;

		if (MaterialsSettings->bOnlySelected)
		{
			FFractureEngineMaterials::SetMaterial(Collection, FractureContext.GetSelection(), TargetFaces, MatID);
		}
		else
		{
			FFractureEngineMaterials::SetMaterialOnAllGeometry(Collection, TargetFaces, MatID);
		}

		Collection.ReindexMaterials();
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE

