// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorViewportBuilder.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfiguratorPreviewScene.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Views/Viewport/DisplayClusterConfiguratorScreenActor.h"
#include "Views/Viewport/DisplayClusterConfiguratorCameraActor.h"


FDisplayClusterConfiguratorViewportBuilder::FDisplayClusterConfiguratorViewportBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene)
	: ToolkitPtr(InToolkit)
	, PreviewScenePtr(InPreviewScene)
{}

void FDisplayClusterConfiguratorViewportBuilder::BuildViewport()
{
	ToolkitPtr.Pin()->RegisterOnObjectSelected(IDisplayClusterConfiguratorToolkit::FOnObjectSelectedDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewportBuilder::OnObjectSelected));
	ToolkitPtr.Pin()->RegisterOnClearViewportSelection(IDisplayClusterConfiguratorToolkit::FOnClearViewportSelectionDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewportBuilder::OnClearViewportSelection));
	
	TSharedPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScene = PreviewScenePtr.Pin();
	check(PreviewScene.IsValid());

	UWorld* PreviewSceneWorld = PreviewScene->GetWorld();

	// It should be moved to preview builder
	for (UActorComponent* Component : Components)
	{
		PreviewScene->RemoveComponent(Component);
	}
	Components.Empty();

	// TODO. fix destroy actors
	for (AActor* Actor : Actors)
	{
		Actor->Destroy();
		PreviewSceneWorld->DestroyActor(Actor);
	}
	Actors.Empty();

	UDisplayClusterConfigurationData* const Config = ToolkitPtr.Pin()->GetConfig();
	if (Config)
	{
		// Add cameras
		for (const auto& it : Config->Scene->Cameras)
		{
			ADisplayClusterConfiguratorCameraActor* ConfiguratorCameraActor = PreviewSceneWorld->SpawnActor<ADisplayClusterConfiguratorCameraActor>(ADisplayClusterConfiguratorCameraActor::StaticClass(), FTransform::Identity);
			ConfiguratorCameraActor->Initialize(it.Value, ToolkitPtr.Pin().ToSharedRef());
			ConfiguratorCameraActor->AddComponents();
			Actors.Add(ConfiguratorCameraActor);
		}

		// Add Screens, just for testing
		static FColor Colors[]
		{
			FColor::Red,
			FColor::Green,
			FColor::Blue,
			FColor::Yellow,
			FColor::Cyan,
			FColor::Magenta,
			FColor::Orange,
			FColor::Purple,
			FColor::Turquoise,
			FColor::Silver,
			FColor::Emerald,
		};

		uint32 Index = 0;
		for (const auto& it : Config->Scene->Screens)
		{
			FVector2D SizeNormilized = it.Value->Size;
			FVector Location = it.Value->Location;

			// Spawn actor
			ADisplayClusterConfiguratorScreenActor* ConfiguratorScreenActor = PreviewSceneWorld->SpawnActor<ADisplayClusterConfiguratorScreenActor>(ADisplayClusterConfiguratorScreenActor::StaticClass(), FTransform::Identity);
			ConfiguratorScreenActor->Initialize(it.Value, ToolkitPtr.Pin().ToSharedRef());
			ConfiguratorScreenActor->AddComponents();
			ConfiguratorScreenActor->SetColor(Colors[Index]);
			FTransform Transform(FRotator(0, 90, 90), FVector(Location.Z, Location.Y, Location.X), FVector(SizeNormilized.X, SizeNormilized.Y, 1.f));
			ConfiguratorScreenActor->SetActorTransform(Transform);
			Actors.Add(ConfiguratorScreenActor);

			Index++;// Unsafe, just for testing
		}
	}

	ToolkitPtr.Pin()->InvalidateViews();
}

void FDisplayClusterConfiguratorViewportBuilder::OnObjectSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();

	if (SelectedObjects.Num())
	{
		for (AActor* Actor : Actors)
		{
			if (ADisplayClusterConfiguratorActor* ConfiguratorActor = Cast<ADisplayClusterConfiguratorActor>(Actor))
			{
				if (ConfiguratorActor->IsSelected())
				{
					ConfiguratorActor->SetNodeSelection(true);
				}
				else
				{
					ConfiguratorActor->SetNodeSelection(false);
				}
			}
		}
	}

	ToolkitPtr.Pin()->InvalidateViews();
}

void FDisplayClusterConfiguratorViewportBuilder::OnClearViewportSelection()
{
	ClearViewportSelection();
}


void FDisplayClusterConfiguratorViewportBuilder::ClearViewportSelection()
{
	for (AActor* Actor : Actors)
	{
		if (ADisplayClusterConfiguratorActor* ConfiguratorActor = Cast<ADisplayClusterConfiguratorActor>(Actor))
		{
			ConfiguratorActor->SetNodeSelection(false);
		}
	}


	TArray<UObject*> SelectedObjects;
	ToolkitPtr.Pin()->SelectObjects(SelectedObjects);

	ToolkitPtr.Pin()->InvalidateViews();
}
