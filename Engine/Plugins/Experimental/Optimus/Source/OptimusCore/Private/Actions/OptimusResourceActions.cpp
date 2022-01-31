// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusResourceActions.h"

#include "OptimusDeformer.h"
#include "OptimusHelpers.h"
#include "OptimusResourceDescription.h"

#include "Serialization/ObjectWriter.h"


FOptimusResourceAction_AddResource::FOptimusResourceAction_AddResource(
    UOptimusDeformer* InDeformer,
	FOptimusDataTypeRef InDataType,
    FName InName
	)
{
	if (ensure(InDeformer))
	{
		// FIXME: Validate name?
		ResourceName = Optimus::GetUniqueNameForScopeAndClass(InDeformer, UOptimusResourceDescription::StaticClass(), InName);
		DataType = InDataType;

		SetTitlef(TEXT("Add resource '%s'"), *ResourceName.ToString());
	}
}


UOptimusResourceDescription* FOptimusResourceAction_AddResource::GetResource(
	IOptimusPathResolver* InRoot
	) const
{
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);

	return Deformer->ResolveResource(ResourceName);
}


bool FOptimusResourceAction_AddResource::Do(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(InRoot);

	UOptimusResourceDescription* Resource = Deformer->CreateResourceDirect(ResourceName);
	if (!Resource)
	{
		return false;
	}

	// The name should not have changed.
	check(Resource->GetFName() == ResourceName);


	Resource->ResourceName = Resource->GetFName();
	Resource->DataType = DataType;

	if (!Deformer->AddResourceDirect(Resource))
	{
		Resource->Rename(nullptr, GetTransientPackage());
		return false;
	}
	
	ResourceName = Resource->GetFName();
	return true;
}


bool FOptimusResourceAction_AddResource::Undo(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusResourceDescription* Resource = GetResource(InRoot);
	if (!Resource)
	{
		return false;
	}

	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);
	return Deformer->RemoveResourceDirect(Resource);
}


FOptimusResourceAction_RemoveResource::FOptimusResourceAction_RemoveResource(
	UOptimusResourceDescription* InResource
	)
{
	if (ensure(InResource))
	{
		ResourceName = InResource->GetFName();
		DataType = InResource->DataType;

		SetTitlef(TEXT("Remove resource '%s'"), *InResource->GetName());
	}
}


bool FOptimusResourceAction_RemoveResource::Do(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);
	
	UOptimusResourceDescription* Resource = Deformer->ResolveResource(ResourceName);
	if (!Resource)
	{
		return false;
	}

	{
		Optimus::FBinaryObjectWriter ResourceArchive(Resource, ResourceData);
	}

	return Deformer->RemoveResourceDirect(Resource);
}


bool FOptimusResourceAction_RemoveResource::Undo(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);
	
	UOptimusResourceDescription* Resource = Deformer->CreateResourceDirect(ResourceName);
	if (!Resource)
	{
		return false;
	}

	// The names should match since the name should have remained unique.
	check(Resource->GetFName() == ResourceName);

	// Fill in the stored data
	{
		Optimus::FBinaryObjectReader ResourceArchive(Resource, ResourceData);
	}

	if (!Deformer->AddResourceDirect(Resource))
	{
		Resource->Rename(nullptr, GetTransientPackage());
		return false;
	}
	
	return true;
}


FOptimusResourceAction_RenameResource::FOptimusResourceAction_RenameResource(
	UOptimusResourceDescription* InResource, 
	FName InNewName
	)
{
	if (ensure(InResource))
	{
		UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(InResource->GetOuter());

		OldName = InResource->GetFName();
		NewName = Optimus::GetUniqueNameForScopeAndClass(Deformer, UOptimusResourceDescription::StaticClass(), InNewName);

		SetTitlef(TEXT("Rename resource to '%s'"), *NewName.ToString());
	}
}


bool FOptimusResourceAction_RenameResource::Do(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);
	UOptimusResourceDescription* Resource = Deformer->ResolveResource(OldName);
	
	return Resource && Deformer->RenameResourceDirect(Resource, NewName);
}


bool FOptimusResourceAction_RenameResource::Undo(
	IOptimusPathResolver* InRoot
	)
{
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(InRoot);
	UOptimusResourceDescription* Resource = Deformer->ResolveResource(NewName);

	return Resource && Deformer->RenameResourceDirect(Resource, OldName);
}
