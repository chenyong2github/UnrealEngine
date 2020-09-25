// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/Viewport/DisplayClusterConfiguratorActor.h"
#include "DisplayClusterConfiguratorCameraActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceConstant;

UCLASS()
class ADisplayClusterConfiguratorCameraActor
	: public ADisplayClusterConfiguratorActor
{
	GENERATED_BODY()
public:

	ADisplayClusterConfiguratorCameraActor();

	//~ Begin ADisplayClusterConfiguratorActor Interface
	virtual void AddComponents() override;
	virtual void SetColor(const FColor& Color) override;
	virtual void SetNodeSelection(bool bSelect) override;
	//~ End ADisplayClusterConfiguratorActor Interface

protected:

	bool bSelected;

	UStaticMeshComponent* CineCamComponent;
};