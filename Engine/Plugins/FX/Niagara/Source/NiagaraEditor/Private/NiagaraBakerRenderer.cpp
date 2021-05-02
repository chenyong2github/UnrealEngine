// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerRenderer.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComputeExecutionContext.h"

#include "Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h"
#include "Niagara/Classes/NiagaraDataInterfaceGrid2DCollection.h"

#include "Modules/ModuleManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetToolsModule.h"
#include "BufferVisualizationData.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "PackageTools.h"

namespace NiagaraBakerRendererLocal
{
	const FString STRING_BufferVisualization("BufferVisualization");
	const FString STRING_EmitterDI("EmitterDI");
	const FString STRING_EmitterParticles("EmitterParticles");
}

FNiagaraBakerRenderer::FNiagaraBakerRenderer(UNiagaraComponent* InPreviewComponent, float InWorldTime)
	: PreviewComponent(InPreviewComponent)
	, WorldTime(InWorldTime)
{
	if ( PreviewComponent && PreviewComponent->GetAsset() )
	{
		BakerSettings = PreviewComponent->GetAsset()->GetBakerSettings();
	}
}

FNiagaraBakerRenderer::FNiagaraBakerRenderer(UNiagaraComponent* InPreviewComponent, UNiagaraBakerSettings* InBakerSettings, float InWorldTime)
	: PreviewComponent(InPreviewComponent)
	, BakerSettings(InBakerSettings)
	, WorldTime(InWorldTime)
{
}

bool FNiagaraBakerRenderer::IsValid() const
{
	return PreviewComponent && BakerSettings;
}

bool FNiagaraBakerRenderer::RenderView(UTextureRenderTarget2D* RenderTarget, int32 iOutputTextureIndex) const
{
	if (!IsValid())
	{
		return false;
	}

	UWorld* World = PreviewComponent->GetWorld();
	if ( (World == nullptr) || (World->Scene == nullptr) )
	{
		return false;
	}

	FCanvas Canvas(RenderTarget->GameThread_GetRenderTargetResource(), nullptr, WorldTime, FApp::GetDeltaTime(), WorldTime, World->Scene->GetFeatureLevel());
	Canvas.Clear(FLinearColor::Transparent);

	bool bRendered = false;
	if (BakerSettings->OutputTextures.IsValidIndex(iOutputTextureIndex))
	{
		const FNiagaraBakerTextureSettings& OutputTextureSettings = BakerSettings->OutputTextures[iOutputTextureIndex];
		const FIntRect ViewRect = FIntRect(0, 0, OutputTextureSettings.FrameSize.X, OutputTextureSettings.FrameSize.Y);
		bRendered = RenderView(RenderTarget, &Canvas, iOutputTextureIndex, ViewRect);
	}
	Canvas.Flush_GameThread();
	return bRendered;
}

bool FNiagaraBakerRenderer::RenderView(UTextureRenderTarget2D* RenderTarget, FCanvas* Canvas, int32 iOutputTextureIndex, FIntRect ViewRect) const
{
	using namespace NiagaraBakerRendererLocal;

	if (!IsValid())
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
		// Regular render
		case ERenderType::View:
		{
			// Create View Family
			FSceneViewFamilyContext ViewFamily(
				FSceneViewFamily::ConstructionValues(RenderTarget->GameThread_GetRenderTargetResource(), World->Scene, FEngineShowFlags(ESFIM_Game))
				.SetWorldTimes(WorldTime, FApp::GetDeltaTime(), WorldTime)
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
			if (BakerSettings->bRenderComponentOnly)
			{
				ViewInitOptions.ShowOnlyPrimitives.Emplace();
				ViewInitOptions.ShowOnlyPrimitives->Add(PreviewComponent->ComponentId);
			}

			FSceneView* NewView = new FSceneView(ViewInitOptions);
			NewView->CurrentBufferVisualizationMode = SourceName;
			ViewFamily.Views.Add(NewView);

			ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f, false));

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
				FNiagaraSystemInstance* SystemInstance = PreviewComponent->GetSystemInstance();
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

			FNiagaraSystemInstance* SystemInstance = PreviewComponent->GetSystemInstance();
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

FNiagaraBakerRenderer::ERenderType FNiagaraBakerRenderer::GetRenderType(FName SourceName, FName& OutName)
{
	using namespace NiagaraBakerRendererLocal;

	OutName = FName();
	if (SourceName.IsNone())
	{
		return ERenderType::View;
	}

	FString SourceBindingString = SourceName.ToString();
	TArray<FString> SplitNames;
	SourceBindingString.ParseIntoArray(SplitNames, TEXT("."));

	if (!ensure(SplitNames.Num() > 0))
	{
		return ERenderType::None;
	}

	// Buffer Visualization Mode
	if (SplitNames[0] == STRING_BufferVisualization)
	{
		if (!ensure(SplitNames.Num() == 2))
		{
			return ERenderType::None;
		}
		OutName = FName(SplitNames[1]);
		return ERenderType::View;
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
