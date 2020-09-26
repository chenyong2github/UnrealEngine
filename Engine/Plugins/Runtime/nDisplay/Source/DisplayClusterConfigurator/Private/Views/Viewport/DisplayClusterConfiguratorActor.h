// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorViewportItem.h"
#include "DisplayClusterConfiguratorActor.generated.h"

class FDisplayClusterConfiguratorToolkit;
class UMaterial;

UCLASS()
class ADisplayClusterConfiguratorActor
	: public AActor
	, public IDisplayClusterConfiguratorViewportItem
{
	GENERATED_BODY()
public:
	ADisplayClusterConfiguratorActor();

	virtual void AddComponents() {}
	virtual void SetColor(const FColor& Color) {}
	virtual void SetNodeSelection(bool bSelect) {}

	void Initialize(UObject* InObject, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	UMaterial* CreateMateral(const FString& MateralPath);

	//~ Begin IDisplayClusterConfiguratorItem Interface
	virtual void OnSelection() override;
	virtual UObject* GetObject() const override { return ObjectToEdit.Get(); }
	virtual bool IsSelected() override;
	//~ End IDisplayClusterConfiguratorItem Interface

protected:
	TWeakObjectPtr<UObject> ObjectToEdit;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};