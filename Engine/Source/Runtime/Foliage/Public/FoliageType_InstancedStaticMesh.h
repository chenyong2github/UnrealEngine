// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "FoliageType.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "FoliageType_InstancedStaticMesh.generated.h"

UCLASS(hidecategories=Object, editinlinenew, MinimalAPI)
class UFoliageType_InstancedStaticMesh : public UFoliageType
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=Mesh, meta=(DisplayThumbnail="true"))
	UStaticMesh* Mesh;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Mesh, Meta = (ToolTip = "Material overrides for foliage instances."))
	TArray<class UMaterialInterface*> OverrideMaterials;
		
	/** The component class to use for foliage instances. 
	  * You can make a Blueprint subclass of FoliageInstancedStaticMeshComponent to implement custom behavior and assign that class here. */
	UPROPERTY(EditAnywhere, Category = Mesh)
	TSubclassOf<UFoliageInstancedStaticMeshComponent> ComponentClass;
		
	UStaticMesh* GetStaticMesh() const
	{
		return Mesh;
	}

	UClass* GetComponentClass() const
	{
		return *ComponentClass;
	}

	virtual UObject* GetSource() const override;

#if WITH_EDITOR
	virtual void UpdateBounds() override;
	virtual bool IsSourcePropertyChange(const FProperty* Property) const override
	{
		return Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFoliageType_InstancedStaticMesh, Mesh);
	}
	virtual void SetSource(UObject* InSource) override;

	void SetStaticMesh(UStaticMesh* InStaticMesh)
	{
		Mesh = InStaticMesh;
		UpdateBounds();
	}
#endif
};
