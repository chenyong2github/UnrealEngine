// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingRendererComponent.h"

#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingScreenComponent.h"
#include "DMXPixelMappingTypes.h"
#include "IDMXPixelMappingRendererModule.h"
#include "Library/DMXEntityFixtureType.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"

#include "LevelEditor.h"
#endif

class FTextureResource;
const FIntPoint UDMXPixelMappingRendererComponent::MaxDownsampleBufferTargetSize = FIntPoint(4096);
const FLinearColor UDMXPixelMappingRendererComponent::ClearTextureColor = FLinearColor::Black;

UDMXPixelMappingRendererComponent::UDMXPixelMappingRendererComponent()
	: DownsampleBufferTarget(nullptr)
	, DownsamplePixelCount(0)
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
			ResizePreviewRenderTarget(DefaultUMGSizeX, DefaultUMGSizeY);
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
	if (DownsampleBufferTarget == nullptr || DownsamplePixelCount == 0)
	{
		return;
	}

	const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = GetRenderer();
	if (!ensure(Renderer))
	{
		return;
	}

	TArray<FDMXPixelMappingDownsamplePixelPreviewParam> PixelPreviewParams;
	PixelPreviewParams.Reserve(DownsamplePixelCount);
	
	ForEachChild([this, &PixelPreviewParams](UDMXPixelMappingBaseComponent* InComponent) {
		if(UDMXPixelMappingScreenComponent* ScreenComponent = Cast<UDMXPixelMappingScreenComponent>(InComponent))
		{
			const FVector2D SizePixel = ScreenComponent->GetScreenPixelSize();
			const int32 DownsampleIndexStart = ScreenComponent->GetPixelDownsamplePositionRange().Key;
			const int32 PositionX = ScreenComponent->PositionX;
			const int32 PositionY = ScreenComponent->PositionY;

			ScreenComponent->ForEachPixel([this, &PixelPreviewParams, SizePixel, PositionX, PositionY, DownsampleIndexStart](const int32 InXYIndex, const int32 XIndex, const int32 YIndex)
				{
					FDMXPixelMappingDownsamplePixelPreviewParam PixelPreviewParam;
					PixelPreviewParam.ScreenPixelSize = SizePixel;
					PixelPreviewParam.ScreenPixelPosition = FVector2D(PositionX + SizePixel.X * XIndex, PositionY + SizePixel.Y * YIndex);
					PixelPreviewParam.DownsamplePosition = GetPixelPosition(InXYIndex + DownsampleIndexStart);

					PixelPreviewParams.Add(MoveTemp(PixelPreviewParam));
				});
		}
		else if (UDMXPixelMappingOutputDMXComponent* Component = Cast<UDMXPixelMappingOutputDMXComponent>(InComponent))
		{
			FDMXPixelMappingDownsamplePixelPreviewParam PixelPreviewParam;
			PixelPreviewParam.ScreenPixelSize = Component->GetSize();
			PixelPreviewParam.ScreenPixelPosition = Component->GetPosition();
			PixelPreviewParam.DownsamplePosition = GetPixelPosition(Component->GetDownsamplePixelIndex());

			PixelPreviewParams.Add(MoveTemp(PixelPreviewParam));
		}
	}, true);

	Renderer->RenderPreview(GetPreviewRenderTarget()->GetResource(), DownsampleBufferTarget->GetResource(), MoveTemp(PixelPreviewParams));
}

UTextureRenderTarget2D* UDMXPixelMappingRendererComponent::GetPreviewRenderTarget()
{
	if (PreviewRenderTarget == nullptr)
	{
		PreviewRenderTarget = CreateRenderTarget(TEXT("DMXPreviewRenderTarget"));
	}

	return PreviewRenderTarget;
}

FVector2D UDMXPixelMappingRendererComponent::GetSize() const
{
	// Get a size from Input Texture
	if (const UTexture* const RendererInputTexture = GetRendererInputTexture())
	{
		if (const FTextureResource* Resource = RendererInputTexture->GetResource())
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

void UDMXPixelMappingRendererComponent::OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType)
{
	if (UserWidget != nullptr)
	{
		UserWidget->MarkPendingKill();
		UserWidget = nullptr;
	}
}

#endif // WITH_EDITOR

void UDMXPixelMappingRendererComponent::ResizePreviewRenderTarget(uint32 InSizeX, uint32 InSizeY)
{
#if WITH_EDITOR
	UTextureRenderTarget2D* Target = GetPreviewRenderTarget();

	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{
		check(Target);
		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
#endif
}

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
	// 1. Get downsample input texture
	UTexture* DownsampleInputTexture = GetRendererInputTexture();
	if (!DownsampleInputTexture)
	{
		return;
	}

	// 2. Render the input texture before downsample
	RendererInputTexture();

	// 3. Make sure there is the DownsampleBufferTarget exists and can size can hold all pixels
	CreateOrUpdateDownsampleBufferTarget();

	// 4. reserve enough space for pixels params
	DownsamplePixelParams.Reserve(DownsamplePixelCount);

	// 5. Loop through all child pixels in order to get pixels downsample params for rendering
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->QueueDownsample();
		}
	}, false);

	// 6. Make sure pixel count the same with pixel params set number
	if (!ensure(DownsamplePixelParams.Num() == DownsamplePixelCount))
	{
		DownsamplePixelParams.Empty();
		return;
	}

	// 7. Downsample all pixels
	GetRenderer()->DownsampleRender(
		DownsampleInputTexture->GetResource(),
		DownsampleBufferTarget->GetResource(),
		DownsampleBufferTarget->GameThread_GetRenderTargetResource(),
		MoveTemp(DownsamplePixelParams), // Move Set to GPU thread, no empty function call needed
		[this](TArray<FColor>&& InDownsampleBuffer, FIntRect InRect) { SetDownsampleBuffer(MoveTemp(InDownsampleBuffer), InRect); }
	);
}

void UDMXPixelMappingRendererComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}


FIntPoint UDMXPixelMappingRendererComponent::GetPixelPosition(int32 InPosition) const
{
	const int32 YRows = InPosition / MaxDownsampleBufferTargetSize.X;
	return FIntPoint(InPosition % MaxDownsampleBufferTargetSize.X, YRows);
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

void UDMXPixelMappingRendererComponent::ResizeMaterialRenderTarget(int32 InSizeX, int32 InSizeY)
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
		InputRenderTarget = CreateRenderTarget(TEXT("InputRenderTarget"));
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

UTextureRenderTarget2D* UDMXPixelMappingRendererComponent::CreateRenderTarget(const FName& InBaseName)
{
	const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), InBaseName);
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
	RenderTarget->ClearColor = ClearTextureColor;
	constexpr bool bInForceLinearGamma = false;
	RenderTarget->InitCustomFormat(SizeX, SizeY, EPixelFormat::PF_B8G8R8A8, bInForceLinearGamma);

	return  RenderTarget;
}

void UDMXPixelMappingRendererComponent::RendererInputTexture()
{
	Initialize();

	switch (RendererType)
	{
	case(EDMXPixelMappingRendererType::Texture) :
		// Nothing
		if (InputTexture != nullptr &&  InputTexture->GetResource() != nullptr)
		{
			ResizePreviewRenderTarget(InputTexture->GetResource()->GetSizeX(), InputTexture->GetResource()->GetSizeY());
		}
		break;
	case(EDMXPixelMappingRendererType::Material):
		PixelMappingRenderer->RenderMaterial(InputRenderTarget, InputMaterial);
		ResizePreviewRenderTarget(SizeX, SizeY);
		break;
	case(EDMXPixelMappingRendererType::UMG):
		PixelMappingRenderer->RenderWidget(InputRenderTarget, UserWidget);
		ResizePreviewRenderTarget(SizeX, SizeY);
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
	default:
		checkNoEntry();
	}

	return nullptr;
}

bool UDMXPixelMappingRendererComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRootComponent>();
}

int32 UDMXPixelMappingRendererComponent::GetTotalDownsamplePixelCount()
{
	// Reset pixel counter
	DownsamplePixelCount = 0;

	// Count all pixels
	constexpr bool bIsRecursive = true;
	ForEachComponentOfClass<UDMXPixelMappingOutputComponent>([&](UDMXPixelMappingOutputComponent* InComponent)
		{
			// If that is screen component
			if (UDMXPixelMappingScreenComponent* ScreenComponent = Cast<UDMXPixelMappingScreenComponent>(InComponent))
			{
				DownsamplePixelCount += (ScreenComponent->NumXCells * ScreenComponent->NumYCells);
			}
			// If that is single pixel component
			else if (Cast<UDMXPixelMappingOutputDMXComponent>(InComponent))
			{
				DownsamplePixelCount++;
			}
		}, bIsRecursive);

	return DownsamplePixelCount;
}

void UDMXPixelMappingRendererComponent::CreateOrUpdateDownsampleBufferTarget()
{
	// Create texture if it does not exists
	if (DownsampleBufferTarget == nullptr)
	{	
		DownsampleBufferTarget = CreateRenderTarget(TEXT("DMXPixelMappingDownsampleBufferTarget"));
	}

	const int32 PreviousDownsamplePixelCount = DownsamplePixelCount;
	const int32 TotalDownsamplePixelCount = GetTotalDownsamplePixelCount();

	if (TotalDownsamplePixelCount > 0 && 
		TotalDownsamplePixelCount != PreviousDownsamplePixelCount)
	{
		// Make sure total pixel count less then max texture size MaxDownsampleBufferTargetSize.X * MaxDownsampleBufferTargetSize.Y
		if (!ensure(TotalDownsamplePixelCount < (MaxDownsampleBufferTargetSize.X * MaxDownsampleBufferTargetSize.Y)))
		{
			return;
		}

		/**
		 * if total pixel count less then max size x texture high equal 1
		 * and texture widht dynamic from 1 up to MaxDownsampleBufferTargetSize.X
		 * |0,1,2,3,4,5,...,n|
		 */
		if (TotalDownsamplePixelCount <= MaxDownsampleBufferTargetSize.X)
		{
			constexpr uint32 TargetSizeY = 1;
			DownsampleBufferTarget->ResizeTarget(TotalDownsamplePixelCount, TargetSizeY);
		}
		/**
		* if total pixel count more then max size x. At this case it should resize X and Y for buffer texture target
		* |0,1,2,3,4,5,..., MaxDownsampleBufferTargetSize.X|
		* |0,1,2,3,4,5,..., MaxDownsampleBufferTargetSize.X|
		* |................................................|
		* |MaxDownsampleBufferTargetSize.Y.................|
		*/
		else
		{
			const uint32 TargetSizeY = ((TotalDownsamplePixelCount - 1) / MaxDownsampleBufferTargetSize.X) + 1;
			DownsampleBufferTarget->ResizeTarget(MaxDownsampleBufferTargetSize.X, TargetSizeY);
		}
	}
}

void UDMXPixelMappingRendererComponent::AddPixelToDownsampleSet(FDMXPixelMappingDownsamplePixelParam&& InDownsamplePixelParam)
{
	DownsamplePixelParams.Emplace(InDownsamplePixelParam);
}

bool UDMXPixelMappingRendererComponent::IsPixelRangeValid(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd) const
{
	if (InDownsamplePixelIndexEnd >= InDownsamplePixelIndexStart &&
		DownsampleBuffer.IsValidIndex(InDownsamplePixelIndexStart) &&
		DownsampleBuffer.IsValidIndex(InDownsamplePixelIndexEnd))
	{
		return true;
	}

	return false;
}

void UDMXPixelMappingRendererComponent::SetDownsampleBuffer(TArray<FColor>&& InDownsampleBuffer, FIntRect InRect)
{
	check(IsInRenderingThread());

	FScopeLock ScopeLock(&DownsampleBufferCS);
	DownsampleBuffer = MoveTemp(InDownsampleBuffer);
}

TOptional<FColor> UDMXPixelMappingRendererComponent::GetDownsampleBufferPixel(const int32 InDownsamplePixelIndex)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);
	TOptional<FColor> Color;

	if (DownsampleBuffer.IsValidIndex(InDownsamplePixelIndex))
	{
		Color = DownsampleBuffer[InDownsamplePixelIndex];
	}

	return Color;
}

TArray<FColor> UDMXPixelMappingRendererComponent::GetDownsampleBufferPixels(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);
	TArray<FColor> DownsamplePixelRange;
	
	// Could be out of the range when texture resizing on GPU thread
	if (!IsPixelRangeValid(InDownsamplePixelIndexStart, InDownsamplePixelIndexEnd))
	{
		return DownsamplePixelRange;
	}

	DownsamplePixelRange.Reserve(InDownsamplePixelIndexEnd - InDownsamplePixelIndexStart + 1);
	for (int32 PixelIndex = InDownsamplePixelIndexStart; PixelIndex <= InDownsamplePixelIndexEnd; ++PixelIndex)
	{
		DownsamplePixelRange.Add(DownsampleBuffer[PixelIndex]);
	}

	return DownsamplePixelRange;
}

bool UDMXPixelMappingRendererComponent::ResetColorDownsampleBufferPixel(const int32 InDownsamplePixelIndex)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	if (DownsampleBuffer.IsValidIndex(InDownsamplePixelIndex))
	{
		DownsampleBuffer[InDownsamplePixelIndex] = FColor::Black;
		return true;
	}

	return false;
}

bool UDMXPixelMappingRendererComponent::ResetColorDownsampleBufferPixels(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	// Could be out of the range when texture resizing on GPU thread
	if (!IsPixelRangeValid(InDownsamplePixelIndexStart, InDownsamplePixelIndexEnd))
	{
		return false;
	}

	for (int32 PixelIndex = InDownsamplePixelIndexStart; PixelIndex <= InDownsamplePixelIndexEnd; ++PixelIndex)
	{
		DownsampleBuffer[PixelIndex] = FColor::Black;
	}

	return true;
}

void UDMXPixelMappingRendererComponent::EmptyDownsampleBuffer()
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	DownsampleBuffer.Empty();
}
