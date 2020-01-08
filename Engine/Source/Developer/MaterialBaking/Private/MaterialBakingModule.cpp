// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialBakingModule.h"
#include "MaterialRenderItem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ExportMaterialProxy.h"
#include "Interfaces/IMainFrameModule.h"
#include "MaterialOptionsWindow.h"
#include "MaterialOptions.h"
#include "PropertyEditorModule.h"
#include "MaterialOptionsCustomization.h"
#include "UObject/UObjectGlobals.h"
#include "MaterialBakingStructures.h"
#include "Framework/Application/SlateApplication.h"
#include "MaterialBakingHelpers.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Materials/MaterialInstance.h"
#include "RenderingThread.h"
#include "Misc/ScopedSlowTask.h"
#include "MeshDescription.h"
#if WITH_EDITOR
#include "Misc/FileHelper.h"
#endif

IMPLEMENT_MODULE(FMaterialBakingModule, MaterialBaking);

#define LOCTEXT_NAMESPACE "MaterialBakingModule"

/** Cvars for advanced features */
static TAutoConsoleVariable<int32> CVarUseMaterialProxyCaching(
	TEXT("MaterialBaking.UseMaterialProxyCaching"),
	1,
	TEXT("Determines whether or not Material Proxies should be cached to speed up material baking.\n")
	TEXT("0: Turned Off\n")
	TEXT("1: Turned On"),	
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarSaveIntermediateTextures(
	TEXT("MaterialBaking.SaveIntermediateTextures"),
	0,
	TEXT("Determines whether or not to save out intermediate BMP images for each flattened material property.\n")
	TEXT("0: Turned Off\n")
	TEXT("1: Turned On"),
	ECVF_Default);

namespace FMaterialBakingModuleImpl
{
	// Custom dynamic mesh allocator specifically tailored for Material Baking.
	// This will always reuse the same couple buffers, so searching linearly is not a problem.
	class FMaterialBakingDynamicMeshBufferAllocator : public FDynamicMeshBufferAllocator
	{
		TArray<FIndexBufferRHIRef>  IndexBuffers;
		TArray<FVertexBufferRHIRef> VertexBuffers;

		template <typename RefType>
		RefType GetSmallestFit(int32 SizeInBytes, TArray<RefType>& Array)
		{
			int32 SmallestFitIndex = -1;
			int32 SmallestFitSize  = -1;
			for (int32 Index = 0; Index < Array.Num(); ++Index)
			{
				int32 Size = Array[Index]->GetSize();
				if (Size >= SizeInBytes && (SmallestFitIndex == -1 || Size < SmallestFitSize))
				{
					SmallestFitIndex = Index;
					SmallestFitSize  = Size;
				}
			}

			RefType Ref;
			if (SmallestFitIndex != -1)
			{
				Ref = Array[SmallestFitIndex];
				Array.RemoveAtSwap(SmallestFitIndex);
			}

			return Ref;
		}

		virtual FIndexBufferRHIRef AllocIndexBuffer(uint32 NumElements) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AllocIndexBuffer)
			FIndexBufferRHIRef Ref = GetSmallestFit(GetIndexBufferSize(NumElements), IndexBuffers);
			if (Ref.IsValid())
			{
				return Ref;
			}

			return FDynamicMeshBufferAllocator::AllocIndexBuffer(NumElements);
		}

		virtual void ReleaseIndexBuffer(FIndexBufferRHIRef& IndexBufferRHI) override
		{
			IndexBuffers.Add(MoveTemp(IndexBufferRHI));
		}

		virtual FVertexBufferRHIRef AllocVertexBuffer(uint32 Stride, uint32 NumElements) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AllocVertexBuffer)
			FVertexBufferRHIRef Ref = GetSmallestFit(GetVertexBufferSize(Stride, NumElements), VertexBuffers);
			if (Ref.IsValid())
			{
				return Ref;
			}

			return FDynamicMeshBufferAllocator::AllocVertexBuffer(Stride, NumElements);
		}

		virtual void ReleaseVertexBuffer(FVertexBufferRHIRef& VertexBufferRHI) override
		{
			VertexBuffers.Add(MoveTemp(VertexBufferRHI));
		}
	};
}

void FMaterialBakingModule::StartupModule()
{
	// Set which properties should enforce gamme correction
	FMemory::Memset(PerPropertyGamma, (uint8)false);
	PerPropertyGamma[MP_Normal] = true;
	PerPropertyGamma[MP_Opacity] = true;
	PerPropertyGamma[MP_OpacityMask] = true;

	// Set which pixel format should be used for the possible baked out material properties
	FMemory::Memset(PerPropertyFormat, (uint8)PF_Unknown);
	PerPropertyFormat[MP_EmissiveColor] = PF_FloatRGBA;
	PerPropertyFormat[MP_Opacity] = PF_B8G8R8A8;
	PerPropertyFormat[MP_OpacityMask] = PF_B8G8R8A8;
	PerPropertyFormat[MP_BaseColor] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Metallic] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Specular] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Roughness] = PF_B8G8R8A8;
	PerPropertyFormat[MP_Normal] = PF_B8G8R8A8;
	PerPropertyFormat[MP_AmbientOcclusion] = PF_B8G8R8A8;
	PerPropertyFormat[MP_SubsurfaceColor] = PF_B8G8R8A8;

	// Register property customization
	FPropertyEditorModule& Module = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	Module.RegisterCustomPropertyTypeLayout(TEXT("PropertyEntry"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPropertyEntryCustomization::MakeInstance));
	
	// Register callback for modified objects
	FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FMaterialBakingModule::OnObjectModified);
}

void FMaterialBakingModule::ShutdownModule()
{
	// Unregister customization and callback
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");

	if (PropertyEditorModule)
	{
		PropertyEditorModule->UnregisterCustomPropertyTypeLayout(TEXT("PropertyEntry"));
	}
	FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
}

void FMaterialBakingModule::BakeMaterials(const TArray<FMaterialData*>& MaterialSettings, const TArray<FMeshData*>& MeshSettings, TArray<FBakeOutput>& Output)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::BakeMaterials)

	checkf(MaterialSettings.Num() == MeshSettings.Num(), TEXT("Number of material settings does not match that of MeshSettings"));
	const int32 NumMaterials = MaterialSettings.Num();
	const bool bSaveIntermediateTextures = CVarSaveIntermediateTextures.GetValueOnAnyThread() == 1;

	FMaterialBakingModuleImpl::FMaterialBakingDynamicMeshBufferAllocator MaterialBakingDynamicMeshBufferAllocator;

	FScopedSlowTask Progress(NumMaterials, LOCTEXT("BakeMaterials", "Baking Materials..."), true );
	Progress.MakeDialog(true);

	TArray<uint32> ProcessingOrder;
	ProcessingOrder.Reserve(MeshSettings.Num());
	for (int32 Index = 0; Index < MeshSettings.Num(); ++Index)
	{
		ProcessingOrder.Add(Index);
	}

	// Start with the biggest mesh first so we can always reuse the same vertex/index buffers.
	// This will decrease the number of allocations backed by newly allocated memory from the OS,
	// which will reduce soft page faults while copying into that memory.
	// Soft page faults are now incredibly expensive on Windows 10.
	Algo::SortBy(
		ProcessingOrder,
		[&MeshSettings](const uint32 Index){ return MeshSettings[Index]->RawMeshDescription ? MeshSettings[Index]->RawMeshDescription->Vertices().Num() : 0; },
		TGreater<>()
	);

	TAtomic<uint32> NumTasks(0);
	Output.SetNum(NumMaterials);

	// Create all material proxies right away to start compiling shaders asynchronously and avoid stalling the baking process as much as possible
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateMaterialProxies)

		for (int32 Index = 0; Index < NumMaterials; ++Index)
		{
			int32 MaterialIndex = ProcessingOrder[Index];
			const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];

			TArray<UTexture*> MaterialTextures;
			CurrentMaterialSettings->Material->GetUsedTextures(MaterialTextures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);

			// Force load materials used by the current material
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LoadTexturesForMaterial)

				for (UTexture* Texture : MaterialTextures)
				{
					if (Texture != NULL)
					{
						UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
						if (Texture2D)
						{
							Texture2D->SetForceMipLevelsToBeResident(30.0f);
							Texture2D->WaitForStreaming();
						}
					}
				}
			}

			for (TMap<EMaterialProperty, FIntPoint>::TConstIterator PropertySizeIterator = CurrentMaterialSettings->PropertySizes.CreateConstIterator(); PropertySizeIterator; ++PropertySizeIterator)
			{
				// They will be stored in the pool and compiled asynchronously
				CreateMaterialProxy(CurrentMaterialSettings->Material, PropertySizeIterator.Key());
			}
		}
	}

	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(BakeOneMaterial)

		Progress.EnterProgressFrame();

		int32 MaterialIndex = ProcessingOrder[Index];
		const FMaterialData* CurrentMaterialSettings = MaterialSettings[MaterialIndex];
		const FMeshData* CurrentMeshSettings = MeshSettings[MaterialIndex];
		FBakeOutput& CurrentOutput = Output[MaterialIndex];

		// Canvas per size / special property?
		TArray<TPair<UTextureRenderTarget2D*, FSceneViewFamily>> TargetsViewFamilyPairs;
		TArray<FExportMaterialProxy*> MaterialRenderProxies;
		TArray<EMaterialProperty> MaterialPropertiesToBakeOut;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CreateMaterialProxies)

			for (TMap<EMaterialProperty, FIntPoint>::TConstIterator PropertySizeIterator = CurrentMaterialSettings->PropertySizes.CreateConstIterator(); PropertySizeIterator; ++PropertySizeIterator)
			{
				const EMaterialProperty& Property = PropertySizeIterator.Key();
				UTextureRenderTarget2D* RenderTarget = CreateRenderTarget(PerPropertyGamma[Property], PerPropertyFormat[Property], PropertySizeIterator.Value());
				FExportMaterialProxy* Proxy = CreateMaterialProxy(CurrentMaterialSettings->Material, Property);

				FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget->GameThread_GetRenderTargetResource(), nullptr,
					FEngineShowFlags(ESFIM_Game))
					.SetWorldTimes(0.0f, 0.0f, 0.0f)
					.SetGammaCorrection(RenderTarget->GameThread_GetRenderTargetResource()->GetDisplayGamma()));

				TargetsViewFamilyPairs.Add(TPair<UTextureRenderTarget2D*, FSceneViewFamily>(RenderTarget, Forward<FSceneViewFamily>(ViewFamily)));
				MaterialRenderProxies.Add(Proxy);
				MaterialPropertiesToBakeOut.Add(Property);
			}
		}

		const int32 NumPropertiesToRender = CurrentMaterialSettings->PropertySizes.Num();
		if (NumPropertiesToRender > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RenderOneMaterial)

			// Ensure data in memory will not change place while adding to avoid race conditions
			CurrentOutput.PropertyData.Reserve(NumPropertiesToRender);
			CurrentOutput.PropertySizes.Reserve(NumPropertiesToRender);

			FCanvas Canvas(TargetsViewFamilyPairs[0].Key->GameThread_GetRenderTargetResource(), nullptr, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, GMaxRHIFeatureLevel);
			Canvas.SetAllowedModes(FCanvas::Allow_Flush);

			UTextureRenderTarget2D* PreviousRenderTarget = nullptr;
			FTextureRenderTargetResource* RenderTargetResource = nullptr;

			// The RenderItem will use our custom allocator for vertex/index buffers.
			FMeshMaterialRenderItem RenderItem(CurrentMaterialSettings, CurrentMeshSettings, MaterialPropertiesToBakeOut[0], &MaterialBakingDynamicMeshBufferAllocator);
			FCanvas::FCanvasSortElement& SortElement = Canvas.GetSortElement(Canvas.TopDepthSortKey());
			
			for (int32 PropertyIndex = 0; PropertyIndex < NumPropertiesToRender; ++PropertyIndex)
			{
				const EMaterialProperty Property = MaterialPropertiesToBakeOut[PropertyIndex];
				FExportMaterialProxy* ExportMaterialProxy = MaterialRenderProxies[PropertyIndex];

				if (!ExportMaterialProxy->IsCompilationFinished())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitForMaterialProxyCompilation)
					ExportMaterialProxy->FinishCompilation();
				}

				// Update render item
				RenderItem.MaterialProperty = Property;
				RenderItem.MaterialRenderProxy = ExportMaterialProxy;

				UTextureRenderTarget2D* RenderTarget = TargetsViewFamilyPairs[PropertyIndex].Key;
				if (RenderTarget != nullptr)
				{
					// Check if the render target changed, in which case we will need to reinit the resource, view family and possibly the mesh data
					if (RenderTarget != PreviousRenderTarget)
					{
						// Setup render target and view family
						RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
						Canvas.SetRenderTarget_GameThread(RenderTargetResource);
						const FRenderTarget* CanvasRenderTarget = Canvas.GetRenderTarget();

						RenderItem.ViewFamily = &TargetsViewFamilyPairs[PropertyIndex].Value;

						if (PreviousRenderTarget != nullptr && (PreviousRenderTarget->GetSurfaceHeight() != RenderTarget->GetSurfaceHeight() || PreviousRenderTarget->GetSurfaceWidth() != RenderTarget->GetSurfaceWidth()))
						{
							RenderItem.GenerateRenderData();
						}

						Canvas.SetRenderTargetRect(FIntRect(0, 0, RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight()));
						Canvas.SetBaseTransform(Canvas.CalcBaseTransform2D(RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight()));
						PreviousRenderTarget = RenderTarget;
					}
					
					// TODO find out why this is required
					static bool bDoubleFlush = false;
					if (IsRunningCommandlet() && !bDoubleFlush)
					{
						Canvas.Clear(RenderTarget->ClearColor);
						SortElement.RenderBatchArray.Add(&RenderItem);
						Canvas.Flush_GameThread();
						FlushRenderingCommands();
						bDoubleFlush = true;
					}

					// Clear canvas before rendering
					Canvas.Clear(RenderTarget->ClearColor);

					SortElement.RenderBatchArray.Add(&RenderItem);

					// Do rendering
					Canvas.Flush_GameThread();

					SortElement.RenderBatchArray.Empty();

					// Reading texture does an implicit FlushRenderingCommands()
					ReadTextureOutput(RenderTargetResource, Property, CurrentOutput);

					// Schedule final processing for another thread
					NumTasks++;
					auto FinalProcessing =
						[&NumTasks, bSaveIntermediateTextures, CurrentMaterialSettings, MaterialIndex, &Output, Property]()
						{
							FBakeOutput& CurrentOutput = Output[MaterialIndex];

							if (CurrentMaterialSettings->bPerformBorderSmear)
							{
								// This will resize the output to a single pixel if the result is monochrome.
								FMaterialBakingHelpers::PerformUVBorderSmearAndShrink(
									CurrentOutput.PropertyData[Property],
									CurrentOutput.PropertySizes[Property].X,
									CurrentOutput.PropertySizes[Property].Y
								);
							}
#if WITH_EDITOR
							// If saving intermediates is turned on
							if (bSaveIntermediateTextures)
							{
								const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
								FName PropertyName = PropertyEnum->GetNameByValue(Property);
								FString TrimmedPropertyName = PropertyName.ToString();
								TrimmedPropertyName.RemoveFromStart(TEXT("MP_"));

								const FString DirectoryPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() + TEXT("MaterialBaking/"));
								FString FilenameString = FString::Printf(TEXT("%s%s-%d-%s.bmp"), *DirectoryPath, *CurrentMaterialSettings->Material->GetName(), MaterialIndex, *TrimmedPropertyName);
								FFileHelper::CreateBitmap(*FilenameString, CurrentOutput.PropertySizes[Property].X, CurrentOutput.PropertySizes[Property].Y, CurrentOutput.PropertyData[Property].GetData());
							}
#endif // WITH_EDITOR
							NumTasks--;
						};

					if (FPlatformProcess::SupportsMultithreading())
					{
						Async(EAsyncExecution::ThreadPool, MoveTemp(FinalProcessing));
					}
					else
					{
						FinalProcessing();
					}
				}
			}
		}
	}

	while (NumTasks.Load(EMemoryOrder::Relaxed) > 0)
	{
		FPlatformProcess::Sleep(0.1f);
	}

	if (!CVarUseMaterialProxyCaching.GetValueOnAnyThread())
	{
		CleanupMaterialProxies();
	}
}

bool FMaterialBakingModule::SetupMaterialBakeSettings(TArray<TWeakObjectPtr<UObject>>& OptionObjects, int32 NumLODs)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Material Baking Options"))
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SMaterialOptions> Options;

	Window->SetContent
	(
		SAssignNew(Options, SMaterialOptions)
		.WidgetWindow(Window)
		.NumLODs(NumLODs)
		.SettingsObjects(OptionObjects)
	);

	TSharedPtr<SWindow> ParentWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
		return !Options->WasUserCancelled();
	}

	return false;
}

void FMaterialBakingModule::CleanupMaterialProxies()
{
	for (auto Iterator : MaterialProxyPool)
	{
		delete Iterator.Value.Value;
	}
	MaterialProxyPool.Reset();
}

UTextureRenderTarget2D* FMaterialBakingModule::CreateRenderTarget(bool bInForceLinearGamma, EPixelFormat InPixelFormat, const FIntPoint& InTargetSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::CreateRenderTarget)

	UTextureRenderTarget2D* RenderTarget = nullptr;
	const int32 MaxTextureSize = 1 << (MAX_TEXTURE_MIP_COUNT - 1); // Don't use GetMax2DTextureDimension() as this is for the RHI only.
	const FIntPoint ClampedTargetSize(FMath::Clamp(InTargetSize.X, 1, MaxTextureSize), FMath::Clamp(InTargetSize.Y, 1, MaxTextureSize));
	auto RenderTargetComparison = [bInForceLinearGamma, InPixelFormat, ClampedTargetSize](const UTextureRenderTarget2D* CompareRenderTarget) -> bool
	{
		return (CompareRenderTarget->SizeX == ClampedTargetSize.X && CompareRenderTarget->SizeY == ClampedTargetSize.Y && CompareRenderTarget->OverrideFormat == InPixelFormat && CompareRenderTarget->bForceLinearGamma == bInForceLinearGamma);
	};

	// Find any pooled render target with suitable properties.
	UTextureRenderTarget2D** FindResult = RenderTargetPool.FindByPredicate(RenderTargetComparison);
	
	if (FindResult)
	{
		RenderTarget = *FindResult;
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateNewRenderTarget)

		// Not found - create a new one.
		RenderTarget = NewObject<UTextureRenderTarget2D>();
		check(RenderTarget);
		RenderTarget->AddToRoot();
		RenderTarget->ClearColor = FLinearColor(1.0f, 0.0f, 1.0f);
		RenderTarget->ClearColor.A = 1.0f;
		RenderTarget->TargetGamma = 0.0f;
		RenderTarget->InitCustomFormat(ClampedTargetSize.X, ClampedTargetSize.Y, InPixelFormat, bInForceLinearGamma);

		RenderTargetPool.Add(RenderTarget);
	}

	checkf(RenderTarget != nullptr, TEXT("Unable to create or find valid render target"));
	return RenderTarget;
}

FExportMaterialProxy* FMaterialBakingModule::CreateMaterialProxy(UMaterialInterface* Material, const EMaterialProperty Property)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::CreateMaterialProxy)

	FExportMaterialProxy* Proxy = nullptr;

	// Find all pooled material proxy matching this material
	TArray<FMaterialPoolValue> Entries;
	MaterialProxyPool.MultiFind(Material, Entries);

	// Look for the matching property
	for (FMaterialPoolValue& Entry : Entries)
	{
		if (Entry.Key == Property)
		{
			Proxy = Entry.Value;
			break;
		}
	}

	// Not found, create a new entry
	if (Proxy == nullptr)
	{
		Proxy = new FExportMaterialProxy(Material, Property, false /* bInSynchronousCompilation */);
		MaterialProxyPool.Add(Material, FMaterialPoolValue(Property, Proxy));
	}

	return Proxy;
}

void FMaterialBakingModule::ReadTextureOutput(FTextureRenderTargetResource* RenderTargetResource, EMaterialProperty Property, FBakeOutput& Output)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::ReadTextureOutput)

	checkf(!Output.PropertyData.Contains(Property) && !Output.PropertySizes.Contains(Property), TEXT("Should not be reading same property data twice"));

	TArray<FColor>& OutputColor = Output.PropertyData.Add(Property);
	FIntPoint& OutputSize = Output.PropertySizes.Add(Property);	
	
	// Retrieve rendered size
	OutputSize = RenderTargetResource->GetSizeXY();

	const bool bNormalmap = (Property== MP_Normal);
	FReadSurfaceDataFlags ReadPixelFlags(bNormalmap ? RCM_SNorm : RCM_UNorm);
	ReadPixelFlags.SetLinearToGamma(false);
		
	if (Property != MP_EmissiveColor)
	{
		// Read out pixel data
		RenderTargetResource->ReadPixels(OutputColor, ReadPixelFlags);
	}
	else
	{
		// Emissive is a special case	
		TArray<FFloat16Color> Color16;
		RenderTargetResource->ReadFloat16Pixels(Color16);		
		
		const int32 NumThreads = [&]()
		{
			return FPlatformProcess::SupportsMultithreading() ? FPlatformMisc::NumberOfCores() : 1;
		}();

		float* MaxValue = new float[NumThreads];
		FMemory::Memset(MaxValue, 0, NumThreads * sizeof(MaxValue[0]));
		const int32 LinesPerThread = FMath::CeilToInt((float)OutputSize.Y / (float)NumThreads);

		// Find maximum float value across texture
		ParallelFor(NumThreads, [&Color16, LinesPerThread, MaxValue, OutputSize](int32 Index)
		{
			const int32 EndY = FMath::Min((Index + 1) * LinesPerThread, OutputSize.Y);			
			float& CurrentMaxValue = MaxValue[Index];
			const FFloat16Color MagentaFloat16 = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f));
			for (int32 PixelY = Index * LinesPerThread; PixelY < EndY; ++PixelY)
			{
				const int32 YOffset = PixelY * OutputSize.X;
				for (int32 PixelX = 0; PixelX < OutputSize.X; PixelX++)
				{
					const FFloat16Color& Pixel16 = Color16[PixelX + YOffset];
					// Find maximum channel value across texture
					if (!(Pixel16 == MagentaFloat16))
					{
						CurrentMaxValue = FMath::Max(CurrentMaxValue, FMath::Max3(Pixel16.R.GetFloat(), Pixel16.G.GetFloat(), Pixel16.B.GetFloat()));
					}
				}
			}
		});

		const float GlobalMaxValue = [&MaxValue, NumThreads]
		{
			float TempValue = 0.0f;
			for (int32 ThreadIndex = 0; ThreadIndex < NumThreads; ++ThreadIndex)
			{
				TempValue = FMath::Max(TempValue, MaxValue[ThreadIndex]);
			}

			return TempValue;
		}();
		
		if (GlobalMaxValue <= 0.01f)
		{
			// Black emissive, drop it			
		}

		// Now convert Float16 to Color using the scale
		OutputColor.SetNumUninitialized(Color16.Num());
		const float Scale = 255.0f / GlobalMaxValue;
		ParallelFor(NumThreads, [&Color16, LinesPerThread, &OutputColor, OutputSize, Scale](int32 Index)
		{
			const int32 EndY = FMath::Min((Index + 1) * LinesPerThread, OutputSize.Y);
			for (int32 PixelY = Index * LinesPerThread; PixelY < EndY; ++PixelY)
			{
				const int32 YOffset = PixelY * OutputSize.X;
				for (int32 PixelX = 0; PixelX < OutputSize.X; PixelX++)
				{
					const FFloat16Color& Pixel16 = Color16[PixelX + YOffset];

					const FFloat16Color MagentaFloat16 = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f));
					FColor& Pixel8 = OutputColor[PixelX + YOffset];
					if (Pixel16 == MagentaFloat16)
					{
						Pixel8.R = 255;
						Pixel8.G = 0;
						Pixel8.B = 255;
					}
					else
					{
						Pixel8.R = (uint8)FMath::RoundToInt(Pixel16.R.GetFloat() * Scale);
						Pixel8.G = (uint8)FMath::RoundToInt(Pixel16.G.GetFloat() * Scale);
						Pixel8.B = (uint8)FMath::RoundToInt(Pixel16.B.GetFloat() * Scale);						
					}
					
					
					Pixel8.A = 255;
				}
			}
		});

		// This scale will be used in the proxy material to get the original range of emissive values outside of 0-1
		Output.EmissiveScale = GlobalMaxValue;
	}	
}

void FMaterialBakingModule::OnObjectModified(UObject* Object)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMaterialBakingModule::OnObjectModified)

	if (CVarUseMaterialProxyCaching.GetValueOnAnyThread())
	{
		if (Object && Object->IsA<UMaterialInterface>())
		{
			UMaterialInterface* MaterialToInvalidate = Cast<UMaterialInterface>(Object);

			// Search our proxy pool for materials or material instances that refer to MaterialToInvalidate
			for (auto It = MaterialProxyPool.CreateIterator(); It; ++It)
			{
				TWeakObjectPtr<UMaterialInterface> PoolMaterialPtr = It.Key();

				// Remove stale entries from the pool
				bool bMustDelete = PoolMaterialPtr.IsValid();
				if (!bMustDelete)
				{
					bMustDelete = PoolMaterialPtr == MaterialToInvalidate;
				}

				// No match - Test the MaterialInstance hierarchy
				if (!bMustDelete)
				{
					UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(PoolMaterialPtr);
					while (!bMustDelete && MaterialInstance && MaterialInstance->Parent != nullptr)
					{
						bMustDelete = MaterialInstance->Parent == MaterialToInvalidate;
						MaterialInstance = Cast<UMaterialInstance>(MaterialInstance->Parent);
					}
				}

				// We have a match, remove the entry from our pool
				if (bMustDelete)
				{
					FExportMaterialProxy* Proxy = It.Value().Value;

					ENQUEUE_RENDER_COMMAND(DeleteCachedMaterialProxy)(
						[Proxy](FRHICommandListImmediate& RHICmdList)
						{
							delete Proxy;
						});

					It.RemoveCurrent();
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE //"MaterialBakingModule"
