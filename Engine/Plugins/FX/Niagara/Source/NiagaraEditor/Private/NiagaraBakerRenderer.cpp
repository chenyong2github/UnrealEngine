// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerRenderer.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraBatchedElements.h"

#include "Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h"
#include "Niagara/Classes/NiagaraDataInterfaceGrid2DCollection.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "BufferVisualizationData.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "PackageTools.h"

namespace NiagaraBakerRendererLocal
{
	const FString STRING_SceneCaptureSource("SceneCaptureSource");
	const FString STRING_BufferVisualization("BufferVisualization");
	const FString STRING_EmitterDI("EmitterDI");
	const FString STRING_EmitterParticles("EmitterParticles");
}

FNiagaraBakerRenderer::FNiagaraBakerRenderer()
{
	SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	SceneCaptureComponent->bTickInEditor = false;
	SceneCaptureComponent->SetComponentTickEnabled(false);
	SceneCaptureComponent->SetVisibility(true);
	SceneCaptureComponent->bCaptureEveryFrame = false;
	SceneCaptureComponent->bCaptureOnMovement = false;
}

FNiagaraBakerRenderer::~FNiagaraBakerRenderer()
{
}

bool FNiagaraBakerRenderer::RenderView(UNiagaraComponent* PreviewComponent, const UNiagaraBakerSettings* BakerSettings, float WorldTime, UTextureRenderTarget2D* RenderTarget, int32 iOutputTextureIndex) const
{
	if (!PreviewComponent || !BakerSettings || !RenderTarget)
	{
		return false;
	}

	UWorld* World = PreviewComponent->GetWorld();
	if ( (World == nullptr) || (World->Scene == nullptr) )
	{
		return false;
	}

	FCanvas Canvas(RenderTarget->GameThread_GetRenderTargetResource(), nullptr, FGameTime::CreateUndilated(WorldTime, FApp::GetDeltaTime()), World->Scene->GetFeatureLevel());
	Canvas.Clear(FLinearColor::Black);

	bool bRendered = false;
	if (BakerSettings->OutputTextures.IsValidIndex(iOutputTextureIndex))
	{
		const FNiagaraBakerTextureSettings& OutputTextureSettings = BakerSettings->OutputTextures[iOutputTextureIndex];
		const FIntRect ViewRect = FIntRect(0, 0, OutputTextureSettings.FrameSize.X, OutputTextureSettings.FrameSize.Y);
		bRendered = RenderView(PreviewComponent, BakerSettings, WorldTime, RenderTarget, &Canvas, iOutputTextureIndex, ViewRect);
	}
	Canvas.Flush_GameThread();
	return bRendered;
}

bool FNiagaraBakerRenderer::RenderView(UNiagaraComponent* PreviewComponent, const UNiagaraBakerSettings* BakerSettings, float WorldTime, UTextureRenderTarget2D* RenderTarget, FCanvas* Canvas, int32 iOutputTextureIndex, FIntRect ViewRect) const
{
	using namespace NiagaraBakerRendererLocal;

	if (!PreviewComponent || !BakerSettings || !RenderTarget)
	{
		return false;
	}

	if (!BakerSettings->OutputTextures.IsValidIndex(iOutputTextureIndex))
	{
		return false;
	}

	if (Canvas->IsHitTesting())
	{
		return false;
	}

	UWorld* World = PreviewComponent->GetWorld();
	check(World);

	// We need any GPU sims to flush pending ticks
	FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface = FNiagaraGpuComputeDispatchInterface::Get(World);
	ensureMsgf(ComputeDispatchInterface, TEXT("The batcher was not valid on the world this may result in incorrect baking"));
	if (ComputeDispatchInterface)
	{
		ComputeDispatchInterface->FlushPendingTicks_GameThread();
	}
	
	const FNiagaraBakerTextureSettings& OutputTextureSettings = BakerSettings->OutputTextures[iOutputTextureIndex];

	// Validate the rect
	if ( ViewRect.Area() <= 0 )
	{
		return false;
	}

	// Get visualization options
	float GammaCorrection = 1.0f;
	FName SourceName;

	auto RenderType = GetRenderType(OutputTextureSettings.SourceBinding.SourceName, SourceName);
	switch ( RenderType )
	{
		case ERenderType::SceneCapture:
		{
			// Setup capture component
			SceneCaptureComponent->RegisterComponentWithWorld(World);
			SceneCaptureComponent->TextureTarget = RenderTarget;

			// Translate source to enum
			static UEnum* SceneCaptureOptions = StaticEnum<ESceneCaptureSource>();
			const int32 CaptureSource = SceneCaptureOptions->GetIndexByName(SourceName);
			SceneCaptureComponent->CaptureSource = (CaptureSource == INDEX_NONE) ? ESceneCaptureSource::SCS_SceneColorHDR : ESceneCaptureSource(CaptureSource);

			// Set view location
			if (BakerSettings->IsOrthographic())
			{
				SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
				SceneCaptureComponent->OrthoWidth = BakerSettings->CameraOrthoWidth;
			}
			else
			{
				SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
				SceneCaptureComponent->FOVAngle = BakerSettings->CameraFOV;
			}

			const FMatrix SceneCaptureMatrix = FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
			FMatrix ViewMatrix = SceneCaptureMatrix * BakerSettings->GetViewportMatrix().Inverse() * FRotationTranslationMatrix(BakerSettings->GetCameraRotation(), BakerSettings->GetCameraLocation());
			SceneCaptureComponent->SetWorldLocationAndRotation(ViewMatrix.GetOrigin(), ViewMatrix.Rotator());

			SceneCaptureComponent->bUseCustomProjectionMatrix = true;
			SceneCaptureComponent->CustomProjectionMatrix = BakerSettings->GetProjectionMatrixForTexture(iOutputTextureIndex);

			SceneCaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			SceneCaptureComponent->ShowOnlyComponents.Empty(1);
			SceneCaptureComponent->ShowOnlyComponents.Add(PreviewComponent);

			SceneCaptureComponent->CaptureScene();

			SceneCaptureComponent->TextureTarget = nullptr;
			SceneCaptureComponent->UnregisterComponent();

			// Alpha from a scene capture is 1- so we need to invert
			if ( SceneCaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneColorHDR )
			{
				FCanvasTileItem TileItem(FVector2D(ViewRect.Min.X, ViewRect.Min.Y), FVector2D(ViewRect.Width(), ViewRect.Height()), FLinearColor::White);
				TileItem.BlendMode = SE_BLEND_Opaque;
				TileItem.BatchedElementParameters = new FBatchedElementNiagaraInvertColorChannel(0);
				Canvas->DrawItem(TileItem);
			}

			return true;
		}

		// Regular render
		case ERenderType::BufferVisualization:
		{
			// Create View Family
			FSceneViewFamilyContext ViewFamily(
				FSceneViewFamily::ConstructionValues(RenderTarget->GameThread_GetRenderTargetResource(), World->Scene, FEngineShowFlags(ESFIM_Game))
				.SetTime(FGameTime::CreateUndilated(WorldTime, FApp::GetDeltaTime()))
				.SetGammaCorrection(GammaCorrection)
			);

			ViewFamily.EngineShowFlags.SetScreenPercentage(false);
			//ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
			//ViewFamily.EngineShowFlags.MotionBlur = 0;
			//ViewFamily.EngineShowFlags.SetDistanceCulledPrimitives(true); // show distance culled objects
			//ViewFamily.EngineShowFlags.SetPostProcessing(false);

			if (SourceName.IsValid())
			{
				ViewFamily.EngineShowFlags.SetPostProcessing(true);
				ViewFamily.EngineShowFlags.SetVisualizeBuffer(true);
				ViewFamily.EngineShowFlags.SetTonemapper(false);
				ViewFamily.EngineShowFlags.SetScreenPercentage(false);
			}

			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.SetViewRectangle(ViewRect);
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.ViewOrigin = BakerSettings->GetCameraLocation();
			ViewInitOptions.ViewRotationMatrix = BakerSettings->GetViewMatrix();
			ViewInitOptions.ProjectionMatrix = BakerSettings->GetProjectionMatrixForTexture(iOutputTextureIndex);
			ViewInitOptions.BackgroundColor = FLinearColor::Black;
			if (BakerSettings->bRenderComponentOnly)
			{
				ViewInitOptions.ShowOnlyPrimitives.Emplace();
				ViewInitOptions.ShowOnlyPrimitives->Add(PreviewComponent->ComponentId);
			}

			FSceneView* NewView = new FSceneView(ViewInitOptions);
			NewView->CurrentBufferVisualizationMode = SourceName;
			ViewFamily.Views.Add(NewView);

			ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f));

			GetRendererModule().BeginRenderingViewFamily(Canvas, &ViewFamily);

			return true;
		}

		// Data Interface
		case ERenderType::DataInterface:
		{
			if (UNiagaraSystem* NiagaraSystem = BakerSettings->GetTypedOuter<UNiagaraSystem>())
			{
				// Gather data interface / attribute name
				FString SourceString = SourceName.ToString();
				int32 DotIndex;
				if (!SourceString.FindLastChar('.', DotIndex))
				{
					return false;
				}

				const FName DataInterfaceName = FName(SourceString.LeftChop(SourceString.Len() - DotIndex));
				const FName VariableName = FName(SourceString.RightChop(DotIndex + 1));

				// Find data interface
				FNiagaraSystemInstanceControllerPtr SystemInstanceController = PreviewComponent->GetSystemInstanceController();
				check(SystemInstanceController.IsValid());

				FNiagaraSystemInstance* SystemInstance = SystemInstanceController->GetSoloSystemInstance();
				const FNiagaraSystemInstanceID SystemInstanceID = SystemInstance->GetId();
				for (auto EmitterInstance : SystemInstance->GetEmitters())
				{
					if (FNiagaraComputeExecutionContext* ExecContext = EmitterInstance->GetGPUContext())
					{
						for (const FNiagaraVariableWithOffset& Variable : ExecContext->CombinedParamStore.ReadParameterVariables())
						{
							if (Variable.IsDataInterface())
							{
								if (Variable.GetName() == DataInterfaceName)
								{
									UNiagaraDataInterface* DataInterface = ExecContext->CombinedParamStore.GetDataInterface(Variable.Offset);
									check(DataInterface);
									return DataInterface->RenderVariableToCanvas(SystemInstanceID, VariableName, Canvas, ViewRect);
								}
							}
						}
					}
				}
			}
			return false;
		}

		// Particle attribute
		case ERenderType::Particle:
		{
			FString SourceString = SourceName.ToString();
			int32 DotIndex;
			if ( !SourceString.FindChar('.', DotIndex) )
			{
				return false;
			}

			const FString EmitterName = SourceString.LeftChop(SourceString.Len() - DotIndex);
			const FName AttributeName = FName(SourceString.RightChop(DotIndex + 1));

			FNiagaraSystemInstanceControllerPtr SystemInstanceController = PreviewComponent->GetSystemInstanceController();
			if (!ensure(SystemInstanceController.IsValid()))
			{
				return false;
			}

			FNiagaraSystemInstance* SystemInstance = SystemInstanceController->GetSoloSystemInstance();
			if ( !ensure(SystemInstance) )
			{
				return false;
			}

			for ( const auto& EmitterInstance : SystemInstance->GetEmitters() )
			{
				UNiagaraEmitter* NiagaraEmitter = EmitterInstance->GetCachedEmitter();
				if ( !NiagaraEmitter || (NiagaraEmitter->GetUniqueEmitterName() != EmitterName) )
				{
					continue;
				}

				//-TODO: We don't support GPU Context currently
				if (EmitterInstance->GetGPUContext() != nullptr)
				{
					return false;
				}

				const FNiagaraDataSet& ParticleDataSet = EmitterInstance->GetData();
				const FNiagaraDataBuffer* ParticleDataBuffer = ParticleDataSet.GetCurrentData();
				FNiagaraDataSetReaderInt32<int32> UniqueIDAccessor = FNiagaraDataSetAccessor<int32>::CreateReader(ParticleDataSet, FName("UniqueID"));
				if ( !ParticleDataBuffer || !UniqueIDAccessor.IsValid() )
				{
					return false;
				}

				const int32 VariableIndex = ParticleDataSet.GetCompiledData().Variables.IndexOfByPredicate([&AttributeName](const FNiagaraVariable& Variable) { return Variable.GetName() == AttributeName; });
				if (VariableIndex == INDEX_NONE)
				{
					return false;
				}
				const FNiagaraVariableLayoutInfo& VariableInfo = ParticleDataSet.GetCompiledData().VariableLayouts[VariableIndex];

				float* FloatChannels[4];
				FloatChannels[0] = (float*)ParticleDataBuffer->GetComponentPtrFloat(VariableInfo.FloatComponentStart);
				FloatChannels[1] = VariableInfo.GetNumFloatComponents() > 1 ? (float*)ParticleDataBuffer->GetComponentPtrFloat(VariableInfo.FloatComponentStart + 1) : nullptr;
				FloatChannels[2] = VariableInfo.GetNumFloatComponents() > 2 ? (float*)ParticleDataBuffer->GetComponentPtrFloat(VariableInfo.FloatComponentStart + 2) : nullptr;
				FloatChannels[3] = VariableInfo.GetNumFloatComponents() > 3 ? (float*)ParticleDataBuffer->GetComponentPtrFloat(VariableInfo.FloatComponentStart + 3) : nullptr;

				const int32 ParticleBufferStore = OutputTextureSettings.TextureSize.X * OutputTextureSettings.TextureSize.Y;
				for ( uint32 i=0; i < ParticleDataBuffer->GetNumInstances(); ++i )
				{
					const int32 UniqueID = UniqueIDAccessor[i];
					if (UniqueID >= ParticleBufferStore)
					{
						continue;
					}

					FLinearColor OutputColor;
					OutputColor.R = FloatChannels[0] ? FloatChannels[0][i] : 0.0f;
					OutputColor.G = FloatChannels[1] ? FloatChannels[1][i] : 0.0f;
					OutputColor.B = FloatChannels[2] ? FloatChannels[2][i] : 0.0f;
					OutputColor.A = FloatChannels[3] ? FloatChannels[3][i] : 0.0f;

					const int32 TexelX = UniqueID % OutputTextureSettings.TextureSize.X;
					const int32 TexelY = UniqueID / OutputTextureSettings.TextureSize.X;
					Canvas->DrawTile(TexelX, TexelY, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, OutputColor);
				}

				return true;
			}

			return false;
		}
	}
	return false;
}

void FNiagaraBakerRenderer::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SceneCaptureComponent);
}

FNiagaraBakerRenderer::ERenderType FNiagaraBakerRenderer::GetRenderType(FName SourceName, FName& OutName)
{
	using namespace NiagaraBakerRendererLocal;

	OutName = FName();
	if (SourceName.IsNone())
	{
		return ERenderType::SceneCapture;
	}

	FString SourceBindingString = SourceName.ToString();
	TArray<FString> SplitNames;
	SourceBindingString.ParseIntoArray(SplitNames, TEXT("."));

	if (!ensure(SplitNames.Num() > 0))
	{
		return ERenderType::None;
	}

	// Scene Capture mode
	if ( SplitNames[0] == STRING_SceneCaptureSource )
	{
		if (!ensure(SplitNames.Num() == 2))
		{
			return ERenderType::None;
		}

		OutName = FName(SplitNames[1]);
		return ERenderType::SceneCapture;
	}

	// Buffer Visualization Mode
	if (SplitNames[0] == STRING_BufferVisualization)
	{
		if (!ensure(SplitNames.Num() == 2))
		{
			return ERenderType::None;
		}
		OutName = FName(SplitNames[1]);
		return ERenderType::BufferVisualization;
	}

	// Emitter Data Interface
	if (SplitNames[0] == STRING_EmitterDI)
	{
		OutName = FName(*SourceBindingString.RightChop(SplitNames[0].Len() + 1));
		return ERenderType::DataInterface;
	}

	// Emitter Data Interface
	if (SplitNames[0] == STRING_EmitterParticles)
	{
		OutName = FName(*SourceBindingString.RightChop(SplitNames[0].Len() + 1));
		return ERenderType::Particle;
	}
	return ERenderType::None;
}

TArray<FName> FNiagaraBakerRenderer::GatherAllRenderOptions(UNiagaraSystem* NiagaraSystem)
{
	using namespace NiagaraBakerRendererLocal;

	TArray<FName> RendererOptions;

	// Put all scene capture options
	static UEnum* SceneCaptureOptions = StaticEnum<ESceneCaptureSource>();
	for ( int i=0; i < SceneCaptureOptions->GetMaxEnumValue(); ++i )
	{
		RendererOptions.Emplace(STRING_SceneCaptureSource + TEXT(".") + SceneCaptureOptions->GetNameStringByIndex(i));
	}

	// Gather all buffer visualization options
	struct FIterator
	{
		TArray<FName>& OutAllOptions;

		FIterator(TArray<FName>& InOutAllOptions)
			: OutAllOptions(InOutAllOptions)
		{}

		void ProcessValue(const FString& MaterialName, UMaterialInterface* Material, const FText& DisplayName)
		{
			OutAllOptions.Emplace(STRING_BufferVisualization + TEXT(".") + MaterialName);
		}
	} Iterator(RendererOptions);
	GetBufferVisualizationData().IterateOverAvailableMaterials(Iterator);

	if (!NiagaraSystem)
	{
		return RendererOptions;
	}

	const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& AllEmitterCompiledData = NiagaraSystem->GetEmitterCompiledData();

	// For each emitter find data interfaces / particle attributes we can render
	for ( int32 EmitterIndex=0; EmitterIndex < NiagaraSystem->GetEmitterHandles().Num(); ++EmitterIndex )
	{
		const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(EmitterIndex);
		UNiagaraEmitter* EmitterInstance = EmitterHandle.GetInstance();
		if (!EmitterHandle.IsValid() || !EmitterHandle.GetIsEnabled() || !EmitterInstance)
		{
			continue;
		}

		FString EmitterName = EmitterHandle.GetName().ToString();
		FString EmitterPrefix = EmitterName + TEXT(".");

		// Gather particle attributes
		if ( ensure(AllEmitterCompiledData.IsValidIndex(EmitterIndex)) )
		{
			const FNiagaraDataSetCompiledData& ParticleDataSet = AllEmitterCompiledData[EmitterIndex]->DataSetCompiledData;
			for ( int32 iVariable=0; iVariable < ParticleDataSet.VariableLayouts.Num(); ++iVariable )
			{
				const FNiagaraVariable& Variable = ParticleDataSet.Variables[iVariable];
				const FNiagaraVariableLayoutInfo& VariableLayout = ParticleDataSet.VariableLayouts[iVariable];
				if ( VariableLayout.GetNumFloatComponents() > 0 )
				{
					RendererOptions.Emplace(STRING_EmitterParticles + TEXT(".") + EmitterName + TEXT(".") + *Variable.GetName().ToString());
				}
			}
		}

		// Gather data interfaces
		EmitterInstance->ForEachScript(
			[&](UNiagaraScript* NiagaraScript)
			{
				if (const FNiagaraScriptExecutionParameterStore* SrcStore = NiagaraScript->GetExecutionReadyParameterStore(EmitterInstance->SimTarget))
				{
					for (const FNiagaraVariableWithOffset& Variable : SrcStore->ReadParameterVariables())
					{
						if (Variable.IsDataInterface())
						{
							FString VariableName = Variable.GetName().ToString();
							if (!VariableName.StartsWith(EmitterPrefix))
							{
								continue;
							}

							UNiagaraDataInterface* DataInterface = SrcStore->GetDataInterface(Variable.Offset);
							check(DataInterface);

							if ( DataInterface->CanRenderVariablesToCanvas() )
							{
								TArray<FNiagaraVariableBase> RendererableVariables;
								DataInterface->GetCanvasVariables(RendererableVariables);
								for (const FNiagaraVariableBase& RendererableVariable : RendererableVariables)
								{
									RendererOptions.Emplace(STRING_EmitterDI + TEXT(".") + VariableName + TEXT(".") + RendererableVariable.GetName().ToString());
								}
							}
						}
					}
				}
			}
		);
	}

	return RendererOptions;
}
