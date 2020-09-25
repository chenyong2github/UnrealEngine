// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorActor.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Materials/Material.h"


ADisplayClusterConfiguratorActor::ADisplayClusterConfiguratorActor()
{
}

void ADisplayClusterConfiguratorActor::Initialize(UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	ObjectToEdit = InObject;
	ToolkitPtr = InToolkit;
}

UMaterial* ADisplayClusterConfiguratorActor::CreateMateral(const FString& MateralPath)
{
	FSoftObjectPath MaterialSoftPath = FSoftObjectPath(MateralPath);
	if (UMaterial* Material = Cast< UMaterial >(MaterialSoftPath.TryLoad()))
	{
		Material->SetFlags(RF_Transient);
		Material->ClearFlags(RF_Transactional);

		return Material;
	}

	check(false);
	return nullptr;
}

void ADisplayClusterConfiguratorActor::OnSelection()
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Add(GetObject());

	ToolkitPtr.Pin()->SelectObjects(SelectedObjects);
}

bool ADisplayClusterConfiguratorActor::IsSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();

	UObject* const* SelectedObject = SelectedObjects.FindByPredicate([this](const UObject* InObject)
	{
		return InObject == GetObject();
	});

	if (SelectedObject != nullptr)
	{
		UObject* Obj = *SelectedObject;

		return Obj != nullptr;
	}

	return false;
}
