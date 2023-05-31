// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingRendererComponent.h"

#include "Async/Async.h"
#include "Blueprint/UserWidget.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingScreenComponent.h"
#include "DMXPixelMappingPreprocessRenderer.h"
#include "DMXPixelMappingMainStreamObjectVersion.h"
#include "DMXPixelMappingTypes.h"
#include "DMXStats.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IDMXPixelMappingRenderer.h"
#include "IDMXPixelMappingRendererModule.h"
#include "Modulators/DMXModulator.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#endif


DECLARE_CYCLE_STAT(TEXT("PixelMapping Render"), STAT_DMXPixelMappingRender, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("PixelMapping SendDMX"), STAT_DMXPixelMappingSendDMX, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("PixelMapping RenderInputTexture"), STAT_RenderInputTexture, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("PixelMapping GetTotalDownsamplePixelCount"), STAT_GetTotalDownsamplePixelCount, STATGROUP_DMX);


const FIntPoint UDMXPixelMappingRendererComponent::MaxDownsampleBufferTargetSize = FIntPoint(4096);
const FLinearColor UDMXPixelMappingRendererComponent::ClearTextureColor = FLinearColor::Black;

UDMXPixelMappingRendererComponent::UDMXPixelMappingRendererComponent()
{
	SetSize(FVector2D(100.f, 100.f));
	
#if WITH_EDITOR
	ConstructorHelpers::FObjectFinder<UTexture> DefaultTexture(TEXT("Texture2D'/Engine/VREditor/Devices/Vive/UE4_Logo.UE4_Logo'"), LOAD_NoWarn);
	if (ensureAlwaysMsgf(DefaultTexture.Succeeded(), TEXT("Failed to load Texture2D'/Engine/VREditor/Devices/Vive/UE4_Logo.UE4_Logo'")))
	{
		InputTexture = DefaultTexture.Object;
		RendererType = EDMXPixelMappingRendererType::Texture;

		if (FTextureResource* Resource = InputTexture->GetResource())
		{
			SetSize(FVector2D(Resource->GetSizeX(), Resource->GetSizeY()));
		}
	}
#endif
	
	PreprocessRenderer = CreateDefaultSubobject<UDMXPixelMappingPreprocessRenderer>("PreprocessRenderer");
	Brightness = 1.0f;
}

const FName& UDMXPixelMappingRendererComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("Renderer");
	return NamePrefix;
}

bool UDMXPixelMappingRendererComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRootComponent>();
}

void UDMXPixelMappingRendererComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FDMXPixelMappingMainStreamObjectVersion::GUID);
	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FDMXPixelMappingMainStreamObjectVersion::GUID) < FDMXPixelMappingMainStreamObjectVersion::LockRendererComponentsThatUseTextureInDesigner)
		{
			if (RendererType == EDMXPixelMappingRendererType::Texture)
			{
				// Refresh the size of the texture if that is used as input
				if (InputTexture)
				{
					if (const FTextureResource* TextureResource = InputTexture->GetResource())
					{
						const FVector2D NewSize = FVector2D(TextureResource->GetSizeX(), TextureResource->GetSizeY());
						SetSize(NewSize);
					}
				}
			}
		}
	}
}

void UDMXPixelMappingRendererComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsTemplate())
	{
		Initialize();
	}
}

void UDMXPixelMappingRendererComponent::PostLoad()
{
	Super::PostLoad();
	
	if (!IsTemplate())
	{
		Initialize();
	}
}

#if WITH_EDITOR
void UDMXPixelMappingRendererComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	const FName PropertyName = PropertyChangedChainEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, Brightness))
	{
		const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = GetRenderer();
		if (Renderer.IsValid())
		{
			Renderer->SetBrightness(Brightness);
		}
	}

	if (PropertyChangedChainEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		// Apply interactive changes only once per frame
		static uint64 Frame = GFrameCounter;
		if (Frame != GFrameCounter)
		{
			Initialize();
			Frame = GFrameCounter;
		}
	}
	else
	{
		Initialize();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingRendererComponent::RenderEditorPreviewTexture()
{
	if (!DownsampleBufferTarget)
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
			const int32 PositionX = ScreenComponent->GetPosition().X;
			const int32 PositionY = ScreenComponent->GetPosition().Y;

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
#endif // WITH_EDITOR

#if WITH_EDITOR
UTextureRenderTarget2D* UDMXPixelMappingRendererComponent::GetPreviewRenderTarget()
{
	if (PreviewRenderTarget == nullptr)
	{
		PreviewRenderTarget = CreateRenderTarget(TEXT("DMXPreviewRenderTarget"));
	}

	return PreviewRenderTarget;
}
#endif // WITH_EDITOR

bool UDMXPixelMappingRendererComponent::GetPixelMappingComponentModulators(FDMXEntityFixturePatchRef FixturePatchRef, TArray<UDMXModulator*>& DMXModulators)
{
	for (const UDMXPixelMappingBaseComponent* Child : Children)
	{
		if (const UDMXPixelMappingFixtureGroupComponent* GroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Child))
		{
			for (const UDMXPixelMappingBaseComponent* ChildOfGroupComponent : GroupComponent->Children)
			{
				if (const UDMXPixelMappingFixtureGroupItemComponent* GroupItemComponent = Cast<UDMXPixelMappingFixtureGroupItemComponent>(ChildOfGroupComponent))
				{
					if (GroupItemComponent->FixturePatchRef.GetFixturePatch() == FixturePatchRef.GetFixturePatch())
					{
						DMXModulators = GroupItemComponent->Modulators;
						return true;
					}
				}
				else if (const UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(ChildOfGroupComponent))
				{
					if (MatrixComponent->FixturePatchRef.GetFixturePatch() == FixturePatchRef.GetFixturePatch())
					{
						DMXModulators = MatrixComponent->Modulators;
						return true;
					}
				}
			}
		}
	}

	return false;
}

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingRendererComponent::TakeWidget()
{
	if (!ComponentsCanvas.IsValid())
	{
		ComponentsCanvas =
			SNew(SConstraintCanvas);
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			// Build all child DMX pixel mapping slots
			Component->BuildSlot(ComponentsCanvas.ToSharedRef());
		}
	}, true);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	return ComponentsCanvas.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingRendererComponent::ResizePreviewRenderTarget(uint32 InSizeX, uint32 InSizeY)
{
	UTextureRenderTarget2D* Target = GetPreviewRenderTarget();

	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{
		check(Target);
		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
}
#endif // WITH_EDITOR

void UDMXPixelMappingRendererComponent::ResetDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent)
		{
			if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
			{
				Component->ResetDMX();
			}
		}, false);
}

void UDMXPixelMappingRendererComponent::SendDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent)
		{
			if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
			{
				SCOPE_CYCLE_COUNTER(STAT_DMXPixelMappingSendDMX);

				Component->SendDMX();
			}
		}, false);
}

void UDMXPixelMappingRendererComponent::Render()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXPixelMappingRender);

	if (!PixelMappingRenderer.IsValid())
	{
		return;
	}

	// 1. Render the input texture
	RendererInputTexture();
	UTexture* DownsampleInputTexture = GetRenderedInputTexture();
	if (!DownsampleInputTexture)
	{
		return;
	}

	// 2. Make sure there is the DownsampleBufferTarget exists and can size can hold all pixels
	CreateOrUpdateDownsampleBufferTarget();

	// 3. reserve enough space for pixels params
	DownsamplePixelParams.Reset(DownsamplePixelCount);

	// 4. Loop through all child pixels in order to get pixels downsample params for rendering
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->QueueDownsample();
		}
	}, false);

	// 5. Make sure pixel count the same with pixel params set number
	if (!ensure(DownsamplePixelParams.Num() == DownsamplePixelCount))
	{
		DownsamplePixelParams.Empty();
		return;
	}

	// 6. Downsample all pixels
	PixelMappingRenderer->DownsampleRender(
		DownsampleInputTexture->GetResource(),
		DownsampleBufferTarget->GetResource(),
		DownsampleBufferTarget->GameThread_GetRenderTargetResource(),
		DownsamplePixelParams, // Copy Set to GPU thread, no empty function call needed
		[this](TArray<FLinearColor>&& InDownsampleBuffer, FIntRect InRect) { SetDownsampleBuffer(MoveTemp(InDownsampleBuffer), InRect); }
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

void UDMXPixelMappingRendererComponent::Initialize()
{
	if (!PixelMappingRenderer.IsValid())
	{
		PixelMappingRenderer = IDMXPixelMappingRendererModule::Get().CreateRenderer();
		PixelMappingRenderer->SetBrightness(Brightness);
	}

	switch (RendererType)
	{
	case(EDMXPixelMappingRendererType::Texture):
		PreprocessRenderer->SetInputTexture(InputTexture.Get());
		break;

	case(EDMXPixelMappingRendererType::Material):
		PreprocessRenderer->SetInputMaterial(InputMaterial.Get());
		break;

	case(EDMXPixelMappingRendererType::UMG):
		UserWidget = CreateWidget(GetWorld(), InputWidget);
		PreprocessRenderer->SetInputUserWidget(UserWidget.Get());
		break;

	default:
		checkf(0, TEXT("Invalid Renderer Type in DMXPixelMappingRendererComponent"));
	}

	SetSize(PreprocessRenderer->GetResultingSize2D());
}

UTextureRenderTarget2D* UDMXPixelMappingRendererComponent::CreateRenderTarget(const FName& InBaseName)
{
	const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), InBaseName);
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
	RenderTarget->ClearColor = ClearTextureColor;
	constexpr bool bInForceLinearGamma = false;
	RenderTarget->InitCustomFormat(GetSize().X, GetSize().Y, EPixelFormat::PF_B8G8R8A8, bInForceLinearGamma);

	return RenderTarget;
}

void UDMXPixelMappingRendererComponent::RendererInputTexture()
{
	SCOPE_CYCLE_COUNTER(STAT_RenderInputTexture);

	PreprocessRenderer->Render();
}

UTexture* UDMXPixelMappingRendererComponent::GetRenderedInputTexture() const
{
	return PreprocessRenderer->GetRenderedTexture();
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

void UDMXPixelMappingRendererComponent::AddPixelToDownsampleSet(FDMXPixelMappingDownsamplePixelParamsV2&& InDownsamplePixelParam)
{
	const FScopeLock Lock(&DownsampleBufferCS);
	DownsamplePixelParams.Emplace(InDownsamplePixelParam);
}

int32 UDMXPixelMappingRendererComponent::GetDownsamplePixelNum()
{
	const FScopeLock Lock(&DownsampleBufferCS);
	return DownsamplePixelParams.Num();
}

void UDMXPixelMappingRendererComponent::SetDownsampleBuffer(TArray<FLinearColor>&& InDownsampleBuffer, FIntRect InRect)
{
	check(IsInRenderingThread());

	if (!bWasEverRendered)
	{
		for (int32 PixelIndex = 0; PixelIndex < GetTotalDownsamplePixelCount(); PixelIndex++)
		{
			ResetColorDownsampleBufferPixel(PixelIndex);
		}

		bWasEverRendered = true;
	}

	FScopeLock ScopeLock(&DownsampleBufferCS);
	DownsampleBuffer = MoveTemp(InDownsampleBuffer);
}

bool UDMXPixelMappingRendererComponent::GetDownsampleBufferPixel(const int32 InDownsamplePixelIndex, FLinearColor& OutLinearColor)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);


	if (!DownsampleBuffer.IsValidIndex(InDownsamplePixelIndex))
	{
		return false;
	}

	OutLinearColor = DownsampleBuffer[InDownsamplePixelIndex];
	return true;
}

bool UDMXPixelMappingRendererComponent::GetDownsampleBufferPixels(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd, TArray<FLinearColor>& OutLinearColors)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);
	
	// Could be out of the range when texture resizing on GPU thread
	if (!IsPixelRangeValid(InDownsamplePixelIndexStart, InDownsamplePixelIndexEnd))
	{
		return false;
	}

	OutLinearColors.Reset(InDownsamplePixelIndexEnd - InDownsamplePixelIndexStart + 1);
	for (int32 PixelIndex = InDownsamplePixelIndexStart; PixelIndex <= InDownsamplePixelIndexEnd; ++PixelIndex)
	{
		OutLinearColors.Add(DownsampleBuffer[PixelIndex]);
	}

	return true;
}

bool UDMXPixelMappingRendererComponent::ResetColorDownsampleBufferPixel(const int32 InDownsamplePixelIndex)
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	if (DownsampleBuffer.IsValidIndex(InDownsamplePixelIndex))
	{
		DownsampleBuffer[InDownsamplePixelIndex] = FLinearColor::Black;
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
		DownsampleBuffer[PixelIndex] = FLinearColor::Black;
	}

	return true;
}

void UDMXPixelMappingRendererComponent::EmptyDownsampleBuffer()
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	DownsampleBuffer.Empty();
}

bool UDMXPixelMappingRendererComponent::IsPixelRangeValid(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd) const
{
	FScopeLock ScopeLock(&DownsampleBufferCS);

	if (InDownsamplePixelIndexEnd >= InDownsamplePixelIndexStart &&
		DownsampleBuffer.IsValidIndex(InDownsamplePixelIndexStart) &&
		DownsampleBuffer.IsValidIndex(InDownsamplePixelIndexEnd))
	{
		return true;
	}

	return false;
}

int32 UDMXPixelMappingRendererComponent::GetTotalDownsamplePixelCount()
{
	SCOPE_CYCLE_COUNTER(STAT_GetTotalDownsamplePixelCount);

	FScopeLock ScopeLock(&DownsampleBufferCS);

	// Reset pixel counter
	DownsamplePixelCount = 0;

	// Count all pixels
	constexpr bool bIsRecursive = true;
	ForEachChildOfClass<UDMXPixelMappingOutputComponent>([&](UDMXPixelMappingOutputComponent* InComponent)
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
