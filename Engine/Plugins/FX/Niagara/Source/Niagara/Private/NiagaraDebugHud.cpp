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
#include "Particles/FXBudget.h"

namespace NiagaraDebugLocal
{
	const FLinearColor InfoTextColor = FLinearColor::White;
	const FLinearColor WarningTextColor = FLinearColor(0.9f, 0.7f, 0.0, 1.0f);
	const FLinearColor ErrorTextColor = FLinearColor(1.0f, 0.4, 0.3, 1.0f);
	const FLinearColor WorldTextColor = FLinearColor::White;
	const FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

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

	static TMap<TWeakObjectPtr<UNiagaraSystem>, FCachedVariables> GCachedSystemVariables;

	FNiagaraDebugHUDSettingsData Settings;

	static FDelegateHandle	GDebugDrawHandle;
	static int32			GDebugDrawHandleUsers = 0;

	static TTuple<const TCHAR*, const TCHAR*, TFunction<void(FString)>> GDebugConsoleCommands[] =
	{
		// Main HUD commands
		MakeTuple(TEXT("Enabled="), TEXT("Enable or disable the HUD"), [](FString Arg) {Settings.bEnabled = FCString::Atoi(*Arg) != 0; }),
		MakeTuple(TEXT("ValidateSystemSimulationDataBuffers="), TEXT("Enable or disable validation on system data buffers"), [](FString Arg) {Settings.bValidateSystemSimulationDataBuffers = FCString::Atoi(*Arg) != 0; }),
		MakeTuple(TEXT("bValidateParticleDataBuffers="), TEXT("Enable or disable validation on particle data buffers"), [](FString Arg) {Settings.bValidateParticleDataBuffers = FCString::Atoi(*Arg) != 0; }),

		MakeTuple(TEXT("OverviewEnabled="), TEXT("Enable or disable the main overview display"), [](FString Arg) {Settings.bOverviewEnabled = FCString::Atoi(*Arg) != 0; }),

		MakeTuple(TEXT("OverviewLocation="), TEXT("Set the overview location"),
			[](FString Arg)
			{
				TArray<FString> Values;
				Arg.ParseIntoArray(Values, TEXT(","));
				if (Values.Num() > 0)
				{
					Settings.OverviewLocation.X = FCString::Atof(*Values[0]);
					if (Values.Num() > 1)
					{
						Settings.OverviewLocation.Y = FCString::Atof(*Values[1]);
					}
				}
			}
		),
		MakeTuple(TEXT("SystemFilter="), TEXT("Set the system filter"), [](FString Arg) {Settings.SystemFilter = Arg; Settings.bSystemFilterEnabled = !Arg.IsEmpty(); }),
		MakeTuple(TEXT("EmitterFilter="), TEXT("Set the emitter filter"), [](FString Arg) {Settings.EmitterFilter = Arg; Settings.bEmitterFilterEnabled = !Arg.IsEmpty(); GCachedSystemVariables.Empty(); }),
		MakeTuple(TEXT("ActorFilter="), TEXT("Set the actor filter"), [](FString Arg) {Settings.ActorFilter = Arg; Settings.bActorFilterEnabled = !Arg.IsEmpty(); }),
		MakeTuple(TEXT("ComponentFilter="), TEXT("Set the component filter"), [](FString Arg) {Settings.ComponentFilter = Arg; Settings.bComponentFilterEnabled = !Arg.IsEmpty(); }),

		MakeTuple(TEXT("ShowGlobalBudgetInfo="), TEXT("Shows global budget information"), [](FString Arg) {Settings.bShowGlobalBudgetInfo = FCString::Atoi(*Arg) != 0; }),

		// System commands
		MakeTuple(TEXT("SystemShowBounds="), TEXT("Show system bounds"), [](FString Arg) {Settings.bSystemShowBounds = FCString::Atoi(*Arg) != 0; }),
		MakeTuple(TEXT("SystemShowActiveOnlyInWorld="), TEXT("When enabled only active systems are shown in world"), [](FString Arg) {Settings.bSystemShowActiveOnlyInWorld = FCString::Atoi(*Arg) != 0; }),
		MakeTuple(TEXT("SystemDebugVerbosity="), TEXT("Set the in world system debug verbosity"), [](FString Arg) {Settings.SystemDebugVerbosity = FMath::Clamp(ENiagaraDebugHudVerbosity(FCString::Atoi(*Arg)), ENiagaraDebugHudVerbosity::None, ENiagaraDebugHudVerbosity::Verbose); }),
		MakeTuple(TEXT("SystemEmitterVerbosity="), TEXT("Set the in world system emitter debug verbosity"), [](FString Arg) {Settings.SystemEmitterVerbosity = FMath::Clamp(ENiagaraDebugHudVerbosity(FCString::Atoi(*Arg)), ENiagaraDebugHudVerbosity::None, ENiagaraDebugHudVerbosity::Verbose); }),
		MakeTuple(TEXT("SystemVariables="), TEXT("Set the system variables to display"), [](FString Arg) {FNiagaraDebugHUDVariable::InitFromString(Arg, Settings.SystemVariables); GCachedSystemVariables.Empty(); }),
		MakeTuple(TEXT("ShowSystemVariables="), TEXT("Set system variables visibility"), [](FString Arg) {Settings.bShowSystemVariables = FCString::Atoi(*Arg) != 0; GCachedSystemVariables.Empty(); }),

		// Particle commands
		MakeTuple(TEXT("EnableGpuParticleReadback="), TEXT("Enables GPU readback support for particle attributes"), [](FString Arg) {Settings.bEnableGpuParticleReadback = FCString::Atoi(*Arg) != 0;}),
		MakeTuple(TEXT("ParticleVariables="), TEXT("Set the particle variables to display"), [](FString Arg) {FNiagaraDebugHUDVariable::InitFromString(Arg, Settings.ParticlesVariables); GCachedSystemVariables.Empty(); }),
		MakeTuple(TEXT("ShowParticleVariables="), TEXT("Set Particle variables visibility"), [](FString Arg) {Settings.bShowParticleVariables = FCString::Atoi(*Arg) != 0; GCachedSystemVariables.Empty(); }),
		MakeTuple(TEXT("MaxParticlesToDisplay="), TEXT("Maximum number of particles to show variables on"), [](FString Arg) {Settings.MaxParticlesToDisplay = FMath::Max(FCString::Atoi(*Arg), 0); Settings.bUseMaxParticlesToDisplay = Settings.MaxParticlesToDisplay > 0; }),
		MakeTuple(TEXT("ShowParticlesVariablesWithSystem="), TEXT("When enabled particle variables are shown with the system display"), [](FString Arg) {Settings.bShowParticlesVariablesWithSystem = FCString::Atoi(*Arg) != 0; }),
	};

	static FAutoConsoleCommandWithWorldAndArgs CmdDebugHud(
		TEXT("fx.Niagara.Debug.Hud"),
		TEXT("Set options for debug hud display"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld*)
			{
				if ( Args.Num() == 0 )
				{
					UE_LOG(LogNiagara, Log, TEXT("fx.Niagara.Debug.Hud - CommandList"));
					for ( const auto& Command : GDebugConsoleCommands )
					{
						UE_LOG(LogNiagara, Log, TEXT(" \"%s\" %s"), Command.Get<0>(), Command.Get<1>());
					}
					return;
				}

				for ( FString Arg : Args )
				{
					bool bFound = false;
					for (const auto& Command : GDebugConsoleCommands)
					{
						if ( Arg.RemoveFromStart(Command.Get<0>()) )
						{
							Command.Get<2>()(Arg);
							bFound = true;
							break;
						}
					}

					if ( !bFound )
					{
						UE_LOG(LogNiagara, Warning, TEXT("Command '%s' not found"), *Arg);
					}
				}
			}
		)
	);


	template<typename TVariableList, typename TPredicate>
	void FindVariablesByWildcard(const TVariableList& Variables, const TArray<FNiagaraDebugHUDVariable>& DebugVariables, TPredicate Predicate)
	{
		if (DebugVariables.Num() == 0)
		{
			return;
		}

		for (const auto& Variable : Variables)
		{
			const FString VariableName = Variable.GetName().ToString();
			for (const FNiagaraDebugHUDVariable& DebugVariable : DebugVariables)
			{
				if (DebugVariable.bEnabled && (DebugVariable.Name.Len() > 0) && VariableName.MatchesWildcard(DebugVariable.Name))
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

			if (Settings.bShowSystemVariables && Settings.SystemVariables.Num() > 0)
			{
				const FNiagaraDataSetCompiledData& SystemCompiledData = NiagaraSystem->GetSystemCompiledData().DataSetCompiledData;
				FindVariablesByWildcard(
					SystemCompiledData.Variables,
					Settings.SystemVariables,
					[&](const FNiagaraVariable& Variable) { CachedVariables->SystemVariables.AddDefaulted_GetRef().Init(SystemCompiledData, Variable.GetName()); }
				);

				FindVariablesByWildcard(
					NiagaraSystem->GetExposedParameters().ReadParameterVariables(),
					Settings.SystemVariables,
					[&](const FNiagaraVariableWithOffset& Variable) { CachedVariables->UserVariables.Add(Variable); }
				);

				for (int32 iVariable = 0; iVariable < (int32)EEngineVariables::Num; ++iVariable)
				{
					for (const FNiagaraDebugHUDVariable& DebugVariable : Settings.SystemVariables)
					{
						if (DebugVariable.bEnabled && GEngineVariableStrings[iVariable].MatchesWildcard(DebugVariable.Name))
						{
							CachedVariables->bShowEngineVariable[iVariable] = true;
							break;
						}
					}
				}
			}

			if (Settings.bShowParticleVariables && Settings.ParticlesVariables.Num() > 0)
			{
				const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& AllEmittersCompiledData = NiagaraSystem->GetEmitterCompiledData();

				CachedVariables->ParticleVariables.AddDefaulted(AllEmittersCompiledData.Num());
				CachedVariables->ParticlePositionAccessors.AddDefaulted(AllEmittersCompiledData.Num());
				for (int32 iEmitter = 0; iEmitter < AllEmittersCompiledData.Num(); ++iEmitter)
				{
					const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(iEmitter);
					if (!EmitterHandle.IsValid() || !EmitterHandle.GetIsEnabled())
					{
						continue;
					}

					if (Settings.bEmitterFilterEnabled && !EmitterHandle.GetUniqueInstanceName().MatchesWildcard(Settings.EmitterFilter))
					{
						continue;
					}

					const FNiagaraDataSetCompiledData& EmitterCompiledData = AllEmittersCompiledData[iEmitter]->DataSetCompiledData;

					FindVariablesByWildcard(
						EmitterCompiledData.Variables,
						Settings.ParticlesVariables,
						[&](const FNiagaraVariable& Variable) { CachedVariables->ParticleVariables[iEmitter].AddDefaulted_GetRef().Init(EmitterCompiledData, Variable.GetName()); }
					);

					if (CachedVariables->ParticleVariables[iEmitter].Num() > 0)
					{
						static const FName PositionName(TEXT("Position"));
						CachedVariables->ParticlePositionAccessors[iEmitter].Init(EmitterCompiledData, PositionName);
					}
				}
			}
		}
		return *CachedVariables;
	}

	UFont* GetFont(ENiagaraDebugHudFont Font)
	{
		switch (Font)
		{
			default:
			case ENiagaraDebugHudFont::Small:	return GEngine->GetTinyFont();
			case ENiagaraDebugHudFont::Normal:	return GEngine->GetSmallFont();
		}
	};

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

	TPair<FVector2D, FVector2D> GetTextLocation(UFont* Font, const TCHAR* Text, const FNiagaraDebugHudTextOptions& TextOptions, const FVector2D ScreenLocation)
	{
		FVector2D StringSize = GetStringSize(Font, Text);
		FVector2D OutLocation = ScreenLocation + TextOptions.ScreenOffset;
		if (TextOptions.HorizontalAlignment == ENiagaraDebugHudHAlign::Center )
		{
			OutLocation.X -= StringSize.X * 0.5f;
		}
		else if (TextOptions.HorizontalAlignment == ENiagaraDebugHudHAlign::Right)
		{
			OutLocation.X -= StringSize.X;
		}
		if (TextOptions.VerticalAlignment == ENiagaraDebugHudVAlign::Center )
		{
			OutLocation.Y -= StringSize.Y * 0.5f;
		}
		else if (TextOptions.VerticalAlignment == ENiagaraDebugHudVAlign::Bottom)
		{
			OutLocation.Y -= StringSize.Y;
		}
		return TPair<FVector2D, FVector2D>(StringSize, OutLocation);
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

void FNiagaraDebugHud::UpdateSettings(const FNiagaraDebugHUDSettingsData& NewSettings)
{
	using namespace NiagaraDebugLocal;
	
	FNiagaraDebugHUDSettingsData::StaticStruct()->CopyScriptStruct(&Settings, &NewSettings);
	GCachedSystemVariables.Empty();
}

void FNiagaraDebugHud::AddMessage(FName Key, const FNiagaraDebugMessage& Message)
{
	Messages.FindOrAdd(Key) = Message;
}

void FNiagaraDebugHud::RemoveMessage(FName Key)
{
	Messages.Remove(Key);
}

void FNiagaraDebugHud::GatherSystemInfo()
{
	using namespace NiagaraDebugLocal;

	GlobalTotalSystems = 0;
	GlobalTotalScalability = 0;
	GlobalTotalEmitters = 0;
	GlobalTotalParticles = 0;
	GlobalTotalBytes = 0;
	PerSystemDebugInfo.Reset();
	InWorldComponents.Reset();

	UWorld* World = WeakWorld.Get();
	if (World == nullptr)
	{
		return;
	}

	// When not enabled do nothing
	if (!Settings.bEnabled)
	{
		return;
	}

	// If the overview is not enabled and we don't have any filters we can skip everything below as nothing will be displayed
	if (!Settings.bOverviewEnabled)
	{
		if ( !Settings.bActorFilterEnabled && !Settings.bComponentFilterEnabled && !Settings.bSystemFilterEnabled )
		{
			return;
		}
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
			SystemDebugInfo.bShowInWorld = Settings.bSystemFilterEnabled && SystemDebugInfo.SystemName.MatchesWildcard(Settings.SystemFilter);
		}

		if (SystemDebugInfo.bShowInWorld && (bIsActive || !Settings.bSystemShowActiveOnlyInWorld))
		{
			bool bIsMatch = true;

			// Filter by actor
			if ( Settings.bActorFilterEnabled )
			{
				AActor* Actor = NiagaraComponent->GetOwner();
				bIsMatch &= (Actor != nullptr) && Actor->GetName().MatchesWildcard(Settings.ActorFilter);
			}

			// Filter by component
			if ( bIsMatch && Settings.bComponentFilterEnabled )
			{
				bIsMatch &= NiagaraComponent->GetName().MatchesWildcard(Settings.ComponentFilter);
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

		// Track rough memory usage
		{
			for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
			{
				UNiagaraEmitter* NiagaraEmitter = EmitterInstance->GetCachedEmitter();
				if (NiagaraEmitter == nullptr)
				{
					continue;
				}

				const int64 BytesUsed = EmitterInstance->GetTotalBytesUsed();
				SystemDebugInfo.TotalBytes += BytesUsed;
				GlobalTotalBytes += BytesUsed;
			}
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
		if (!Settings.bEnableGpuParticleReadback)
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

FNiagaraDebugHud::FValidationErrorInfo& FNiagaraDebugHud::GetValidationErrorInfo(UNiagaraComponent* NiagaraComponent)
{
	FValidationErrorInfo* InfoOut = ValidationErrors.Find(NiagaraComponent);
	if (!InfoOut)
	{
		InfoOut = &ValidationErrors.Add(NiagaraComponent);
		InfoOut->DisplayName = GetNameSafe(NiagaraComponent->GetOwner()) / *NiagaraComponent->GetName() / *GetNameSafe(NiagaraComponent->GetAsset());
	}
	InfoOut->LastWarningTime = FPlatformTime::Seconds();
	return *InfoOut;
}

void FNiagaraDebugHud::DebugDrawCallback(UCanvas* Canvas, APlayerController* PC)
{
	using namespace NiagaraDebugLocal;

	if (!Settings.bEnabled)
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

	float CurrTime = WorldManager->GetWorld()->GetRealTimeSeconds();
	DeltaSeconds = CurrTime - LastDrawTime;

	// Draw in world components
	DrawComponents(WorldManager, Canvas);

	// Draw overview
	DrawOverview(WorldManager, Canvas->Canvas);

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

	LastDrawTime = CurrTime;
}

void FNiagaraDebugHud::DrawOverview(class FNiagaraWorldManager* WorldManager, FCanvas* DrawCanvas)
{
	using namespace NiagaraDebugLocal;

	UFont* Font = GetFont(Settings.OverviewFont);
	const float fAdvanceHeight = Font->GetMaxCharHeight() + 1.0f;

	const FLinearColor HeadingColor = FLinearColor::Green;
	const FLinearColor DetailColor = FLinearColor::White;
	const FLinearColor DetailHighlightColor = FLinearColor::Yellow;

	FVector2D TextLocation = Settings.OverviewLocation;

	// Display overview
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
			if (Settings.bSystemFilterEnabled || Settings.bEmitterFilterEnabled || Settings.bActorFilterEnabled || Settings.bComponentFilterEnabled)
			{
				if (Settings.bSystemFilterEnabled)
				{
					OverviewString.Appendf(TEXT("SystemFilter: %s"), *Settings.SystemFilter);
					OverviewString.Append(Separator);
				}
				if (Settings.bEmitterFilterEnabled)
				{
					OverviewString.Appendf(TEXT("EmitterFilter: %s"), *Settings.EmitterFilter);
					OverviewString.Append(Separator);
				}
				if (Settings.bActorFilterEnabled)
				{
					OverviewString.Appendf(TEXT("ActorFilter: %s"), *Settings.ActorFilter);
					OverviewString.Append(Separator);
				}
				if (Settings.bComponentFilterEnabled)
				{
					OverviewString.Appendf(TEXT("ComponentFilter: %s"), *Settings.ComponentFilter);
					OverviewString.Append(Separator);
				}
			}
		}

		if (Settings.bOverviewEnabled || OverviewString.Len() > 0)
		{
			static const float ColumnOffset[] = { 0, 150, 300, 450, 600 };
			static const float GuessWidth = 750.0f;

			const int32 NumLines = 1 + (Settings.bOverviewEnabled ? 1 : 0);
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
			if (Settings.bOverviewEnabled)
			{
				static const TCHAR* HeadingText[] = { TEXT("TotalSystems:"), TEXT("TotalScalability:"), TEXT("TotalEmitters:") , TEXT("TotalParticles:"), TEXT("TotalMemory:") };
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[0], TextLocation.Y, HeadingText[0], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[1], TextLocation.Y, HeadingText[1], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[2], TextLocation.Y, HeadingText[2], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[3], TextLocation.Y, HeadingText[3], Font, HeadingColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[4], TextLocation.Y, HeadingText[4], Font, HeadingColor);

				static const float DetailOffset[] =
				{
					ColumnOffset[0] + Font->GetStringSize(HeadingText[0]) + 5.0f,
					ColumnOffset[1] + Font->GetStringSize(HeadingText[1]) + 5.0f,
					ColumnOffset[2] + Font->GetStringSize(HeadingText[2]) + 5.0f,
					ColumnOffset[3] + Font->GetStringSize(HeadingText[3]) + 5.0f,
					ColumnOffset[4] + Font->GetStringSize(HeadingText[4]) + 5.0f,
				};

				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[0], TextLocation.Y, *FString::FromInt(GlobalTotalSystems), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[1], TextLocation.Y, *FString::FromInt(GlobalTotalScalability), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[2], TextLocation.Y, *FString::FromInt(GlobalTotalEmitters), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[3], TextLocation.Y, *FString::FromInt(GlobalTotalParticles), Font, DetailColor);
				DrawCanvas->DrawShadowedString(TextLocation.X + DetailOffset[4], TextLocation.Y, *FString::Printf(TEXT("%6.2fmb"), float(double(GlobalTotalBytes) / (1024.0*1024.0))), Font, DetailColor);

				TextLocation.Y += fAdvanceHeight;
			}
		}
	}

	// Display active systems information
	if (Settings.bOverviewEnabled)
	{
		TextLocation.Y += fAdvanceHeight;

		static float ColumnOffset[] = { 0, 300, 400, 500, 600, 700 };
		static float GuessWidth = 800.0f;

		const uint32 NumLines = 1 + PerSystemDebugInfo.Num();
		DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, GuessWidth + 1.0f, 2.0f + (float(NumLines) * fAdvanceHeight), 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[0], TextLocation.Y, TEXT("System Name"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[1], TextLocation.Y, TEXT("# Active"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[2], TextLocation.Y, TEXT("# Scalability"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[3], TextLocation.Y, TEXT("# Emitters"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[4], TextLocation.Y, TEXT("# Particles"), Font, HeadingColor);
		DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[5], TextLocation.Y, TEXT("# MBytes"), Font, HeadingColor);
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
			DrawCanvas->DrawShadowedString(TextLocation.X + ColumnOffset[5], TextLocation.Y, *FString::Printf(TEXT("%6.2f"), double(SystemInfo.TotalBytes) / (1024.0*1024.0)), Font, RowColor);
			TextLocation.Y += fAdvanceHeight;
		}
	}

	TextLocation.Y += fAdvanceHeight;

	//Display global budget usage information
	if(Settings.bShowGlobalBudgetInfo)
	{
		static const float GuessWidth = 400.0f;
		if (FFXBudget::Enabled())
		{
			FFXTimeData Time = FFXBudget::GetTime();
			FFXTimeData Budget = FFXBudget::GetBudget();
			FFXTimeData Usage = FFXBudget::GetUsage();
			FFXTimeData AdjustedUsage = FFXBudget::GetAdjustedUsage();

			const int32 NumLines = 5;//Header, time, budget, usage and adjusted usage.
			DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, GuessWidth + 1.0f, 2.0f + (float(NumLines) * fAdvanceHeight), 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

			FString Temp;
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Global Budget Info"), Font, HeadingColor);

			TextLocation.Y += fAdvanceHeight;
			FString TimeLabels[] =
			{
				TEXT("Time: "),
				TEXT("Budget: "),
				TEXT("Usage: "),
				TEXT("Adjusted: ")
			};

			int32 LabelSizes[] =
			{
				Font->GetStringSize(*TimeLabels[0]),
				Font->GetStringSize(*TimeLabels[1]),
				Font->GetStringSize(*TimeLabels[2]),
				Font->GetStringSize(*TimeLabels[3])
			};
			int32 MaxLabelSize = FMath::Max(FMath::Max(LabelSizes[0], LabelSizes[1]), FMath::Max(LabelSizes[2], LabelSizes[3]));

			auto DrawTimeData = [&](FString Heading, FFXTimeData Time, float HighlightThreshold = FLT_MAX)
			{
				//Draw Time
				int32 XOff = 0;
				int32 AlignSize = 50;
				FString Text[] = {
					Heading,
					FString::Printf(TEXT("GT = %2.3f"), Time.GT),
					FString::Printf(TEXT("CNC = %2.3f"), Time.GTConcurrent),
					FString::Printf(TEXT("RT = %2.3f"), Time.RT)
				};
				int32 Sizes[] =
				{
					MaxLabelSize,
					Font->GetStringSize(*Text[1]),
					Font->GetStringSize(*Text[2]),
					Font->GetStringSize(*Text[3])
				};
				DrawCanvas->DrawShadowedString(TextLocation.X + XOff, TextLocation.Y, *Text[0], Font, HeadingColor);
				XOff = AlignArbitrary(XOff + Sizes[0], AlignSize);
				DrawCanvas->DrawShadowedString(TextLocation.X + XOff, TextLocation.Y, *Text[1], Font, Time.GT > HighlightThreshold ? DetailHighlightColor : DetailColor);
				XOff = AlignArbitrary(XOff + Sizes[1], AlignSize);
				DrawCanvas->DrawShadowedString(TextLocation.X + XOff, TextLocation.Y, *Text[2], Font, Time.GTConcurrent > HighlightThreshold ? DetailHighlightColor : DetailColor);
				XOff = AlignArbitrary(XOff + Sizes[2], AlignSize);
				DrawCanvas->DrawShadowedString(TextLocation.X + XOff, TextLocation.Y, *Text[3], Font, Time.RT > HighlightThreshold ? DetailHighlightColor : DetailColor);
				XOff = AlignArbitrary(XOff + Sizes[3], AlignSize);

				TextLocation.Y += fAdvanceHeight;
			};
			DrawTimeData(TimeLabels[0], Time);
			DrawTimeData(TimeLabels[1], Budget);
			DrawTimeData(TimeLabels[2], Usage, 1.0f);
			DrawTimeData(TimeLabels[3], AdjustedUsage, 1.0f);
		}
		else
		{
			int32 NumLines = 2;
			DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, GuessWidth + 1.0f, 2.0f + (float(NumLines) * fAdvanceHeight), 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Global Budget Info"), Font, HeadingColor);
			TextLocation.Y += fAdvanceHeight;
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Global budget tracking is disabled."), Font, DetailHighlightColor);
			TextLocation.Y += fAdvanceHeight;
		}
	}

	DrawValidation(WorldManager, DrawCanvas, TextLocation);

	DrawMessages(WorldManager, DrawCanvas, TextLocation);
}

void FNiagaraDebugHud::DrawValidation(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas, FVector2D& TextLocation)
{
	using namespace NiagaraDebugLocal;

	if (!Settings.bValidateSystemSimulationDataBuffers && !Settings.bValidateParticleDataBuffers)
	{
		return;
	}

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

		auto SystemSimulation = SystemInstance->GetSystemSimulation();
		if (!SystemSimulation.IsValid() || !SystemSimulation->IsValid())
		{
			continue;
		}

		// Ensure systems are complete
		SystemSimulation->WaitForInstancesTickComplete();

		// Look for validation errors
		if (Settings.bValidateSystemSimulationDataBuffers)
		{
			FNiagaraDataSetDebugAccessor::ValidateDataBuffer(
				SystemSimulation->MainDataSet.GetCompiledData(),
				SystemSimulation->MainDataSet.GetCurrentData(),
				SystemInstance->GetSystemInstanceIndex(),
				[&](const FNiagaraVariable& Variable, int32 ComponentIndex)
				{
					auto& ValidationError = GetValidationErrorInfo(NiagaraComponent);
					ValidationError.SystemVariablesWithErrors.AddUnique(Variable.GetName());
				}
			);
		}

		if (Settings.bValidateParticleDataBuffers)
		{
			auto& EmitterHandles = SystemInstance->GetEmitters();
			for ( int32 iEmitter=0; iEmitter < EmitterHandles.Num(); ++iEmitter)
			{
				FNiagaraEmitterInstance* EmitterInstance = &EmitterHandles[iEmitter].Get();
				if ( !EmitterInstance )
				{
					continue;
				}

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

				FNiagaraDataSetDebugAccessor::ValidateDataBuffer(
					ParticleDataSet->GetCompiledData(),
					DataBuffer,
					[&](const FNiagaraVariable& Variable, int32 InstanceIndex, int32 ComponentIndex)
					{
						auto& ValidationError = GetValidationErrorInfo(NiagaraComponent);
						ValidationError.ParticleVariablesWithErrors.FindOrAdd(EmitterInstance->GetCachedEmitter()->GetFName()).AddUnique(Variable.GetName());
					}
				);
			}
		}
	}

	if ( ValidationErrors.Num() > 0 )
	{
		const double TrimSeconds = FPlatformTime::Seconds() - 5.0;

		TStringBuilder<1024> ErrorString;
		for (auto ValidationIt=ValidationErrors.CreateIterator(); ValidationIt; ++ValidationIt)
		{
			const FValidationErrorInfo& ErrorInfo = ValidationIt.Value();
			if (ErrorInfo.LastWarningTime < TrimSeconds)
			{
				ValidationIt.RemoveCurrent();
				continue;
			}

			ErrorString.Append(ErrorInfo.DisplayName);
			ErrorString.Append(TEXT("\n"));

			// System Variables
			{
				const int32 NumVariables = FMath::Min(ErrorInfo.SystemVariablesWithErrors.Num(), 3);
				if (NumVariables > 0)
				{
					for (int32 iVariable=0; iVariable < NumVariables; ++iVariable)
					{
						if (iVariable == 0)
						{
							ErrorString.Append(TEXT("- SystemVars - "));
						}
						else
						{
							ErrorString.Append(TEXT(", "));
						}
						ErrorString.Append(ErrorInfo.SystemVariablesWithErrors[iVariable].ToString());
					}
					if (NumVariables != ErrorInfo.SystemVariablesWithErrors.Num())
					{
						ErrorString.Append(TEXT(", ..."));
					}
					ErrorString.Append(TEXT("\n"));
				}
			}

			// Particle Variables
			for (auto EmitterIt=ErrorInfo.ParticleVariablesWithErrors.CreateConstIterator(); EmitterIt; ++EmitterIt)
			{
				const TArray<FName>& EmitterVariables = EmitterIt.Value();
				const int32 NumVariables = FMath::Min(EmitterVariables.Num(), 3);
				if (NumVariables > 0)
				{
					for (int32 iVariable = 0; iVariable < NumVariables; ++iVariable)
					{
						if (iVariable == 0)
						{
							ErrorString.Appendf(TEXT("- Particles(%s) - "), *EmitterIt.Key().ToString());
						}
						else
						{
							ErrorString.Append(TEXT(", "));
						}
						ErrorString.Append(EmitterVariables[iVariable].ToString());
					}
					if (NumVariables != EmitterVariables.Num())
					{
						ErrorString.Append(TEXT(", ..."));
					}
					ErrorString.Append(TEXT("\n"));
				}
			}
		}

		if (ErrorString.Len() > 0)
		{
			UFont* Font = GetFont(Settings.OverviewFont);
			const float fAdvanceHeight = Font->GetMaxCharHeight() + 1.0f;

			const FVector2D ErrorStringSize = GetStringSize(Font, ErrorString.ToString());

			TextLocation.Y += fAdvanceHeight;
			DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, ErrorStringSize.X + 2.0f, ErrorStringSize.Y + fAdvanceHeight + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Found Errors:"), Font, ErrorTextColor);
			TextLocation.Y += fAdvanceHeight;

			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, ErrorString.ToString(), Font, ErrorTextColor);
		}
	}
}

void FNiagaraDebugHud::DrawComponents(FNiagaraWorldManager* WorldManager, UCanvas* Canvas)
{
	using namespace NiagaraDebugLocal;

	FCanvas* DrawCanvas = Canvas->Canvas;
	UWorld* World = WorldManager->GetWorld();
	UFont* SystemFont = GetFont(Settings.SystemTextOptions.Font);
	UFont* ParticleFont = GetFont(Settings.ParticleTextOptions.Font);

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
		if (Settings.bSystemShowBounds && bIsActive)
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

		const FLinearColor TextColor = ValidationErrors.Contains(NiagaraComponent) ? ErrorTextColor : WorldTextColor;


		// Show particle data in world
		if (!Settings.bShowParticlesVariablesWithSystem && bSystemSimulationValid)
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

				const FTransform& SystemTransform = SystemInstance->GetWorldTransform();
				const bool bParticlesLocalSpace = EmitterInstance->GetCachedEmitter()->bLocalSpace;

				const uint32 NumParticles = Settings.bUseMaxParticlesToDisplay ? FMath::Min((uint32)Settings.MaxParticlesToDisplay, DataBuffer->GetNumInstances()) : DataBuffer->GetNumInstances();
				for (uint32 iInstance = 0; iInstance < NumParticles; ++iInstance)
				{
					const FVector ParticalLocalPosition = PositionReader.Get(iInstance);
					const FVector ParticleWorldPosition = bParticlesLocalSpace ? SystemTransform.TransformPosition(ParticalLocalPosition) : ParticalLocalPosition;

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
						const TPair<FVector2D, FVector2D> SizeAndLocation = GetTextLocation(ParticleFont, FinalString, Settings.ParticleTextOptions, FVector2D(ParticleScreenLocation));
						DrawCanvas->DrawTile(SizeAndLocation.Value.X - 1.0f, SizeAndLocation.Value.Y - 1.0f, SizeAndLocation.Key.X + 2.0f, SizeAndLocation.Key.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
						DrawCanvas->DrawShadowedString(SizeAndLocation.Value.X, SizeAndLocation.Value.Y, FinalString, ParticleFont, TextColor);
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
			if ( Settings.SystemDebugVerbosity != ENiagaraDebugHudVerbosity::None)
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

				if (Settings.SystemDebugVerbosity == ENiagaraDebugHudVerbosity::Verbose)
				{
					StringBuilder.Appendf(TEXT("System ActualState %s - RequestedState %s\n"), *ExecutionStateEnum->GetNameStringByIndex((int32)SystemInstance->GetActualExecutionState()), *ExecutionStateEnum->GetNameStringByIndex((int32)SystemInstance->GetRequestedExecutionState()));
					if (NiagaraComponent->PoolingMethod != ENCPoolMethod::None)
					{
						StringBuilder.Appendf(TEXT("Pooled - %s\n"), *PoolingMethodEnum->GetNameStringByIndex((int32)NiagaraComponent->PoolingMethod));
					}
					if (bIsActive && NiagaraComponent->IsRegisteredWithScalabilityManager())
					{
						StringBuilder.Appendf(TEXT("Scalability - %s\n"), *GetNameSafe(NiagaraSystem->GetEffectType()));
						if (SystemInstance->SignificanceIndex != INDEX_NONE )
						{
							StringBuilder.Appendf(TEXT("SignificanceIndex - %d\n"), SystemInstance->SignificanceIndex);
						}
					}

					int64 TotalBytes = 0;
					for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
					{
						if ( UNiagaraEmitter* NiagaraEmitter = EmitterInstance->GetCachedEmitter() )
						{
							TotalBytes += EmitterInstance->GetTotalBytesUsed();
						}
					}
					StringBuilder.Appendf(TEXT("Memory - %6.2fMB\n"), float(double(TotalBytes) / (1024.0*1024.0)));
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

						if (Settings.SystemEmitterVerbosity == ENiagaraDebugHudVerbosity::Verbose)
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

					if (Settings.SystemEmitterVerbosity == ENiagaraDebugHudVerbosity::Basic)
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
						if (Settings.bShowParticlesVariablesWithSystem)
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
								const uint32 NumParticles = Settings.bUseMaxParticlesToDisplay ? FMath::Min((uint32)Settings.MaxParticlesToDisplay, DataBuffer->GetNumInstances()) : DataBuffer->GetNumInstances();
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
					if (Settings.SystemDebugVerbosity >= ENiagaraDebugHudVerbosity::Basic)
					{
						StringBuilder.Appendf(TEXT("Deactivated by Scalability - %s "), *GetNameSafe(NiagaraSystem->GetEffectType()));
						if (Settings.SystemDebugVerbosity >= ENiagaraDebugHudVerbosity::Verbose)
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
								if (ScalabilityState.bCulledByGlobalBudget)
								{
									StringBuilder.Append(TEXT(" GlobalBudgetCulled"));
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
				const TPair<FVector2D, FVector2D> SizeAndLocation = GetTextLocation(SystemFont, FinalString, Settings.SystemTextOptions, FVector2D(ScreenLocation));
				DrawCanvas->DrawTile(SizeAndLocation.Value.X - 1.0f, SizeAndLocation.Value.Y - 1.0f, SizeAndLocation.Key.X + 2.0f, SizeAndLocation.Key.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
				DrawCanvas->DrawShadowedString(SizeAndLocation.Value.X, SizeAndLocation.Value.Y, FinalString, SystemFont, TextColor);
			}
		}
	}
}

void FNiagaraDebugHud::DrawMessages(class FNiagaraWorldManager* WorldManager, class FCanvas* DrawCanvas, FVector2D& TextLocation)
{
	using namespace NiagaraDebugLocal;

	static const float MinWidth = 500.0f;
	
	UFont* Font = GetFont(Settings.OverviewFont);
	const float fAdvanceHeight = Font->GetMaxCharHeight() + 1.0f;

	FVector2D BackgroundSize(MinWidth, 0.0f);
	TArray<FName, TInlineAllocator<8>> ToRemove;
	for (TPair<FName, FNiagaraDebugMessage>& Pair : Messages)
	{
		FName& Key = Pair.Key;
		FNiagaraDebugMessage& Message = Pair.Value;

		Message.Lifetime -= DeltaSeconds;
		if (Message.Lifetime > 0.0f)
		{
			BackgroundSize = FMath::Max(BackgroundSize, GetStringSize(Font, *Message.Message));
		}
		else
		{
			ToRemove.Add(Key);
		}
	}

	//Not sure why but the get size always underestimates slightly.
	BackgroundSize.X += 20.0f;

	for (FName DeadMessage : ToRemove)
	{
		Messages.Remove(DeadMessage);
	}

	if (Messages.Num())
	{
		TextLocation.Y += fAdvanceHeight;
		DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, BackgroundSize.X + 2.0f, BackgroundSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

		for (TPair<FName, FNiagaraDebugMessage>& Pair : Messages)
		{
			FName& Key = Pair.Key;
			FNiagaraDebugMessage& Message = Pair.Value;

			FLinearColor MessageColor;
			if (Message.Type == ENiagaraDebugMessageType::Info) MessageColor = InfoTextColor;
			else if (Message.Type == ENiagaraDebugMessageType::Warning) MessageColor = WarningTextColor;
			else if (Message.Type == ENiagaraDebugMessageType::Error) MessageColor = ErrorTextColor;

			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, *Message.Message, Font, MessageColor);//TODO: Sort by type / lifetime?
			TextLocation.Y += fAdvanceHeight;
		}
	}
}