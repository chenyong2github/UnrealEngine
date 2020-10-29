// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingRendererComponent.h"

#include "IDMXPixelMappingRenderer.h"
#include "IDMXPixelMappingRendererModule.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingTypes.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"

#include "RHI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "UObject/ConstructorHelpers.h"


#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"

#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#endif

UDMXPixelMappingRendererComponent::UDMXPixelMappingRendererComponent()
{
#if WITH_EDITOR
	ConstructorHelpers::FObjectFinder<UTexture> DefaultTexture(TEXT("Texture2D'/Engine/VREditor/Devices/Vive/UE4_Logo.UE4_Logo'"), LOAD_NoWarn);
	// Hit breakpoint instead of preventing the editor to load if not found
	checkfSlow(DefaultTexture.Succeeded(), TEXT("Failed to load Texture2D'/Engine/VREditor/Devices/Vive/UE4_Logo.UE4_Logo'"));
	InputTexture = DefaultTexture.Object;
	RendererType = EDMXPixelMappingRendererType::Texture;
#endif
	
	SizeX = 100.f;
	SizeY = 100.f;

	Brightness = 1.0f;
}

UDMXPixelMappingRendererComponent::~UDMXPixelMappingRendererComponent()
{
#if WITH_EDITOR
	if (OnChangeLevelHandle.IsValid())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnMapChanged().Remove(OnChangeLevelHandle);
	}
#endif
}

const FName& UDMXPixelMappingRendererComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("Renderer");
	return NamePrefix;
}

void UDMXPixelMappingRendererComponent::PostLoad()
{
	Super::PostLoad();
	Initialize();
}

void UDMXPixelMappingRendererComponent::PostInitProperties()
{
	Super::PostInitProperties();

	const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = GetRenderer();
	if (Renderer.IsValid())
	{
		Renderer->SetBrightness(Brightness);
	}
}

#if WITH_EDITOR
void UDMXPixelMappingRendererComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, SizeX) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, SizeY))
	{
		// The target always needs be within GMaxTextureDimensions, larger dimensions are not supported by the engine
		const uint32 MaxTextureDimensions = GetMax2DTextureDimension();

		if (SizeX > MaxTextureDimensions ||
			SizeY > MaxTextureDimensions)
		{
			SizeX = FMath::Clamp(SizeX, 0.0f, static_cast<float>(MaxTextureDimensions));
			SizeY = FMath::Clamp(SizeY, 0.0f, static_cast<float>(MaxTextureDimensions));

			UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("Pixel mapping textures are limited to engine's max texture dimension %dx%d"), MaxTextureDimensions, MaxTextureDimensions);
		}

		ResizeMaterialRenderTarget(SizeX, SizeY);
	} 
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, InputWidget))
	{
		if (InputWidget && InputWidget->GetClass() != UserWidget->GetClass())
		{
			// UMG just tries to expand to the max possible size. Instead of using that we set a smaller, reasonable size here. 
			// This doesn't offer a solution to the adaptive nature of UMG, but implies to the user how to deal with the issue.
			constexpr float DefaultUMGSizeX = 1024.f;
			constexpr float DefaultUMGSizeY = 768.f;

			SetSize(FVector2D(DefaultUMGSizeX, DefaultUMGSizeY));
			ResizeOutputTarget(DefaultUMGSizeX, DefaultUMGSizeY);
		}

		UpdateInputWidget(InputWidget);
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, Brightness))
	{
		const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = GetRenderer();
		if (Renderer.IsValid())
		{
			Renderer->SetBrightness(Brightness);
		}
	}
}

void UDMXPixelMappingRendererComponent::RenderEditorPreviewTexture()
{
	UTextureRenderTarget2D* OutTexture = GetOutputTexture();

	const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = GetRenderer();
	{
		TArray<FDMXPixelMappingRendererPreviewInfo> GroupRender;
		ForEachChild([&GroupRender](UDMXPixelMappingBaseComponent* InComponent) {
			if (UDMXPixelMappingOutputDMXComponent* Component = Cast<UDMXPixelMappingOutputDMXComponent>(InComponent))
			{
				FDMXPixelMappingRendererPreviewInfo Config;
				if (UTextureRenderTarget2D* OutTeture = Component->GetOutputTexture())
				{
					Config.TextureResource = OutTeture->Resource;
				}
				Config.TextureSize = Component->GetSize();
				Config.TexturePosition = Component->GetPosition();

				GroupRender.Add(Config);
			}
		}, true);

		Renderer->RenderPreview_GameThread(OutTexture->Resource, GroupRender);
	}
}

UTextureRenderTarget2D* UDMXPixelMappingRendererComponent::GetOutputTexture()
{
	if (OutputTarget == nullptr)
	{
		const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("OutputTarget"));
		OutputTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
		OutputTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		OutputTarget->InitCustomFormat(10, 10, EPixelFormat::PF_B8G8R8A8, false);
	}

	return OutputTarget;
}

FVector2D UDMXPixelMappingRendererComponent::GetSize() const
{
	// Get a size from Input Texture
	if (const UTexture* const RendererInputTexture = GetRendererInputTexture())
	{
		if (class FTextureResource* Resource = RendererInputTexture->Resource)
		{
			return FVector2D(Resource->GetSizeX(), Resource->GetSizeY());
		}
	}

	return ComponentsCanvas->GetDesiredSize();
}

TSharedRef<SWidget> UDMXPixelMappingRendererComponent::TakeWidget()
{
	if (!ComponentsCanvas.IsValid())
	{
		ComponentsCanvas =
			SNew(SConstraintCanvas);
	}

	ComponentsCanvas->ClearChildren();

	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			// Build all child DMX pixel mapping slots
			Component->BuildSlot(ComponentsCanvas.ToSharedRef());
		}
	}, true);

	return ComponentsCanvas.ToSharedRef();
}

void UDMXPixelMappingRendererComponent::ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY)
{
	UTextureRenderTarget2D* Target = GetOutputTexture();

	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{
		check(Target);
		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
}

void UDMXPixelMappingRendererComponent::OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType)
{
	if (UserWidget != nullptr)
	{
		UserWidget->MarkPendingKill();
		UserWidget = nullptr;
	}
}

#endif // WITH_EDITOR

void UDMXPixelMappingRendererComponent::ResetDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent)
	{
		if (UDMXPixelMappingOutputComponent * Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->ResetDMX();
		}
	}, false);
}

void UDMXPixelMappingRendererComponent::SendDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->SendDMX();
		}
	}, false);
}

void UDMXPixelMappingRendererComponent::Render()
{
	RendererInputTexture();

	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->Render();
		}
	}, false);
}

void UDMXPixelMappingRendererComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

UWorld* UDMXPixelMappingRendererComponent::GetWorld() const
{
	UWorld* World = nullptr;

	if (GIsEditor)
	{
#if WITH_EDITOR
		World = GEditor->GetEditorWorldContext().World();
#endif
	}
	else
	{
		World = GWorld;
	}

	return World;
}

void UDMXPixelMappingRendererComponent::ResizeMaterialRenderTarget(uint32 InSizeX, uint32 InSizeY)
{
	check(InputRenderTarget);

	if (InSizeX > 0 && InSizeY > 0)
	{
		if (InputRenderTarget->SizeX != InSizeY ||
			InputRenderTarget->SizeY != InSizeX)
		{
			InputRenderTarget->ResizeTarget(InSizeX, InSizeY);
			InputRenderTarget->UpdateResourceImmediate();
		}
	}
}

void UDMXPixelMappingRendererComponent::UpdateInputWidget(TSubclassOf<UUserWidget> InInputWidget)
{
	if (InInputWidget != nullptr && UserWidget != nullptr)
	{
		UserWidget->MarkPendingKill();
		UserWidget = nullptr;
	}
	else
	{
		UserWidget = CreateWidget(GetWorld(), InInputWidget);
	}
}

void UDMXPixelMappingRendererComponent::Initialize()
{
	if (InputRenderTarget == nullptr)
	{
		const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("InputRenderTarget"));
		InputRenderTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
		InputRenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		InputRenderTarget->InitCustomFormat(SizeX, SizeY, EPixelFormat::PF_B8G8R8A8, false);
	}
	

#if WITH_EDITOR
	if (!OnChangeLevelHandle.IsValid())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnMapChanged().AddUObject(this, &UDMXPixelMappingRendererComponent::OnMapChanged);
	}
#endif

	if (UserWidget == nullptr && InputWidget != nullptr)
	{
		UserWidget = CreateWidget(GetWorld(), InputWidget);
	}

	if (!PixelMappingRenderer.IsValid())
	{
		PixelMappingRenderer = IDMXPixelMappingRendererModule::Get().CreateRenderer();
	}
}

void UDMXPixelMappingRendererComponent::RendererInputTexture()
{
	Initialize();

	switch (RendererType)
	{
	case(EDMXPixelMappingRendererType::Texture) :
		// Nothing
#if WITH_EDITOR
		if (InputTexture != nullptr &&  InputTexture->Resource != nullptr)
		{
			ResizeOutputTarget(InputTexture->Resource->GetSizeX(), InputTexture->Resource->GetSizeY());
		}
#endif
		break;
	case(EDMXPixelMappingRendererType::Material):
		PixelMappingRenderer->RenderMaterial(InputRenderTarget, InputMaterial);
#if WITH_EDITOR
		ResizeOutputTarget(SizeX, SizeY);
#endif
		break;
	case(EDMXPixelMappingRendererType::UMG):
		PixelMappingRenderer->RenderWidget(InputRenderTarget, UserWidget);
#if WITH_EDITOR
		ResizeOutputTarget(SizeX, SizeY);
#endif
		break;
	}
}

UTexture* UDMXPixelMappingRendererComponent::GetRendererInputTexture() const
{
	switch (RendererType)
	{
	case(EDMXPixelMappingRendererType::Texture):
		return InputTexture;
	case(EDMXPixelMappingRendererType::Material):
		if (InputMaterial != nullptr)
		{
			return InputRenderTarget;
		}
		break;

	case(EDMXPixelMappingRendererType::UMG):
		if (InputWidget != nullptr)
		{
			return InputRenderTarget;
		}
		break;
	}

	return nullptr;
}

bool UDMXPixelMappingRendererComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRootComponent>();
}
