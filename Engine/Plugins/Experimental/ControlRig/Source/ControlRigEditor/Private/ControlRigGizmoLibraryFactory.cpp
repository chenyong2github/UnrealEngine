// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigGizmoLibraryFactory.h"
#include "AssetTypeCategories.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "ControlRigGizmoLibraryFactory"

UControlRigGizmoLibraryFactory::UControlRigGizmoLibraryFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UControlRigGizmoLibrary::StaticClass();
}

UObject* UControlRigGizmoLibraryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UControlRigGizmoLibrary* GizmoLibrary = NewObject<UControlRigGizmoLibrary>(InParent, Name, Flags);

	GizmoLibrary->DefaultGizmo.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/ControlRig/Controls/ControlRig_Sphere_solid.ControlRig_Sphere_solid"));
	GizmoLibrary->DefaultMaterial = LoadObject<UMaterial>(nullptr, TEXT("/ControlRig/Controls/ControlRigGizmoMaterial.ControlRigGizmoMaterial"));
	GizmoLibrary->MaterialColorParameter = TEXT("Color");

	return GizmoLibrary;
}

FText UControlRigGizmoLibraryFactory::GetDisplayName() const
{
	return LOCTEXT("ControlRigGizmoLibraryFactoryName", "Control Rig Gizmo Library");
}

uint32 UControlRigGizmoLibraryFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

#undef LOCTEXT_NAMESPACE
