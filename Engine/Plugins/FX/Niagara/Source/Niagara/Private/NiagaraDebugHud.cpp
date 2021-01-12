// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugHud.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDataSetDebugAccessor.h"

#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Components/LineBatchComponent.h"
#include "DrawDebugHelpers.h"

namespace NiagaraDebugLocal
{
	struct FCachedVariables
	{
		bool bIsCached = false;
		TArray<FNiagaraDataSetDebugAccessor> SystemVariables;					// System & Emitter variables since both are inside the same DataBuffer
		TArray<FNiagaraVariableBase> UserVariables;								// Exposed user parameters which will pull from the component
		TArray<TArray<FNiagaraDataSetDebugAccessor>> ParticleVariables;			// Per Emitter Particle variables
		TArray<FNiagaraDataSetAccessor<FVector>> ParticlePositionAccessors;		// Only valid if we have particle attributes
	};

	static bool GEnabled = false;
	static FVector2D GDisplayLocation = FVector2D(30.0f, 150.0f);
	static ENiagaraDebugHudSystemVerbosity GSystemVerbosity = ENiagaraDebugHudSystemVerbosity::Minimal;
	static bool GSystemShowBounds = false;
	static FString GSystemFilter;
	static FString GComponentFilter;
	static TMap<TWeakObjectPtr<UNiagaraSystem>, FCachedVariables> GCachedSystemVariables;
	static TArray<FString> GSystemVariables;
	static TArray<FString> GParticleVariables;
	static uint32 GMaxParticlesToDisplay = 32;
	static bool GShowParticlesInWorld = true;

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
						if (Arg.RemoveFromStart(TEXT("Enabled=")))
						{
							GEnabled = FCString::Atoi(*Arg) != 0;
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
						else if (Arg.RemoveFromStart(TEXT("SystemFilter=")))
						{
							GSystemFilter = Arg;
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
				else
				{
					UE_LOG(
						LogNiagara, Log,
						TEXT("fx.Niagara.DebugHud Enabled=%d DisplayLocation=%f,%f SystemVerbosity=%d SystemShowBounds=%d SystemFilter=%s ComponentFilter=%s SystemVariables=%s ParticleVariables=%s MaxParticlesToDisplay=%d ShowParticlesInWorld=%d"),
						GEnabled,
						(int32)GSystemVerbosity,
						GDisplayLocation.X, GDisplayLocation.Y,
						GSystemShowBounds,
						*GSystemFilter,
						*GComponentFilter,
						*FString::Join(GSystemVariables, TEXT(",")),
						*FString::Join(GParticleVariables, TEXT(",")),
						GMaxParticlesToDisplay,
						GShowParticlesInWorld
					);
				}
			}
		)
	);

	const FCachedVariables& GetCachedVariables(UNiagaraSystem* NiagaraSystem)
	{
		FCachedVariables& CachedVariables = GCachedSystemVariables.FindOrAdd(NiagaraSystem);
		if ( !CachedVariables.bIsCached )
		{
			CachedVariables.bIsCached = true;
			if (GSystemVariables.Num() > 0)
			{
				const FNiagaraDataSetCompiledData& SystemCompiledData = NiagaraSystem->GetSystemCompiledData().DataSetCompiledData;
				//const FNiagaraUserRedirectionParameterStore& ExposedParameters = NiagaraSystem->GetExposedParameters();
				//TArrayView<const FNiagaraVariableWithOffset> DestParameters = OverrideParameters.ReadParameterVariables();
				TArrayView<const FNiagaraVariableWithOffset> UserParameters = NiagaraSystem->GetExposedParameters().ReadParameterVariables();
				for (const FString& VarToFind : GSystemVariables)
				{
					// Match any or exact match?
					if ( VarToFind[0] == TEXT('*') )
					{
						FString VarToFindMatch = VarToFind.RightChop(1);
						for (const FNiagaraVariable& SystemVariable : SystemCompiledData.Variables)
						{
							if (VarToFindMatch.IsEmpty() || SystemVariable.GetName().ToString().Contains(VarToFindMatch))
							{
								CachedVariables.SystemVariables.AddDefaulted_GetRef().Init(SystemCompiledData, SystemVariable.GetName());
							}
						}

						UserParameters.FindByPredicate(
							[&](const FNiagaraVariableWithOffset& UserParam)
							{
								if (VarToFindMatch.IsEmpty() || UserParam.GetName().ToString().Contains(VarToFindMatch))
								{
									CachedVariables.UserVariables.Add(UserParam);
									return true;
								}
								return false;
							}
						);
					}
					else
					{
						FName VarToFindName(VarToFind);
						FNiagaraDataSetDebugAccessor DebugAccessor;
						if (DebugAccessor.Init(SystemCompiledData, VarToFindName) )
						{
							CachedVariables.SystemVariables.Add(DebugAccessor);
						}

						UserParameters.FindByPredicate(
							[&](const FNiagaraVariableWithOffset& UserParam)
							{
								if ( UserParam.GetName() == VarToFindName )
								{
									CachedVariables.UserVariables.Add(UserParam);
									return true;
								}
								return false;
							}
						);
					}
				}
			}

			if (GParticleVariables.Num() > 0)
			{
				const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& AllEmittersCompiledData = NiagaraSystem->GetEmitterCompiledData();
				CachedVariables.ParticleVariables.AddDefaulted(AllEmittersCompiledData.Num());
				CachedVariables.ParticlePositionAccessors.AddDefaulted(AllEmittersCompiledData.Num());
				for (int32 i=0; i < AllEmittersCompiledData.Num(); ++i)
				{
					const FNiagaraDataSetCompiledData& EmitterCompiledData = AllEmittersCompiledData[i]->DataSetCompiledData;
					if ( EmitterCompiledData.SimTarget == ENiagaraSimTarget::GPUComputeSim )
					{
						//-TODO: Support GPU via a readback or using the GPU to write the data via a batched element
						continue;
					}

					for (FString VarToFind : GParticleVariables)
					{
						// Match any or exact match?
						if (VarToFind[0] == TEXT('*'))
						{
							FString VarToFindMatch = VarToFind.RightChop(1);
							for (const FNiagaraVariable& ParticleVariable : EmitterCompiledData.Variables)
							{
								if (VarToFindMatch.IsEmpty() || ParticleVariable.GetName().ToString().Contains(VarToFindMatch))
								{
									CachedVariables.ParticleVariables[i].AddDefaulted_GetRef().Init(EmitterCompiledData, ParticleVariable.GetName());
								}
							}
						}
						else
						{
							FNiagaraDataSetDebugAccessor DebugAccessor;
							if (DebugAccessor.Init(EmitterCompiledData, FName(VarToFind)))
							{
								CachedVariables.ParticleVariables[i].Add(DebugAccessor);
							}
						}
					}

					if ( CachedVariables.ParticleVariables[i].Num() > 0)
					{
						static const FName PositionName(TEXT("Position"));
						CachedVariables.ParticlePositionAccessors[i].Init(EmitterCompiledData, PositionName);
					}
				}
			}
		}
		return CachedVariables;
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
	WeakWorld = World;
	DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FNiagaraDebugHud::DebugDrawNiagara));
}

FNiagaraDebugHud::~FNiagaraDebugHud()
{
	UDebugDrawService::Unregister(DebugDrawHandle);
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

		const bool bHasScalability = NiagaraComponent->IsRegisteredWithScalabilityManager();
		if (!NiagaraComponent->IsActive() && !bHasScalability )
		{
			continue;
		}

		FSystemDebugInfo& SystemDebugInfo = PerSystemDebugInfo.FindOrAdd(NiagaraComponent->GetAsset()->GetFName());
		if (SystemDebugInfo.SystemName.IsEmpty())
		{
			SystemDebugInfo.SystemName = GetNameSafe(NiagaraComponent->GetAsset());
			SystemDebugInfo.bShowInWorld = !GSystemFilter.IsEmpty() && SystemDebugInfo.SystemName.Contains(GSystemFilter);
		}

		if (SystemDebugInfo.bShowInWorld)
		{
			if ( GComponentFilter.IsEmpty() || NiagaraComponent->GetName().Contains(GComponentFilter) )
			{
				InWorldComponents.Add(NiagaraComponent);
			}
		}

		if ( bHasScalability )
		{
			++GlobalTotalScalability;
			++SystemDebugInfo.TotalScalability;
		}

		if ( NiagaraComponent->IsActive() )
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

void FNiagaraDebugHud::DebugDrawNiagara(UCanvas* InCanvas, APlayerController* PC)
{
	using namespace NiagaraDebugLocal;

	if (!GEnabled || !InCanvas || !InCanvas->Canvas)
	{
		return;
	}

	UWorld* World = WeakWorld.Get();
	if (World == nullptr)
	{
		return;
	}

	FCanvas* DrawCanvas = InCanvas->Canvas;

	UFont* Font = GEngine->GetSmallFont();
	UFont* WorldFont = GEngine->GetTinyFont();
	const float fAdvanceHeight = Font->GetMaxCharHeight() + 1.0f;

	const FLinearColor HeadingColor = FLinearColor::Green;
	const FLinearColor DetailColor = FLinearColor::White;
	const FLinearColor DetailHighlightColor = FLinearColor::Yellow;

	const FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

	FVector2D TextLocation = GDisplayLocation;

	// Display global system information
	{
		static const float ColumnOffset[] = { 0, 150, 300, 450 };
		static const float GuessWidth = 600.0f;

		const int32 NumLines = 2 + (GSystemFilter.IsEmpty() ? 0 : 1) + (GComponentFilter.IsEmpty() ? 0 : 1);
		DrawCanvas->DrawTile(TextLocation.X - 1.0f, TextLocation.Y - 1.0f, GuessWidth + 1.0f, 2.0f + (float(NumLines) * fAdvanceHeight), 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);

		DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, TEXT("Niagara DebugHud"), Font, HeadingColor);
		TextLocation.Y += fAdvanceHeight;
		if (!GSystemFilter.IsEmpty())
		{
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, *FString::Printf(TEXT("SystemFilter: %s"), *GSystemFilter), Font, HeadingColor);
			TextLocation.Y += fAdvanceHeight;
		}
		if (!GComponentFilter.IsEmpty())
		{
			DrawCanvas->DrawShadowedString(TextLocation.X, TextLocation.Y, *FString::Printf(TEXT("ComponentFilter: %s"), *GComponentFilter), Font, HeadingColor);
			TextLocation.Y += fAdvanceHeight;
		}

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

	TextLocation.Y += fAdvanceHeight;

	// Display active systems information
	{
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

	// Draw in world components
	UEnum* ExecutionStateEnum = StaticEnum<ENiagaraExecutionState>();
	for ( TWeakObjectPtr<UNiagaraComponent> WeakComponent : InWorldComponents )
	{
		UNiagaraComponent* NiagaraComponent = WeakComponent.Get();
		if (NiagaraComponent == nullptr)
		{
			continue;
		}

		UNiagaraSystem* NiagaraSystem = NiagaraComponent->GetAsset();
		FNiagaraSystemInstance* SystemInstance = NiagaraComponent->GetSystemInstance();
		if ( (NiagaraSystem == nullptr) || (SystemInstance == nullptr) )
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

				auto EmitterInstance = SystemInstance->GetEmitters()[iEmitter];
				FNiagaraDataSet& ParticleDataSet = EmitterInstance->GetData();
				FNiagaraDataBuffer* DataBuffer = ParticleDataSet.GetCurrentData();
				if (!DataBuffer || !DataBuffer->GetNumInstances())
				{
					continue;
				}

				auto PositionReader = CachedVariables.ParticlePositionAccessors[iEmitter].GetReader(ParticleDataSet);

				//StringBuilder.Appendf(TEXT("Emitter (%s)\n"), *EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName());
				const uint32 NumParticles = FMath::Min(GMaxParticlesToDisplay, DataBuffer->GetNumInstances());
				for (uint32 iInstance=0; iInstance < NumParticles; ++iInstance)
				{
					const FVector ParticleWorldPosition = PositionReader.Get(iInstance);
					const FVector ParticleScreenLocation = InCanvas->Project(ParticleWorldPosition);
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
						const FVector2D StringSize = GetStringSize(WorldFont, FinalString);
						DrawCanvas->DrawTile(ParticleScreenLocation.X - 1.0f, ParticleScreenLocation.Y - 1.0f, StringSize.X + 2.0f, StringSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
						DrawCanvas->DrawShadowedString(ParticleScreenLocation.X, ParticleScreenLocation.Y, FinalString, WorldFont, FLinearColor::White);
					}
				}
			}
		}

		const FVector ScreenLocation = InCanvas->Project(ComponentLocation);
		if (!FMath::IsNearlyZero(ScreenLocation.Z))
		{
			// Show locator
			DrawSystemLocation(InCanvas, bIsActive, ScreenLocation, ComponentRotation);

			// Show system text
			if ((GSystemVerbosity > ENiagaraDebugHudSystemVerbosity::None) && (GSystemVerbosity <= ENiagaraDebugHudSystemVerbosity::Verbose))
			{
				TStringBuilder<1024> StringBuilder;
				StringBuilder.Appendf(TEXT("Component - %s\n"), *GetNameSafe(NiagaraComponent));
				StringBuilder.Appendf(TEXT("System - %s\n"), *GetNameSafe(NiagaraSystem));

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

						if ( GSystemVerbosity == ENiagaraDebugHudSystemVerbosity::Verbose )
						{
							StringBuilder.Appendf(TEXT("Emitters %s - State %s - Particles %d\n"), *NiagaraEmitter->GetUniqueEmitterName(), *ExecutionStateEnum->GetNameStringByIndex((int32)EmitterInstance->GetExecutionState()), EmitterInstance->GetNumParticles());
						}
					}

					if ( GSystemVerbosity == ENiagaraDebugHudSystemVerbosity::Basic )
					{
						StringBuilder.Appendf(TEXT("Emitters - %d / %d\n"), ActiveEmitters, TotalEmitters);
						StringBuilder.Appendf(TEXT("Particles - %d\n"), ActiveParticles);
					}

					// Any variables to display?
					if (bSystemSimulationValid)
					{
						const FCachedVariables& CachedVariables = GetCachedVariables(NiagaraSystem);
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

						if (CachedVariables.UserVariables.Num() > 0)
						{
							if (FNiagaraUserRedirectionParameterStore* ParameterStore = SystemInstance->GetOverrideParameters())
							{
								for (const FNiagaraVariableBase& UserVariableBase : CachedVariables.UserVariables)
								{
									FNiagaraVariable UserVariable(UserVariableBase);
									const uint8* ParameterData = ParameterStore->GetParameterData(UserVariable);
									if (ParameterData != nullptr)
									{
										UserVariable.SetData(ParameterData);

										StringBuilder.Append(UserVariable.GetName().ToString());
										StringBuilder.Append(TEXT(" = "));
										StringBuilder.Append(*UserVariable.ToString());
										StringBuilder.Append(TEXT("\n"));
									}
								}
							}
						}

						// Append particle data if we don't show them in world
						if (GShowParticlesInWorld == false )
						{
							for (int32 iEmitter = 0; iEmitter < CachedVariables.ParticleVariables.Num(); ++iEmitter)
							{
								if (CachedVariables.ParticleVariables[iEmitter].Num() == 0)
								{
									continue;
								}

								auto EmitterInstance = SystemInstance->GetEmitters()[iEmitter];
								FNiagaraDataBuffer* DataBuffer = EmitterInstance->GetData().GetCurrentData();
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
					//-TODO: Put a reason why here
					StringBuilder.Appendf(TEXT("Deactivated by Scalability\n"));
				}

				const TCHAR* FinalString = StringBuilder.ToString();
				const FVector2D StringSize = GetStringSize(WorldFont, FinalString);
				DrawCanvas->DrawTile(ScreenLocation.X - 1.0f, ScreenLocation.Y - 1.0f, StringSize.X + 2.0f, StringSize.Y + 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, BackgroundColor);
				DrawCanvas->DrawShadowedString(ScreenLocation.X, ScreenLocation.Y, FinalString, WorldFont, FLinearColor::White);
			}
		}
	}
}
