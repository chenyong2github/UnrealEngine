// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterChromakeyCardActor.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

#if WITH_EDITOR
#include "Layers/LayersSubsystem.h"
#endif

ADisplayClusterChromakeyCardActor::ADisplayClusterChromakeyCardActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ADisplayClusterChromakeyCardActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if WITH_EDITOR
	UpdateChromakeySettings();
#endif
}

void ADisplayClusterChromakeyCardActor::AddToChromakeyLayer(ADisplayClusterRootActor* InRootActor)
{
	check(InRootActor);
	SetRootActorOwner(InRootActor);
	
	TArray<UDisplayClusterICVFXCameraComponent*> ICVFXComponents;
	InRootActor->GetComponents(ICVFXComponents);

	if (ICVFXComponents.Num() > 0)
	{
		static const FString LayerPrefix = TEXT("Chromakey");
			
		FName ChromakeyLayerName = NAME_None;

		// Find an existing layer name.
		for (UDisplayClusterICVFXCameraComponent* ICVFXComponent : ICVFXComponents)
		{
			FDisplayClusterConfigurationICVFX_VisibilityList& VisibilityList = ICVFXComponent->CameraSettings.Chromakey.ChromakeyRenderTexture.ShowOnlyList;
			
			if (const FActorLayer* ExistingLayer = VisibilityList.ActorLayers.FindByPredicate([](const FActorLayer& Layer)
			{
				return Layer.Name.ToString().StartsWith(LayerPrefix);
			}))
			{
				ChromakeyLayerName = ExistingLayer->Name;
				break;
			}
		}

		if (ChromakeyLayerName == NAME_None)
		{
			// Find a unique name so it's Chromakey_XXXX
			do
			{
				const FGuid LayerGuid = FGuid::NewGuid();
				const FString GuidStr = LayerGuid.ToString().Left(4);
				ChromakeyLayerName = *FString::Printf(TEXT("%s_%s"), *LayerPrefix, *GuidStr);
			}
			while (FindObject<UObject>(GetWorld(), *ChromakeyLayerName.ToString()) != nullptr);
		}

		check(!ChromakeyLayerName.IsNone());
		bool bLayerAdded = false;
#if WITH_EDITOR
		ULayersSubsystem* LayersSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULayersSubsystem>() : nullptr;
		if (LayersSubsystem)
		{
			LayersSubsystem->AddActorsToLayer(TArray<AActor*>{ this }, ChromakeyLayerName);
			bLayerAdded = true;
		}
#endif
		if (!bLayerAdded)
		{
			// Likely -game and we need to add the layer directly
			Layers.AddUnique(ChromakeyLayerName);
		}

		// Make sure all ICVFX components on this root actor have this chromakey layer
		for (UDisplayClusterICVFXCameraComponent* ICVFXComponent : ICVFXComponents)
		{
			FDisplayClusterConfigurationICVFX_VisibilityList& VisibilityList = ICVFXComponent->CameraSettings.Chromakey.ChromakeyRenderTexture.ShowOnlyList;

			if (!VisibilityList.ActorLayers.FindByPredicate([ChromakeyLayerName](const FActorLayer& Layer)
			{
				return Layer.Name == ChromakeyLayerName;
			}))
			{
#if WITH_EDITOR
				ICVFXComponent->Modify();
#endif
				VisibilityList.ActorLayers.AddDefaulted_GetRef().Name = ChromakeyLayerName;
			}
		}
	}
}

bool ADisplayClusterChromakeyCardActor::IsReferencedByICVFXCamera(UDisplayClusterICVFXCameraComponent* InCamera) const
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCamera->GetCameraSettingsICVFX();
	if (CameraSettings.Chromakey.ChromakeyRenderTexture.ShowOnlyList.Actors.Contains(this))
	{
		return true;
	}

	for (const FName& ThisLayer : Layers)
	{
		if (const FActorLayer* ExistingLayer = CameraSettings.Chromakey.ChromakeyRenderTexture.ShowOnlyList.ActorLayers.FindByPredicate([ThisLayer](const FActorLayer& Layer)
		{
			return Layer.Name == ThisLayer;
		}))
		{
			return true;
		}
	}

	return false;
}

void ADisplayClusterChromakeyCardActor::UpdateChromakeySettings()
{
	if (RootActorOwner.IsValid())
	{
		int32 Count = 0;
		FLinearColor ChromaColor(ForceInitToZero);
		TArray<UDisplayClusterICVFXCameraComponent*> ICVFXComponents;
		RootActorOwner->GetComponents(ICVFXComponents);

		for (UDisplayClusterICVFXCameraComponent* Component : ICVFXComponents)
		{
			if (IsReferencedByICVFXCamera(Component))
			{
				ChromaColor += Component->GetCameraSettingsICVFX().Chromakey.ChromakeyColor;
				Count++;
			}
		}

		Color = Count > 0 ? ChromaColor / Count : FLinearColor::Green;
	}
}
