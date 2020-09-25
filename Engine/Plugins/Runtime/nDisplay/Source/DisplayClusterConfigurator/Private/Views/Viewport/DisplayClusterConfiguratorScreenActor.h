// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/Viewport/DisplayClusterConfiguratorActor.h"
#include "DisplayClusterConfiguratorScreenActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceConstant;

UCLASS()
class ADisplayClusterConfiguratorScreenActor
	: public ADisplayClusterConfiguratorActor
{
	GENERATED_BODY()
public:
	ADisplayClusterConfiguratorScreenActor();

	//~ Begin ADisplayClusterConfiguratorActor Interface
	virtual void AddComponents() override;
	virtual void SetColor(const FColor& Color) override;
	virtual void SetNodeSelection(bool bSelect) override;
	//~ End ADisplayClusterConfiguratorActor Interface

protected:
	UStaticMeshComponent* RootMeshComponent;

	UMaterialInstanceConstant* MaterialInstance;

	UMaterial* TempMaterial;

	bool bSelected;
};
