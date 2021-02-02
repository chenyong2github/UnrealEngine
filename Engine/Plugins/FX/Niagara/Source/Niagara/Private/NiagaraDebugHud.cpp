// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugHud.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDataSetDebugAccessor.h"

#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Components/LineBatchComponent.h"
#include "DrawDebugHelpers.h"

namespace NiagaraDebugLocal
{
	enum class EEngineVariables : uint8
	{
		LODDistance,
		LODFraction,
		Num
	};

	static FString GEngineVariableStrings[(int)EEngineVariables::Num] =
	{
		TEXT("Engine.LODDistance"),
		TEXT("Engine.LODFraction"),
	};

	struct FCachedVariables
	{
		~FCachedVariables()
		{
#if WITH_EDITORONLY_DATA
			if ( UNiagaraSystem* NiagaraSystem = WeakNiagaraSystem.Get() )
			{
				NiagaraSystem->OnSystemCompiled().Remove(CompiledDelegate);
			}
#endif
		}

		TWeakObjectPtr<UNiagaraSystem> WeakNiagaraSystem;
#if WITH_EDITORONLY_DATA
		FDelegateHandle CompiledDelegate;
#endif

		bool bShowEngineVariable[(int)EEngineVariables::Num] = {};				// Engine variables that are not contained within the store

		TArray<FNiagaraDataSetDebugAccessor> SystemVariables;					// System & Emitter variables since both are inside the same DataBuffer
		TArray<FNiagaraVariableWithOffset> UserVariables;						// Exposed user parameters which will pull from the component

		TArray<TArray<FNiagaraDataSetDebugAccessor>> ParticleVariables;			// Per Emitter Particle variables
		TArray<FNiagaraDataSetAccessor<FVector>> ParticlePositionAccessors;		// Only valid if we have particle attributes
	};

	static ENiagaraDebugHudSystemVerbosity GHudVerbosity = ENiagaraDebugHudSystemVerbosity::Minimal;
	static bool GGpuReadbackEnabled = false;
	static FVector2D GDisplayLocation = FVector2D(30.0f, 150.0f);
	static ENiagaraDebugHudSystemVerbosity GSystemVerbosity = ENiagaraDebugHudSystemVerbosity::Minimal;
	static bool GSystemShowBounds = false;
	static bool GSystemShowActiveOnlyInWorld = true;
	static FString GSystemFilter;
	static FString GEmitterFilter;
	static FString GActorFilter;
	static FString GComponentFilter;
	static TMap<TWeakObjectPtr<UNiagaraSystem>, FCachedVariables> GCachedSystemVariables;
	static TArray<FString> GSystemVariables;

	static TArray<FString> GParticleVariables;
	static uint32 GMaxParticlesToDisplay = 32;
	static bool GShowParticlesInWorld = true;

	static FDelegateHandle	GDebugDrawHandle;
	static int32			GDebugDrawHandleUsers = 0;

	static FAutoConsoleCommandWithWorldAndArgs CmdDebugHud(
		TEXT("fx.Niagara.Debug.Hud"),
		TEXT("Set options for debug hud display"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld*)
			{
				if ( Args.Num() > 0 )
				{
					for (FString Arg : Args)
					{
						if (Arg.RemoveFromStart(TEXT("HudVerbosity=")))
						{
							GHudVerbosity = FMath::Clamp(ENiagaraDebugHudSystemVerbosity(FCString::Atoi(*Arg)), ENiagaraDebugHudSystemVerbosity::None, ENiagaraDebugHudSystemVerbosity::Verbose);
						}
						else if (Arg.RemoveFromStart(TEXT("GpuReadback=")))
						{
							GGpuReadbackEnabled = FCString::Atoi(*Arg) != 0;
						}
						else if (Arg.RemoveFromStart(TEXT("DisplayLocation=")))
						{
							TArray<FString> Values;
							Arg.ParseIntoArray(Values, TEXT(","));
							if ( Values.Num() > 0 )
							{
								GDisplayLocation.X = FCString::Atof(*Values[0]);
								if (Values.Num() > 1)
								{
									GDisplayLocation.Y = FCString::Atof(*Values[1]);
								}
							}
						}
						else if (Arg.RemoveFromStart(TEXT("SystemVerbosity=")))
						{
							GSystemVerbosity = FMath::Clamp(ENiagaraDebugHudSystemVerbosity(FCString::Atoi(*Arg)), ENiagaraDebugHudSystemVerbosity::None, ENiagaraDebugHudSystemVerbosity::Verbose);
						}
						else if (Arg.RemoveFromStart(TEXT("SystemShowBounds=")))
						{
							GSystemShowBounds = FCString::Atoi(*Arg) != 0;
						}
						else if (Arg.RemoveFromStart(TEXT("SystemShowActiveOnlyInWorld=")))
						{
							GSystemShowActiveOnlyInWorld = FCString::Atoi(*Arg) != 0;
						}
						else if (Arg.RemoveFromStart(TEXT("SystemFilter=")))
						{
							GSystemFilter = Arg;
						}
						else if (Arg.RemoveFromStart(TEXT("EmitterFilter=")))
						{
							GEmitterFilter = Arg;
							GCachedSystemVariables.Empty();
						}
						else if (Arg.RemoveFromStart(TEXT("ActorFilter=")))
						{
							GActorFilter = Arg;
						}
						else if (Arg.RemoveFromStart(TEXT("ComponentFilter=")))
						{
							GComponentFilter = Arg;
						}
						else if (Arg.RemoveFromStart(TEXT("SystemVariables=")))
						{
							Arg.ParseIntoArray(GSystemVariables, TEXT(","));
							GCachedSystemVariables.Empty();
						}
						else if (Arg.RemoveFromStart(TEXT("ParticleVariables=")))
						{
							Arg.ParseIntoArray(GParticleVariables, TEXT(","));
							GCachedSystemVariables.Empty();
						}
						else if (Arg.RemoveFromStart(TEXT("MaxParticlesToDisplay=")))
						{
							GMaxParticlesToDisplay = FMath::Max(FCString::Atoi(*Arg), 1);
						}
						else if (Arg.RemoveFromStart(TEXT("ShowParticlesInWorld=")))
						{
							GShowParticlesInWorld = FCString::Atoi(*Arg) != 0;
						}
					}
				}
			}
		)
	);


	template<typename TVariableList, typename TPredicate>
	void FindVariablesByWildcard(const TVariableList& Variables, const TArray<FString>& Wildcards, TPredicate Predicate)
	{
		if (Wildcards.Num() == 0)
		{
			return;
		}

		for (const auto& Variable : Variables)
		{
			const FString VariableName = Variable.GetName().ToString();
			for ( const FString& Wildcard : Wildcards )
			{
				if ((Wildcard.Len() > 0) && VariableName.MatchesWildcard(Wildcard) )
				{
					Predicate(Variable);
					break;
				}
			}
		}
	}

	const FCachedVariables& GetCachedVariables(UNiagaraSystem* NiagaraSystem)
	{
		FCachedVariables* CachedVariables = GCachedSystemVariables.Find(NiagaraSystem);
		if (CachedVariables == nullptr)
		{
			CachedVariables = &GCachedSystemVariables.Emplace(NiagaraSystem);
			CachedVariables->WeakNiagaraSystem = MakeWeakObjectPtr(NiagaraSystem);
#if WITH_EDITORONLY_DATA
			CachedVariables->CompiledDelegate = NiagaraSystem->OnSystemCompiled().AddLambda([](UNiagaraSystem* NiagaraSystem) { GCachedSystemVariables.Remove(NiagaraSystem); });
#endif

			if (GSystemVariables.Num() > 0)
			{
				const FNiagaraDataSetCompiledData& SystemCompiledData = NiagaraSystem->GetSystemCompiledData().DataSetCompiledData;
				FindVariablesByWildcard(
					SystemCompiledData.Variables,
					GSystemVariables,
					[&](const FNiagaraVariable& Variable) { CachedVariables->SystemVariables.AddDefaulted_GetRef().Init(SystemCompiledData, Variable.GetName()); }
				);

				FindVariablesByWildcard(
					NiagaraSystem->GetExposedParameters().ReadParameterVariables(),
					GSystemVariables,
					[&](const FNiagaraVariableWithOffset& Variable) { CachedVariables->UserVariables.Add(Variable); }
				);

				for (int32 iVariable=0; iVariable < (int32)EEngineVariables::Num; ++iVariable)
				{
					for ( const FString& Wildcard : GSystemVariables )
					{
						if ( GEngineVariableStrings[iVariable].MatchesWildcard(Wildcard) )
						{
							CachedVariables->bShowEngineVariable[iVariable] = true;
							break;
						}
					}
				}
			}

			if (GParticleVariables.Num() > 0)
			{
				const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& AllEmittersCompiledData = NiagaraSystem->GetEmitterCompiledData();

				CachedVariables->ParticleVariables.AddDefaulted(AllEmittersCompiledData.Num());
				CachedVariables->ParticlePositionAccessors.AddDefaulted(AllEmittersCompiledData.Num());
				for (int32 iEmitter =0; iEmitter < AllEmittersCompiledData.Num(); ++iEmitter)
				{
					const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(iEmitter);
					if ( !EmitterHandle.IsValid() || !EmitterHandle.GetIsEnabled() )
					{
						continue;
					}

					if ( !GEmitterFilter.IsEmpty() && !EmitterHandle.GetUniqueInstanceName().MatchesWildcard(GEmitterFilter) )
					{
						continue;
					}

					const FNiagaraDataSetCompiledData& EmitterCompiledData = AllEmittersCompiledData[iEmitter]->DataSetCompiledData;

					FindVariablesByWildcard(
						EmitterCompiledData.Variables,
						GParticleVariables,
						[&](const FNiagaraVariable& Variable) { CachedVariables->ParticleVariables[iEmitter].AddDefaulted_GetRef().Init(EmitterCompiledData, Variable.GetName()); }
					);

					if ( CachedVariables->ParticleVariables[iEmitter].Num() > 0)
					{
						static const FName PositionName(TEXT("Position"));
						CachedVariables->ParticlePositionAccessors[iEmitter].Init(EmitterCompiledData, PositionName);
					}
				}
			}
		}
		return *CachedVariables;
	}

	FVector2D GetStringSize(UFont* Font, const TCHAR* Text)
	{
		FVector2D MaxSize = FVector2D::ZeroVector;
		FVector2D CurrSize = FVector2D::ZeroVector;

		const float fAdvanceHeight = Font->GetMaxCharHeight();
		const TCHAR* PrevChar = nullptr;
		while (*Text)
		{
			if ( *Text == '\n' )
			{
				CurrSize.X = 0.0f;
				CurrSize.Y = CurrSize.Y + fAdvanceHeight; 
				PrevChar = nullptr;
				++Text;
				continue;
			}
			
			float TmpWidth, TmpHeight;
			Font->GetCharSize(*Text, TmpWidth, TmpHeight);

			int8 CharKerning = 0;
			if (PrevChar)
			{
				CharKerning = Font->GetCharKerning(*PrevChar, *Text);
			}

			CurrSize.X += TmpWidth + CharKerning;
			MaxSize.X = FMath::Max(MaxSize.X, CurrSize.X);
			MaxSize.Y = FMath::Max(MaxSize.Y, CurrSize.Y + TmpHeight);

			PrevChar = Text++;
		}

		return MaxSize;
	}

	void DrawBox(UWorld* World, const FVector& Location, const FVector& Extents, const FLinearColor& Color, float Thickness = 3.0f)
	{
		if (ULineBatchComponent* LineBatcher = World->LineBatcher)
		{
			LineBatcher->DrawLine(Location + FVector( Extents.X,  Extents.Y,  Extents.Z), Location + FVector( Extents.X, -Extents.Y,  Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector( Extents.X, -Extents.Y,  Extents.Z), Location + FVector(-Extents.X, -Extents.Y,  Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X, -Extents.Y,  Extents.Z), Location + FVector(-Extents.X,  Extents.Y,  Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X,  Extents.Y,  Extents.Z), Location + FVector( Extents.X,  Extents.Y,  Extents.Z), Color, 0, Thickness);

			LineBatcher->DrawLine(Location + FVector( Extents.X,  Extents.Y, -Extents.Z), Location + FVector( Extents.X, -Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector( Extents.X, -Extents.Y, -Extents.Z), Location + FVector(-Extents.X, -Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X, -Extents.Y, -Extents.Z), Location + FVector(-Extents.X,  Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X,  Extents.Y, -Extents.Z), Location + FVector( Extents.X,  Extents.Y, -Extents.Z), Color, 0, Thickness);

			LineBatcher->DrawLine(Location + FVector( Extents.X,  Extents.Y,  Extents.Z), Location + FVector( Extents.X,  Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector( Extents.X, -Extents.Y,  Extents.Z), Location + FVector( Extents.X, -Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X, -Extents.Y,  Extents.Z), Location + FVector(-Extents.X, -Extents.Y, -Extents.Z), Color, 0, Thickness);
			LineBatcher->DrawLine(Location + FVector(-Extents.X,  Extents.Y,  Extents.Z), Location + FVector(-Extents.X,  Extents.Y, -Extents.Z), Color, 0, Thickness);
		}
	}

	void DrawSystemLocation(UCanvas* Canvas, bool bIsActive, const FVector& ScreenLocation, const FRotator& Rotation)
	{
		FSceneView* SceneView = Canvas->SceneView;
		FCanvas* DrawCanvas = Canvas->Canvas;
		if (SceneView && DrawCanvas)
		{
			const FMatrix& ViewMatrix = SceneView->ViewMatrices.GetViewMatrix();
			const float AxisLength = 50.0f;
			const float BoxSize = 10.0f;
			const FVector XAxis(ViewMatrix.TransformVector(Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f))));
			const FVector YAxis(ViewMatrix.TransformVector(Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f))));
			const FVector ZAxis(ViewMatrix.TransformVector(Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f))));

			FBatchedElements* BatchedLineElements = DrawCanvas->GetBatchedElements(FCanvas::ET_Line);

			if ( ensure(BatchedLineElements) )
			{
				FHitProxyId HitProxyId = DrawCanvas->GetHitProxyId();
				const FVector ScreenLocation2D(ScreenLocation.X, ScreenLocation.Y, 0.0f);
				const FVector XAxis2D(XAxis.X, -XAxis.Y, 0.0f);
				const FVector YAxis2D(YAxis.X, -YAxis.Y, 0.0f);
				const FVector ZAxis2D(ZAxis.X, -ZAxis.Y, 0.0f);
				BatchedLineElements->AddLine(ScreenLocation2D, ScreenLocation2D + (XAxis2D * AxisLength), bIsActive ? FLinearColor::Red : FLinearColor::Black, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(ScreenLocation2D, ScreenLocation2D + (YAxis2D * AxisLength), bIsActive ? FLinearColor::Green : FLinearColor::Black, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(ScreenLocation2D, ScreenLocation2D + (ZAxis2D * AxisLength), bIsActive ? FLinearColor::Blue : FLinearColor::Black, HitProxyId, 1.0f);

				const FVector BoxPoints[] =
				{
					ScreenLocation2D + ((-XAxis2D - YAxis2D - ZAxis2D) * BoxSize),
					ScreenLocation2D + (( XAxis2D - YAxis2D - ZAxis2D) * BoxSize),
					ScreenLocation2D + (( XAxis2D + YAxis2D - ZAxis2D) * BoxSize),
					ScreenLocation2D + ((-XAxis2D + YAxis2D - ZAxis2D) * BoxSize),
					ScreenLocation2D + ((-XAxis2D - YAxis2D + ZAxis2D) * BoxSize),
					ScreenLocation2D + (( XAxis2D - YAxis2D + ZAxis2D) * BoxSize),
					ScreenLocation2D + (( XAxis2D + YAxis2D + ZAxis2D) * BoxSize),
					ScreenLocation2D + ((-XAxis2D + YAxis2D + ZAxis2D) * BoxSize),
				};
				const FLinearColor BoxColor = bIsActive ? FLinearColor::White : FLinearColor::Black;
				BatchedLineElements->AddLine(BoxPoints[0], BoxPoints[1], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[1], BoxPoints[2], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[2], BoxPoints[3], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[3], BoxPoints[0], BoxColor, HitProxyId, 1.0f);

				BatchedLineElements->AddLine(BoxPoints[4], BoxPoints[5], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[5], BoxPoints[6], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[6], BoxPoints[7], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[7], BoxPoints[4], BoxColor, HitProxyId, 1.0f);

				BatchedLineElements->AddLine(BoxPoints[0], BoxPoints[4], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[1], BoxPoints[5], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[2], BoxPoints[6], BoxColor, HitProxyId, 1.0f);
				BatchedLineElements->AddLine(BoxPoints[3], BoxPoints[7], BoxColor, HitProxyId, 1.0f);
			}
		}
	}
}

FNiagaraDebugHud::FNiagaraDebugHud(UWorld* World)
{
	using namespace NiagaraDebugLocal;

	WeakWorld = World;

	if ( !GDebugDrawHandle.IsValid() )
	{
		GDebugDrawHandle = UDebugDrawService::Register(TEXT("Particles"), FDebugDrawDelegate::CreateStatic(&FNiagaraDebugHud::DebugDrawCallback));
	}
	++GDebugDrawHandleUsers;
}

FNiagaraDebugHud::~FNiagaraDebugHud()
{
	using namespace NiagaraDebugLocal;

	--GDebugDrawHandleUsers;
	if (GDebugDrawHandleUsers == 0)
	{
		UDebugDrawService::Unregister(GDebugDrawHandle);
		GDebugDrawHandle.Reset();
	}
}

void FNiagaraDebugHud::GatherSystemInfo()
{
	using namespace NiagaraDebugLocal;

	GlobalTotalSystems = 0;
	GlobalTotalScalability = 0;
	GlobalTotalEmitters = 0;
	GlobalTotalParticles = 0;
	PerSystemDebugInfo.Reset();
	InWorldComponents.Reset();

	UWorld* World = WeakWorld.Get();
	if (World == nullptr)
	{
		return;
	}

	// When the display is minimal we won't show in world of overview information
	if (GHudVerbosity <= ENiagaraDebugHudSystemVerbosity::Minimal)
	{
		return;
	}

	// Iterate all components looking for active ones in the world we are in
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* NiagaraComponent = *It;
		if (NiagaraComponent->IsPendingKillOrUnreachable() || NiagaraComponent->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
		{
			continue;
		}
		if (NiagaraComponent->GetWorld() != World)
		{
			continue;
		}

		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();
		if (!SystemInstance)
		{
			continue;
		}

		check(NiagaraComponent->GetAsset() != nullptr);

		const bool bIsActive = NiagaraComponent->IsActive();
		const bool bHasScalability = NiagaraComponent->IsRegisteredWithScalabilityManager();
		if (!bIsActive && !bHasScalability )
		{
			continue;
		}

		FSystemDebugInfo& SystemDebugInfo = PerSystemDebugInfo.FindOrAdd(NiagaraComponent->GetAsset()->GetFName());
		if (SystemDebugInfo.SystemName.IsEmpty())
		{
			SystemDebugInfo.SystemName = GetNameSafe(NiagaraComponent->GetAsset());
			SystemDebugInfo.bShowInWorld = !GSystemFilter.IsEmpty() && SystemDebugInfo.SystemName.MatchesWildcard(GSystemFilter);
		}

		if (SystemDebugInfo.bShowInWorld && (bIsActive || !GSystemShowActiveOnlyInWorld))
		{
			bool bIsMatch = true;

			// Filter by actor
			if ( !GActorFilter.IsEmpty() )
			{
				AActor* Actor = NiagaraComponent->GetOwner();
				bIsMatch &= (Actor != nullptr) && Actor->GetName().MatchesWildcard(GActorFilter);
			}

			// Filter by component
			if ( bIsMatch && !GComponentFilter.IsEmpty() )
			{
				bIsMatch &= NiagaraComponent->GetName().MatchesWildcard(GComponentFilter);
			}

			if (bIsMatch)
			{
				InWorldComponents.Add(NiagaraComponent);
			}
		}

		if ( bHasScalability )
		{
			++GlobalTotalScalability;
			++SystemDebugInfo.TotalScalability;
		}

		if (bIsActive)
		{
			// Accumulate totals
			int32 ActiveEmitters = 0;
			int32 TotalEmitters = 0;
			int32 ActiveParticles = 0;

			for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
			{
				UNiagaraEmitter* NiagaraEmitter = EmitterInstance->GetCachedEmitter();
				if (NiagaraEmitter == nullptr)
				{
					continue;
				}

				++TotalEmitters;
				if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Active)
				{
					++ActiveEmitters;
				}
				ActiveParticles += EmitterInstance->GetNumParticles();
			}

			++SystemDebugInfo.TotalSystems;
			SystemDebugInfo.TotalEmitters += ActiveEmitters;
			SystemDebugInfo.TotalParticles += ActiveParticles;

			++GlobalTotalSystems;
			GlobalTotalEmitters += ActiveEmitters;
			GlobalTotalParticles += ActiveParticles;
		}
	}
}

FNiagaraDataSet* FNiagaraDebugHud::GetParticleDataSet(FNiagaraSystemInstance* SystemInstance, FNiagaraEmitterInstance* EmitterInstance, int32 iEmitter)
{
	using namespace NiagaraDebugLocal;

	// For GPU context we need to readback and cache the data
	if (EmitterInstance->GetGPUContext())
	{
#if !UE_BUILD_SHIPPING
		if (!GGpuReadbackEnabled)
		{
			return nullptr;
		}

		FNiagaraComputeExecutionContext* GPUExecContext = EmitterInstance->GetGPUContext();
		FGpuEmitterCache* GpuCachedData = GpuEmitterData.Find(SystemInstance->GetId());
		if ( GpuCachedData == nullptr )
		{
			const int32 NumEmitters = SystemInstance->GetEmitters().Num();
			GpuCachedData = &GpuEmitterData.Emplace(SystemInstance->GetId());
			GpuCachedData->CurrentEmitterData.AddDefaulted(NumEmitters);
			GpuCachedData->PendingEmitterData.AddDefaulted(NumEmitters);
		}
		GpuCachedData->LastAccessedCycles = FPlatformTime::Cycles64();

		// Pending readback complete?
		if (GpuCachedData->PendingEmitterData[iEmitter] && GpuCachedData->PendingEmitterData[iEmitter]->bWritten)
		{
			GpuCachedData->CurrentEmitterData[iEmitter] = GpuCachedData->PendingEmitterData[iEmitter];
			GpuCachedData->PendingEmitterData[iEmitter] = nullptr;
		}

		// Enqueue a readback?
		if ( GpuCachedData->PendingEmitterData[iEmitter] == nullptr )
		{
			const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& AllEmittersCompiledData = SystemInstance->GetSystem()->GetEmitterCompiledData();

			GpuCachedData->PendingEmitterData[iEmitter] = MakeShared<FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>();
			GpuCachedData->PendingEmitterData[iEmitter]->Parameters = GPUExecContext->CombinedParamStore;
			GpuCachedData->PendingEmitterData[iEmitter]->Frame.Init(&AllEmittersCompiledData[iEmitter]->DataSetCompiledData);

			ENQUEUE_RENDER_COMMAND(NiagaraReadbackGpuSim)(
				[RT_Batcher=SystemInstance->GetBatcher(), RT_InstanceID=SystemInstance->GetId(), RT_DebugInfo=GpuCachedData->PendingEmitterData[iEmitter], RT_Context=GPUExecContext](FRHICommandListImmediate& RHICmdList)
				{
					RT_Batcher->AddDebugReadback(RT_InstanceID, RT_DebugInfo, RT_Context);
				}
			);
		}

		// Pull current data if we have one
		if ( GpuCachedData->CurrentEmitterData[iEmitter] )
		{
			return &GpuCachedData->CurrentEmitterData[iEmitter]->Frame;
		}
#endif
		return nullptr;
	}

	return &EmitterInstance->GetData();
}

void FNiagaraDebugHud::DebugDrawCallback(UCanvas* Canvas, APlayerController* PC)
{
	using namespace NiagaraDebugLocal;

	if (GHudVerbosity == ENiagaraDebugHudSystemVerbosity::None)
	{
		return;
	}

	if (!Canvas || !Canvas->Canvas || !Canvas->SceneView || !Canvas->SceneView->Family || !Canvas->SceneView->Family->Scene)
	{
		return;
	}

	if ( UWorld* World = Canvas->SceneView->Family->Scene->GetWorld())
	{
		if ( FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World) )
		{
			if (FNiagaraDebugHud* DebugHud = WorldManager->GetNiagaraDebugHud())
			{
				DebugHud->Draw(WorldManager, Canvas, PC);
			}
		}
	}
}

void FNiagaraDebugHud::Draw(FNiagaraWorldManager* WorldManager, UCanvas* Canvas, APlayerController* PC)
{
	using namespace NiagaraDebugLocal;

	if (GHudVerbosity == ENiagaraDebugHudSystemVerbosity::None)
	{
		return;
	}

	// Draw in world components
	DrawComponents(WorldManager, Canvas, GEngine->GetTinyFont());

	// Draw overview
	DrawOverview(WorldManager, Canvas->Canvas, GEngine->GetSmallFont());

	// Scrub any gpu cached emitters we haven't used in a while
	{
		static double ScrubDurationSeconds = 1.0;
		const uint64 ScrubDurationCycles = uint64(ScrubDurationSeconds / FPlatformTime::GetSecondsPerCycle64());
		const uint64 ScrubCycles = FPlatformTime::Cycles64() - ScrubDurationCycles;

		for ( auto it=GpuEmitterData.CreateIterator(); it; ++it )
		{
			const FGpuEmitterCache& CachedData = it.Value();
			if ( CachedData.LastAccessedCycles < ScrubCycles )
			{
				it.RemoveCurrent();
			}
		}
	}
}

void FNiagaraDebugHud::DrawOverview(class FNiagaraWorldManager* WorldManager, FCanvas* DrawCanvas, UFont* Font)
{
	using namespace NiagaraDebugLocal;

	const float fAdvanceHeight = Font->GetMaxCharHeight() + 1.0f;

	const FLinearColor HeadingColor = FLinearColor::Green;
	const FLinearColor DetailColor = FLinearColor::White;
	const FLinearColor DetailHighlightColor = FLinearColor::Yellow;

	const FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

	FVector2D TextLocation = GDisplayLocation;

	// Display overview
	const bool bShowSystemInformation = GHudVerbosity > ENiagaraDebugHudSystemVerbosity::Minimal;
	{
		TStringBuilder<1024> OverviewString;
		{
			static const auto CVarGlobalLoopTime = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.Niagara.Debug.GlobalLoopTime"));

			const TCHAR* Separator = TEXT("    ");
			const ENiagaraDebugPlaybackMode PlaybackMode = WorldManager->GetDebugPlaybackMode();
			const float PlaybackRate = WorldManager->GetDebugPlaybackRate();
			const float PlaybackLoopTime = CVarGlobalLoopTime ? CVarGlobalLoopTime->GetFloat() : 0.0f;

			bool bRequiresNewline = false;
			if (PlaybackMode != ENiagaraDebugPlaybackMode::Play)
			{
				bRequiresNewline = true;
				OverviewString.Append(TEXT("PlaybackMode: "));
				switch (WorldManager->GetDebugPlaybackMode())
				{
					case ENiagaraDebugPlaybackMode::Loop:	OverviewString.Append(TEXT("Looping")); break;
					case ENiagaraDebugPlaybackMode::Paused:	OverviewString.Append(TEXT("Paused")); break;
					case ENiagaraDebugPlaybackMode::Step:	OverviewString.Append(TEXT("Step")); break;
					default:								OverviewString.Append(TEXT("Unknown")); break;
				}
				OverviewString.Append(Separator);
			}
			if (!FMath::IsNearlyEqual(PlaybackRate, 1.0f))
			{
				bRequiresNewline = true;
				OverviewString.Appendf(TEXT("PlaybackRate: %.4f"), PlaybackRate);
				OverviewString.Append(Separator);
			}
			if (!FMath::IsNearlyEqual(PlaybackLoopTime, 0.0f))
			{
				bRequiresNewline = true;
				OverviewString.Appendf(TEXT("LoopTime: %.2f"), PlaybackLoopTime);
				OverviewString.Append(Separator);
			}
			if (bRequiresNewline)
			{
				bRequiresNewline = false;
				OverviewString.Append(TEXT("\n"));
			}

			// Display any filters we may have
			if (bShowSystemInformation && (!GSystemFilter.IsEmpty() || !GEmitterFilter.IsEmpty() || !GActorFilter.IsEmpty() || !GComponentFilter.IsEmpty()) )
			{
				if (!GSystemFilter.IsEmpty())
				{
					OverviewString.Appendf(TEXT("SystemFilter: %s"), *GSystemFilter);
					OverviewString.Append(Separator);
				}
				if (!GEmitterFilter.IsEmpty())
				{
					OverviewString.Appendf(TEXT("EmitterFilter: %s"), *GEmitterFilter);
					OverviewString.Append(Separator);
				}
				if (!GActorFilter.IsEmpty())
				{
					OverviewString.Appendf(TEXT("ActorFilter: %s"), *GActorFilter);
					OverviewString.Append(Separator);
				}
				if (!GComponentFilter.IsEmpty())
				{
					OverviewString.Appendf(TEXT("ComponentFilter: %s"), *GComponentFilter);
					OverviewString.Append(Separator);
				}
			}
		}

		if (bShowSystemInformation || OverviewString.Len() > 0)
		{
			static const float ColumnOffset[] = { 0, 150, 300, 450 };
			static const float GuessWidth = 600.0f;

			const bool bShowGlobalInformation = GHudVerbosity > ENiagaraDebugHudSystemVerbosity::Minimal;
			const int32 NumLines = 1 + (bShowGlobalInformation ? 1 : 0);
			const FVector2D OverviewStringSize = GetStringSize(Font, OverviewString.ToString());
			const FVector2D ActualSize(FMath::Max(OverviewStringSize.X, GuessWidth), (NumLines*fAdvanceHeight) + OverviewStringSize.Y);

			// Draw background
			DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, ActualSize.X + 2.0f, ActualSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

			// Draw string
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Niagara DebugHud"), Font, HeadingColor);
			TextLocation.Y += fAdvanceHeight;
			if (OverviewString.Len() > 0)
			{
				DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, OverviewString.ToString(), Font, HeadingColor);
				TextLocation.Y += OverviewStringSize.Y;
			}

			// Display global system information
			if (bShowGlobalInformation)
			{
				static const TCHAR* HeadingText[] = { TEXT("TotalSystems:"), TEXT("TotalScalability:"), TEXT("TotalEmitters:") , TEXT("TotalParticles:") };
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[0], TextLocation.Y, HeadingText[0], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[1], TextLocation.Y, HeadingText[1], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[2], TextLocation.Y, HeadingText[2], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[3], TextLocation.Y, HeadingText[3], Font, HeadingColor);

				static const float DetailOffset[] =
				{
					ColumnOffset[0] + Font->GetStringSize(HeadingText[0]) + 5.0f,
					ColumnOffset[1] + Font->GetStringSize(HeadingText[1]) + 5.0f,
					ColumnOffset[2] + Font->GetStringSize(HeadingText[2]) + 5.0f,
					ColumnOffset[3] + Font->GetStringSize(HeadingText[3]) + 5.0f,
				};

				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[0], TextLocation.Y, *FString::FromInt(GlobalTotalSystems), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[1], TextLocation.Y, *FString::FromInt(GlobalTotalScalability), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[2], TextLocation.Y, *FString::FromInt(GlobalTotalEmitters), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[3], TextLocation.Y, *FString::FromInt(GlobalTotalParticles), Font, DetailColor);

				TextLocation.Y += fAdvanceHeight;
			}
		}
	}

	// Nothing else to show
	if (!bShowSystemInformation)
	{
		return;
	}

	// Display active systems information
	{
		TextLocation.Y += fAdvanceHeight;

		static float ColumnOffset[] = { 0, 300, 400, 500, 600 };
		static float GuessWidth = 700.0f;

		const uint32 NumLines = 1 + PerSystemDebugInfo.Num();
		DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, GuessWidth + 1.0f, 2.0f + (float(NumLines) * fAdvanceHeight), 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[0], TextLocation.Y, TEXT("System Name"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[1], TextLocation.Y, TEXT("# Active"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[2], TextLocation.Y, TEXT("# Scalability"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[3], TextLocation.Y, TEXT("# Emitters"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[4], TextLocation.Y, TEXT("# Particles"), Font, HeadingColor);
		TextLocation.Y += fAdvanceHeight;
		for (auto it = PerSystemDebugInfo.CreateConstIterator(); it; ++it)
		{
			const auto& SystemInfo = it->Value;
			const FLinearColor RowColor = SystemInfo.bShowInWorld ? DetailHighlightColor : DetailColor;

			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[0], TextLocation.Y, *SystemInfo.SystemName, Font, RowColor);
			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[1], TextLocation.Y, *FString::FromInt(SystemInfo.TotalSystems), Font, RowColor);
			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[2], TextLocation.Y, *FString::FromInt(SystemInfo.TotalScalability), Font, RowColor);
			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[3], TextLocation.Y, *FString::FromInt(SystemInfo.TotalEmitters), Font, RowColor);
			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[4], TextLocation.Y, *FString::FromInt(SystemInfo.TotalParticles), Font, RowColor);
			TextLocation.Y += fAdvanceHeight;
		}
	}
}

void FNiagaraDebugHud::DrawComponents(FNiagaraWorldManager* WorldManager, UCanvas* Canvas, UFont* Font)
{
	using namespace NiagaraDebugLocal;

	const FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

	FCanvas* DrawCanvas = Canvas->Canvas;
	UWorld* World = WorldManager->GetWorld();

	// Draw in world components
	UEnum* ExecutionStateEnum = StaticEnum<ENiagaraExecutionState>();
	UEnum* PoolingMethodEnum = StaticEnum<ENCPoolMethod>();
	for (TWeakObjectPtr<UNiagaraComponent> WeakComponent : InWorldComponents)
	{
		UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
		if (NiagaraComponent == nullptr)
		{
			continue;
		}

		UNiagaraSystem* NiagaraSystem = NiagaraComponent->GetAsset();
		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();
		if ((NiagaraSystem == nullptr) || (SystemInstance == nullptr))
		{
			continue;
		}

		const FVector ComponentLocation = NiagaraComponent->GetComponentLocation();
		const FRotator ComponentRotation = NiagaraComponent->GetComponentRotation();
		const bool bIsActive = NiagaraComponent->IsActive();

		// Show system bounds (only active components)
		if (GSystemShowBounds && bIsActive)
		{
			const FBox Bounds = NiagaraComponent->CalcBounds(NiagaraComponent->GetComponentTransform()).GetBox();
			if (Bounds.IsValid)
			{
				DrawBox(World, Bounds.GetCenter(), Bounds.GetExtent(), FColor::Red);
			}
		}

		// Get system simulation
		auto SystemSimulation = NiagaraComponent->GetSystemSimulation();
		const bool bSystemSimulationValid = SystemSimulation.IsValid() && SystemSimulation->IsValid();
		if (bSystemSimulationValid)
		{
			SystemSimulation->WaitForInstancesTickComplete();
		}

		// Show particle data in world
		if (GShowParticlesInWorld && bSystemSimulationValid)
		{
			const FCachedVariables& CachedVariables = GetCachedVariables(NiagaraSystem);
			for (int32 iEmitter = 0; iEmitter < CachedVariables.ParticleVariables.Num(); ++iEmitter)
			{
				if ((CachedVariables.ParticleVariables[iEmitter].Num() == 0) || !CachedVariables.ParticlePositionAccessors[iEmitter].IsValid())
				{
					continue;
				}

				FNiagaraEmitterInstance* EmitterInstance = &SystemInstance->GetEmitters()[iEmitter].Get();
				FNiagaraDataSet* ParticleDataSet = GetParticleDataSet(SystemInstance, EmitterInstance, iEmitter);
				if (ParticleDataSet == nullptr)
				{
					continue;
				}

				FNiagaraDataBuffer* DataBuffer = ParticleDataSet->GetCurrentData();
				if (!DataBuffer || !DataBuffer->GetNumInstances())
				{
					continue;
				}

				// No positions accessor, we can't show this in world
				auto PositionReader = CachedVariables.ParticlePositionAccessors[iEmitter].GetReader(*ParticleDataSet);
				if (!PositionReader.IsValid())
				{
					continue;
				}

				//StringBuilder.Appendf(TEXT("Emitter (%s)\n"), *EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName());
				const uint32 NumParticles = FMath::Min(GMaxParticlesToDisplay, DataBuffer->GetNumInstances());
				for (uint32 iInstance = 0; iInstance < NumParticles; ++iInstance)
				{
					const FVector ParticleWorldPosition = PositionReader.Get(iInstance);
					const FVector ParticleScreenLocation = Canvas->Project(ParticleWorldPosition);
					if (!FMath::IsNearlyZero(ParticleScreenLocation.Z))
					{
						TStringBuilder<1024> StringBuilder;
						StringBuilder.Appendf(TEXT("Particle(%u) "), iInstance);
						for (const auto& ParticleVariable : CachedVariables.ParticleVariables[iEmitter])
						{
							StringBuilder.Append(ParticleVariable.GetName().ToString());
							StringBuilder.Append(TEXT("("));
							ParticleVariable.StringAppend(StringBuilder, DataBuffer, iInstance);
							StringBuilder.Append(TEXT(") "));
						}

						const TCHAR* FinalString = StringBuilder.ToString();
						const FVector2D StringSize = GetStringSize(Font, FinalString);
						DrawCanvas->DrawTile(ParticleScreenLocation.X - 1.0f, ParticleScreenLocation.Y - 1.0f, StringSize.X + 2.0f, StringSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
						DrawCanvas->DrawShadowedString(ParticleScreenLocation.X, ParticleScreenLocation.Y, FinalString, Font, FLinearColor::White);
					}
				}
			}
		}

		const FVector ScreenLocation = Canvas->Project(ComponentLocation);
		if (!FMath::IsNearlyZero(ScreenLocation.Z))
		{
			// Show locator
			DrawSystemLocation(Canvas, bIsActive, ScreenLocation, ComponentRotation);

			// Show system text
			if ( GSystemVerbosity > ENiagaraDebugHudSystemVerbosity::None )
			{
				AActor* OwnerActor = NiagaraComponent->GetOwner();

				TStringBuilder<1024> StringBuilder;

				StringBuilder.Appendf(TEXT("System - %s\n"), *GetNameSafe(NiagaraSystem));

				// Build component name
				StringBuilder.Append(TEXT("Component - "));
				if ( OwnerActor )
				{
					StringBuilder.Append(*GetNameSafe(OwnerActor));
					StringBuilder.Append(TEXT("/"));
				}
				StringBuilder.Append(*GetNameSafe(NiagaraComponent));
				StringBuilder.Append(TEXT("\n"));

				if (GSystemVerbosity == ENiagaraDebugHudSystemVerbosity::Verbose)
				{
					StringBuilder.Appendf(TEXT("System ActualState %s - RequestedState %s\n"), *ExecutionStateEnum->GetNameStringByIndex((int32)SystemInstance->GetActualExecutionState()), *ExecutionStateEnum->GetNameStringByIndex((int32)SystemInstance->GetRequestedExecutionState()));
					if (NiagaraComponent->PoolingMethod != ENCPoolMethod::None)
					{
						StringBuilder.Appendf(TEXT("Pooled - %s\n"), *PoolingMethodEnum->GetNameStringByIndex((int32)NiagaraComponent->PoolingMethod));
					}
					if (bIsActive && NiagaraComponent->IsRegisteredWithScalabilityManager())
					{
						StringBuilder.Appendf(TEXT("Scalability - %s\n"), *GetNameSafe(NiagaraSystem->GetEffectType()));
					}
				}

				if (bIsActive)
				{
					int32 ActiveEmitters = 0;
					int32 TotalEmitters = 0;
					int32 ActiveParticles = 0;
					for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
					{
						UNiagaraEmitter* NiagaraEmitter = EmitterInstance->GetCachedEmitter();
						if (NiagaraEmitter == nullptr)
						{
							continue;
						}

						++TotalEmitters;
						if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Active)
						{
							++ActiveEmitters;
						}
						ActiveParticles += EmitterInstance->GetNumParticles();

						if (GSystemVerbosity == ENiagaraDebugHudSystemVerbosity::Verbose)
						{
							if ( EmitterInstance->GetGPUContext() )
							{
								StringBuilder.Appendf(TEXT("Emitter(GPU) %s - State %s - Particles %d\n"), *NiagaraEmitter->GetUniqueEmitterName(), *ExecutionStateEnum->GetNameStringByIndex((int32)EmitterInstance->GetExecutionState()), EmitterInstance->GetNumParticles());
							}
							else
							{
								StringBuilder.Appendf(TEXT("Emitter %s - State %s - Particles %d\n"), *NiagaraEmitter->GetUniqueEmitterName(), *ExecutionStateEnum->GetNameStringByIndex((int32)EmitterInstance->GetExecutionState()), EmitterInstance->GetNumParticles());
							}
						}
					}

					if (GSystemVerbosity == ENiagaraDebugHudSystemVerbosity::Basic)
					{
						StringBuilder.Appendf(TEXT("Emitters - %d / %d\n"), ActiveEmitters, TotalEmitters);
						StringBuilder.Appendf(TEXT("Particles - %d\n"), ActiveParticles);
					}

					// Any variables to display?
					if (bSystemSimulationValid)
					{
						const FCachedVariables& CachedVariables = GetCachedVariables(NiagaraSystem);

						// Engine Variables
						if (CachedVariables.bShowEngineVariable[(int)EEngineVariables::LODDistance])
						{
							StringBuilder.Appendf(TEXT("%s = %.2f\n"), *GEngineVariableStrings[(int)EEngineVariables::LODDistance], SystemInstance->GetLODDistance());
						}
						if (CachedVariables.bShowEngineVariable[(int)EEngineVariables::LODFraction])
						{
							StringBuilder.Appendf(TEXT("%s = %.2f\n"), *GEngineVariableStrings[(int)EEngineVariables::LODFraction], SystemInstance->GetLODDistance() / SystemInstance->GetMaxLODDistance());
						}

						// System variables
						if (CachedVariables.SystemVariables.Num() > 0)
						{
							FNiagaraDataBuffer* DataBuffer = SystemSimulation->MainDataSet.GetCurrentData();
							const uint32 InstanceIndex = SystemInstance->GetSystemInstanceIndex();

							for (const auto& SystemVariable : CachedVariables.SystemVariables)
							{
								StringBuilder.Append(SystemVariable.GetName().ToString());
								StringBuilder.Append(TEXT(" = "));
								SystemVariable.StringAppend(StringBuilder, DataBuffer, InstanceIndex);
								StringBuilder.Append(TEXT("\n"));
							}
						}

						// User variables
						if (CachedVariables.UserVariables.Num() > 0)
						{
							if (FNiagaraUserRedirectionParameterStore* ParameterStore = SystemInstance->GetOverrideParameters())
							{
								for (const FNiagaraVariableWithOffset& UserVariable : CachedVariables.UserVariables)
								{
									if (UserVariable.IsDataInterface())
									{
										StringBuilder.Appendf(TEXT("%s(%s %s)\n"), *UserVariable.GetName().ToString(), *GetNameSafe(UserVariable.GetType().GetClass()) , *GetNameSafe(ParameterStore->GetDataInterfaces()[UserVariable.Offset]));
									}
									else if (UserVariable.IsUObject())
									{
										StringBuilder.Appendf(TEXT("%s(%s %s)\n"), *UserVariable.GetName().ToString(), *GetNameSafe(UserVariable.GetType().GetClass()) , *GetNameSafe(ParameterStore->GetUObjects()[UserVariable.Offset]));
									}
									else
									{
										FNiagaraVariable UserVariableWithValue(UserVariable);
										if ( const uint8* ParameterData = ParameterStore->GetParameterData(UserVariableWithValue) )
										{
											UserVariableWithValue.SetData(ParameterData);
										}
										StringBuilder.Append(*UserVariableWithValue.ToString());
										StringBuilder.Append(TEXT("\n"));
									}
								}
							}
						}

						// Append particle data if we don't show them in world
						if (GShowParticlesInWorld == false)
						{
							for (int32 iEmitter = 0; iEmitter < CachedVariables.ParticleVariables.Num(); ++iEmitter)
							{
								if (CachedVariables.ParticleVariables[iEmitter].Num() == 0)
								{
									continue;
								}

								FNiagaraEmitterInstance* EmitterInstance = &SystemInstance->GetEmitters()[iEmitter].Get();
								FNiagaraDataSet* ParticleDataSet = GetParticleDataSet(SystemInstance, EmitterInstance, iEmitter);
								if (ParticleDataSet == nullptr)
								{
									continue;
								}

								FNiagaraDataBuffer* DataBuffer = ParticleDataSet->GetCurrentData();
								if (!DataBuffer || !DataBuffer->GetNumInstances())
								{
									continue;
								}

								StringBuilder.Appendf(TEXT("Emitter (%s)\n"), *EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName());
								const uint32 NumParticles = FMath::Min(GMaxParticlesToDisplay, DataBuffer->GetNumInstances());
								for (uint32 iInstance = 0; iInstance < NumParticles; ++iInstance)
								{
									StringBuilder.Appendf(TEXT(" Particle(%u) "), iInstance);
									for (const auto& ParticleVariable : CachedVariables.ParticleVariables[iEmitter])
									{
										StringBuilder.Append(ParticleVariable.GetName().ToString());
										StringBuilder.Append(TEXT("("));
										ParticleVariable.StringAppend(StringBuilder, DataBuffer, iInstance);
										StringBuilder.Append(TEXT(") "));
									}
									StringBuilder.Append(TEXT("\n"));
								}

								if (NumParticles < DataBuffer->GetNumInstances())
								{
									StringBuilder.Appendf(TEXT(" ...Truncated"));
								}
							}
						}
					}
				}
				else
				{
					//-TODO: Put a reason why here (either grab from manager or push from manager)
					//FNiagaraScalabilityState* ScalabilityState = SystemInstance->GetWorldManager()->GetScalabilityState(NiagaraComponent);
					if (GSystemVerbosity >= ENiagaraDebugHudSystemVerbosity::Basic)
					{
						StringBuilder.Appendf(TEXT("Deactivated by Scalability - %s "), *GetNameSafe(NiagaraSystem->GetEffectType()));
						if (GSystemVerbosity >= ENiagaraDebugHudSystemVerbosity::Verbose)
						{
							FNiagaraScalabilityState ScalabilityState;
							if (WorldManager->GetScalabilityState(NiagaraComponent, ScalabilityState))
							{
								StringBuilder.Appendf(TEXT("- Significance(%.2f)"), ScalabilityState.Significance);
#if DEBUG_SCALABILITY_STATE
								if (ScalabilityState.bCulledByDistance)
								{
									StringBuilder.Append(TEXT(" DistanceCulled"));
								}
								if (ScalabilityState.bCulledByInstanceCount)
								{
									StringBuilder.Append(TEXT(" InstanceCulled"));
								}
								if (ScalabilityState.bCulledByVisibility)
								{
									StringBuilder.Append(TEXT(" VisibilityCulled"));
								}
#endif
								StringBuilder.Append(TEXT("\n"));
							}
							else
							{
								StringBuilder.Appendf(TEXT("- Scalability State Unknown\n"));
							}
						}
					}
				}

				const TCHAR* FinalString = StringBuilder.ToString();
				const FVector2D StringSize = GetStringSize(Font, FinalString);
				DrawCanvas->DrawTile(ScreenLocation.X - 1.0f, ScreenLocation.Y - 1.0f, StringSize.X + 2.0f, StringSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
				DrawCanvas->DrawShadowedString(ScreenLocation.X, ScreenLocation.Y, FinalString, Font, FLinearColor::White);
			}
		}
	}
}
