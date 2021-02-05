// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolMeshCut.h"
#include "FractureEditorStyle.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "FractureEditorModeToolkit.h"
#include "FractureToolContext.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "FractureMesh"


UFractureToolMeshCut::UFractureToolMeshCut(const FObjectInitializer& ObjInit) 
	: Super(ObjInit) 
{
	MeshCutSettings = NewObject<UFractureMeshCutSettings>(GetTransientPackage(), UFractureMeshCutSettings::StaticClass());
	MeshCutSettings->OwnerTool = this;
}

FText UFractureToolMeshCut::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolMeshCut", "Mesh Cut Fracture")); 
}

FText UFractureToolMeshCut::GetTooltipText() const 
{
	return FText(NSLOCTEXT("Fracture", "FractureToolMeshCutTooltip", "Mesh fracture can be used to make cuts along a mesh in your Geometry Collection. You can apply noise to mesh cuts for more organic results.  Click the Fracture Button to commit the fracture to the geometry collection."));
}

FSlateIcon UFractureToolMeshCut::GetToolIcon() const 
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.Mesh");
}

void UFractureToolMeshCut::RegisterUICommand( FFractureEditorCommands* BindingContext ) 
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "Mesh", "Mesh", "Mesh Fracture", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->Mesh = UICommandInfo;
}

TArray<UObject*> UFractureToolMeshCut::GetSettingsObjects() const
 {
	TArray<UObject*> Settings;
	//Settings.Add(CutterSettings); // TODO: add cutter settings if/when we support noise and grout
	Settings.Add(CollisionSettings);
	Settings.Add(MeshCutSettings);
	return Settings;
}

bool UFractureToolMeshCut::IsCuttingActorValid()
{
	const UFractureMeshCutSettings* LocalCutSettings = MeshCutSettings;
	if (LocalCutSettings->CuttingActor == nullptr)
	{
		return false;
	}
	const UStaticMeshComponent* Component = LocalCutSettings->CuttingActor->GetStaticMeshComponent();
	if (Component == nullptr)
	{
		return false;
	}
	const UStaticMesh* Mesh = Component->GetStaticMesh();
	if (Mesh == nullptr)
	{
		return false;
	}
	if (Mesh->GetNumLODs() < 1)
	{
		return false;
	}
	return true;
}

int32 UFractureToolMeshCut::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		const UFractureMeshCutSettings* LocalCutSettings = MeshCutSettings;

		if (!IsCuttingActorValid())
		{
			return INDEX_NONE;
		}

		FMeshDescription* MeshDescription = LocalCutSettings->CuttingActor->GetStaticMeshComponent()->GetStaticMesh()->GetMeshDescription(0);
		FTransform Transform(LocalCutSettings->CuttingActor->GetTransform());
		FInternalSurfaceMaterials InternalSurfaceMaterials;
		// (Note: noise and grout not currently supported)
		return CutWithMesh(MeshDescription, Transform, InternalSurfaceMaterials, *FractureContext.GetGeometryCollection(), FractureContext.GetSelection(), CollisionSettings->PointSpacing, FractureContext.GetTransform());
	}

	return INDEX_NONE;
}


#undef LOCTEXT_NAMESPACE