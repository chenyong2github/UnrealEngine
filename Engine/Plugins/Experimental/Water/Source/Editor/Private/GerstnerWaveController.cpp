// Copyright Epic Games, Inc. All Rights Reserved.

#include "GerstnerWaveController.h"
#include "GeneratedCodeHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "WaterBodyActor.h"
#include "WaterEditorModule.h"


AGerstnerWaveController_C::AGerstnerWaveController_C(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, QualitySetting(EWaterQualityLevel::Epic)
	, MinWavelength(512.0f)
	, MaxWavelength(8000.0f)
	, WavelengthFalloff(3.5f)
	, MinAmplitude(1.0f)
	, MaxAmplitude(256.0f)
	, AmplitudeFalloff(8.0f)
	, WindDirection(FVector2D(1.0f, 0.0f))
	, DirectionAngularSpread(3452.0f)
	, DetailNormalIntensity(0.0f)
	, DetailNormalScale(512.0f)
	, InscatterBrightness(4.0f)
	, InscatterContrast(3.0f)
	, Randomness(0.0f)
	, SmallWaveSteepness(0.2f)
	, MID(nullptr)
	, DisplayWaves(32)
	, DetailNormal(nullptr)
	, EnableOverrides(false)
	, LargeWaveSteepness(0.3f)
	, SteepnessFalloff(0.875f)
	, TargetDepthForFullWaves(512.0f)
	, MaxWavesPerWaterBody(32)
{
	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	WaterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterMesh"));
	RootComponent = DefaultSceneRoot;
	DefaultSceneRoot->CreationMethod = EComponentCreationMethod::Native;
	DefaultSceneRoot->Mobility = EComponentMobility::Type::Static;

	WaterMesh->CreationMethod = EComponentCreationMethod::Native;
	WaterMesh->AttachToComponent(DefaultSceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
	WaterMesh->CastShadow = false;
	WaterMesh->bAffectDistanceFieldLighting = false;
	WaterMesh->SetCollisionProfileName(FName(TEXT("Custom")));
	WaterMesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));
	WaterMesh->Mobility = EComponentMobility::Type::Static;

	Seed = FRandomStream(13290);

	LakeSpectrum.MinAmplitude = 2.0f;
	LakeSpectrum.DirectionAngularSpread = 456123.0f;
	LakeSpectrum.SmallWaveSteepness = 0.0f;
	LakeSpectrum.LargeWaveSteepness = 0.0f;
	LakeSpectrum.SteepnessFalloff = 0.0f;
	NullSpectrum.MinAmplitude = 2.0f;
	NullSpectrum.DirectionAngularSpread = 456987.0f;
	NullSpectrum.SmallWaveSteepness = 0.0f;
	NullSpectrum.LargeWaveSteepness = 0.0f;
	NullSpectrum.SteepnessFalloff = 0.0f;

	PrimaryActorTick.bCanEverTick = true;

	Waves.Reserve(20);

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.W = 8000.0f;
		NewWaves.A = 256.0f;
		NewWaves.S = 0.3f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.864404, -0.502799);
		NewWaves.W = 6486.000977f;
		NewWaves.A = 153.163467f;
		NewWaves.S = 0.291161f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.866268, -0.499580);
		NewWaves.W = 5204.389160f;
		NewWaves.A = 88.620270f;
		NewWaves.S = 0.283790f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.214950, -0.976625);
		NewWaves.W = 4132.325195f;
		NewWaves.A = 49.431416f;
		NewWaves.S = 0.276886f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.646662, -0.762776);
		NewWaves.W = 3247.774414f;
		NewWaves.A = 26.528793f;
		NewWaves.S = 0.270270f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.999791, -0.020435);
		NewWaves.W = 2529.531250f;
		NewWaves.A = 13.726875f;
		NewWaves.S = 0.263860f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.836165, -0.548479);
		NewWaves.W = 1957.259644f;
		NewWaves.A = 6.937181f;
		NewWaves.S = 0.257609f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.973289, -0.229583);
		NewWaves.W = 1511.527344f;
		NewWaves.A = 3.555762f;
		NewWaves.S = 0.251487f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.650696, 0.759338);
		NewWaves.W = 1173.851929f;
		NewWaves.A = 1.996094f;
		NewWaves.S = 0.245475f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.794043, -0.607862);
		NewWaves.W = 926.752502f;
		NewWaves.A = 1.342267f;
		NewWaves.S = 0.239555f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.812916, -0.582381);
		NewWaves.W = 753.810547f;
		NewWaves.A = 1.099722f;
		NewWaves.S = 0.233718f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.716225, -0.697869);
		NewWaves.W = 639.744141f;
		NewWaves.A = 1.023192f;
		NewWaves.S = 0.227953f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.951794, -0.306739);
		NewWaves.W = 570.500000f;
		NewWaves.A = 1.003891f;
		NewWaves.S = 0.222254f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.759164, -0.650900);
		NewWaves.W = 533.373230f;
		NewWaves.A = 1.000390f;
		NewWaves.S = 0.216614f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.136324, -0.990664);
		NewWaves.W = 517.170715f;
		NewWaves.A = 1.000015f;
		NewWaves.S = 0.211027f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.941604, -0.336722);
		NewWaves.W = 512.457031f;
		NewWaves.S = 0.205491f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.324626, -0.945842);
		NewWaves.W = 512.000000f;
		NewWaves.S = 0.200000f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(-0.602902, -0.797815);
		NewWaves.W = 512.000000f;
		NewWaves.S = 0.194552f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.554819, -0.831971);
		NewWaves.W = 512.000000f;
		NewWaves.S = 0.189144f;
	}

	{
		FWaveParams_C& NewWaves = Waves.AddDefaulted_GetRef();
		NewWaves.K.Set(0.479843, 0.877354);
		NewWaves.W = 512.000000f;
		NewWaves.S = 0.183774f;
	}
}

void AGerstnerWaveController_C::SetWavesOnWaterBodies()
{
	TArray<AActor*> WaterBodyActors;
	UGameplayStatics::GetAllActorsOfClass(this, AWaterBody::StaticClass(), WaterBodyActors);

	for (AActor* Actor : WaterBodyActors)
	{
		if (AWaterBody* WaterBody = Cast<AWaterBody>(Actor))
		{
			const EWaterBodyType WaterBodyType = WaterBody->GetWaterBodyType();

			if (WaterBodyType == EWaterBodyType::River)
			{
				WaterBody->WaveParams = NullWaves;
			}
			else if (WaterBodyType == EWaterBodyType::Lake)
			{
				WaterBody->WaveParams = LakeWaves;
			}
			else if (WaterBodyType == EWaterBodyType::Ocean)
			{
				WaterBody->WaveParams = OceanWaves;
			}
		}
	}

	RenderWaveTextureData();
}

void AGerstnerWaveController_C::RenderWaveTextureData()
{
	if (!WaveParamsRT)
	{
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(this, WaveParamsRT, FLinearColor(1000.0f, 0.001f, 0.0f, 1.0f));

	UCanvas* Canvas;
	FVector2D CanvasToRenderTargetSize;
	FDrawToRenderTargetContext RenderTargetContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, WaveParamsRT, Canvas, CanvasToRenderTargetSize, RenderTargetContext);
	check(Canvas);

	TArray<AActor*> WaterBodyActors;
	UGameplayStatics::GetAllActorsOfClass(this, AWaterBody::StaticClass(), WaterBodyActors);

	const FVector2D ScreenSize(0.5f, 0.5f);
	const FVector2D BoxOffset(1.0f, 0.0f);

	for (int32 kk = 0, nk = WaterBodyActors.Num(); kk < nk; ++kk)
	{
		if (AWaterBody* WaterBody = Cast<AWaterBody>(WaterBodyActors[kk]))
		{
			const TArray<FWaterWaveParams>& WaveParamsArray = WaterBody->WaveParams;
			for (int32 ii = 0, ni = FMath::Min(WaveParamsArray.Num(), MaxWavesPerWaterBody - 1); ii < ni; ++ii)
			{
				const FWaterWaveParams& WaveParams = WaveParamsArray[ii];
				const FVector2D ScreenPosition(((float)ii * 2.0f) + 0.5f, (float)kk + 0.5f);

				Canvas->K2_DrawBox(ScreenPosition, ScreenSize, 1.0f, FLinearColor(WaveParams.Direction));
				Canvas->K2_DrawBox(ScreenPosition + BoxOffset, ScreenSize, 1.0f, FLinearColor(WaveParams.Wavelength, WaveParams.Amplitude, WaveParams.Steepness, 0.0f));
			}
		}
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, RenderTargetContext);
}

int32 AGerstnerWaveController_C::GetQualityWaveCount()
{
	static TArray<int32> QualityValues({ 6, 9, 12, 32 });
	return QualityValues[(uint8)QualitySetting];
}

void AGerstnerWaveController_C::GetWaterWaves(TArray<FWaterWaveParams>& WaterWaveParams, float& BigWaveDepth)
{
	int32 NumWaterWaves = WaterWaves.Num();
	int32 Quality = GetQualityWaveCount();

	if (Quality != NumWaterWaves)
	{
		for (int32 ii = 0; ii < Quality; ++ii)
		{
			const FWaveParams_C& WaveParams = Waves[ii];

			FWaterWaveParams& NewWaveParams = WaterWaves.AddDefaulted_GetRef();
			NewWaveParams.Wavelength = WaveParams.W;
			NewWaveParams.Amplitude = WaveParams.A;
			NewWaveParams.Steepness = WaveParams.S;
			NewWaveParams.Direction = FVector(WaveParams.K, 0.0f);
		}
	}

	WaterWaveParams = WaterWaves;
	BigWaveDepth = TargetDepthForFullWaves;
}

void AGerstnerWaveController_C::SetDistanceField(UTexture* InRT)
{
	MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("DF")), InRT);
}

void AGerstnerWaveController_C::SetAllWaveMPCParams()
{
	for (int32 ii = 0, ni = Waves.Num(); ii < ni; ++ii)
	{
		SetSingleWaveMPCParameters(Waves[ii], ii);
	}
}

void AGerstnerWaveController_C::SetSingleWaveMPCParameters(FWaveParams_C const& WaveParams, int32 InIdx)
{
	if (WaterMaterialParameterCollection != nullptr)
	{
		FString KIndexString("k");
		KIndexString.Append(UKismetStringLibrary::Conv_IntToString(InIdx));
		FName KIndexName = UKismetStringLibrary::Conv_StringToName(KIndexString);
		UKismetMaterialLibrary::SetVectorParameterValue(this, WaterMaterialParameterCollection, KIndexName, FLinearColor(WaveParams.K.X, WaveParams.K.Y, 0.0f, 0.0f));

		FString WIndexString("w");
		WIndexString.Append(UKismetStringLibrary::Conv_IntToString(InIdx));
		FName WIndexName = UKismetStringLibrary::Conv_StringToName(WIndexString);
		UKismetMaterialLibrary::SetVectorParameterValue(this, WaterMaterialParameterCollection, WIndexName, FLinearColor(WaveParams.W, WaveParams.A, WaveParams.S, 1.0f));
	}
}

void AGerstnerWaveController_C::HarvestMaterialParams()
{
	for (int32 ii = 0, ni = MID->ScalarParameterValues.Num(); ii < ni; ++ii)
	{
		const FScalarParameterValue& ScalarParameterValue = MID->ScalarParameterValues[ii];

		FOverrideScalarParam_C& Params = OverrideScalarParams.AddDefaulted_GetRef();
		Params.ParamName = ScalarParameterValue.ParameterInfo.Name;
		Params.Value = ScalarParameterValue.ParameterValue;
		Params.Override = false;
	}

	for (int32 ii = 0, ni = MID->VectorParameterValues.Num(); ii < ni; ++ii)
	{
		const FVectorParameterValue& VectorParameterValue = MID->VectorParameterValues[ii];

		FOverrideVectorParam_C& Params = OverrideVectorParams.AddDefaulted_GetRef();
		Params.ParamName = VectorParameterValue.ParameterInfo.Name;
		Params.Value = FVector(VectorParameterValue.ParameterValue);
		Params.Override = false;
	}
}

void AGerstnerWaveController_C::BeginPlay()
{
	ManualParameterOverrides();
	SetAllWaveMPCParams();
	GeneralMPCParams();
	RenderWaveTextureData();

	Super::BeginPlay();
}

void AGerstnerWaveController_C::OnConstruction(const FTransform& Transform)
{
	GenerateSpectrumOld();
	GenerateSpectrum(OceanSpectrum, OceanWaves);
	GenerateSpectrum(LakeSpectrum, LakeWaves);
	GenerateSpectrum(NullSpectrum, NullWaves);
	ManualParameterOverrides();
	SetAllWaveMPCParams();
	GeneralMPCParams();

	if (::IsValid(Material))
	{
		if (!::IsValid(MID) || (MID->GetMaterial() != Material))
		{
			if (MID != nullptr)
			{
				// Rename it so we can allocate a new one with the (fixed) name : 
				MID->Rename(nullptr, this, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			}
			// TODO [jonathan.bard] : make MID transient!
			MID = UMaterialInstanceDynamic::Create(Material, this, TEXT("GerstnerWaveControllerMID"));
			//if (Material != MID)
			//{
			//	MID = UKismetMaterialLibrary::CreateDynamicMaterialInstance(this, Material, NAME_None, EMIDCreationFlags::Transient);
			//}
		}
	}
	else
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid Material passed to GerstnerWaveController"));
	}
}

void AGerstnerWaveController_C::ManualParameterOverrides()
{

	for (const FOverrideScalarParam_C& OverrideScalarParam : OverrideScalarParams)
	{
		if (OverrideScalarParam.Override)
		{
			MID->SetScalarParameterValue(OverrideScalarParam.ParamName, OverrideScalarParam.Value);

			// TODO: ON MPC_Water collection
// 			MID->SetScalarParameterValue(OverrideScalarParam.ParamName, OverrideScalarParam.Value);
		}
	}

	for (const FOverrideVectorParam_C& OverrideVectorParam : OverrideVectorParams)
	{
		if (OverrideVectorParam.Override)
		{
			MID->SetVectorParameterValue(OverrideVectorParam.ParamName, FLinearColor(OverrideVectorParam.Value));

			// TODO: ON MPC_Water collection
			// 			MID->SetScalarParameterValue(OverrideVectorParam.ParamName, OverrideVectorParam.Value);
		}
	}

	if (EnableOverrides)
	{
		for (const FWaveParams_C& WaveParams : OverrideWaves)
		{
			FWaveParams_C NewWaveParams;
			NewWaveParams.K = FVector2D(WaveParams.K.GetSafeNormal());
			NewWaveParams.W = FMath::Max(WaveParams.W, 0.001f);
			NewWaveParams.A = FMath::Max(WaveParams.A, 0.001f);
			NewWaveParams.S = WaveParams.S;
			NewWaveParams.O = WaveParams.O;
			NewWaveParams.OverrideIndex = WaveParams.OverrideIndex;

			SetSingleWaveMPCParameters(NewWaveParams, WaveParams.OverrideIndex);
			FCustomThunkTemplates::Array_Set(Waves, WaveParams.OverrideIndex, NewWaveParams, true);
		}
	}
}

void AGerstnerWaveController_C::GeneralMPCParams()
{
	// TODO [jonathan.bard] : is any of this necessary? 
	if (MID)
	{
		MID->SetScalarParameterValue(FName(TEXT("InscatterBrightness")), InscatterBrightness);
		MID->SetScalarParameterValue(FName(TEXT("InscatterContrast")), InscatterContrast);
		MID->SetTextureParameterValue(FName(TEXT("DetailNormal")), DetailNormal);
		MID->SetScalarParameterValue(FName(TEXT("DetailNormalIntensity")), DetailNormalIntensity);
		MID->SetScalarParameterValue(FName(TEXT("DetailNormalScale")), DetailNormalScale);
	}

	if (WaterMaterialParameterCollection)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, WaterMaterialParameterCollection, FName(TEXT("WaterZ")), GetActorLocation().Z);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WaterMaterialParameterCollection, FName(TEXT("TargetWaveDepth")), TargetDepthForFullWaves);
	}
}

void AGerstnerWaveController_C::GenerateSpectrumOld()
{
	Waves.Empty();

	float LastAlpha = 0.0f;
	int32 Quality = GetQualityWaveCount();

	for (int ii = 0; ii < 20; ++ii)
	{
		LastAlpha = FMath::Clamp(1.f - ((float)ii / (float)Quality) + Seed.FRandRange(Randomness * (-1.0f / (float)Quality), Randomness * (1.0f / (float)Quality)), 0.0f, 1.0f);

		FWaveParams_C& Params = Waves.AddDefaulted_GetRef();
		Params.O = 0.0f;
		Params.OverrideIndex = 0;

		if (ii >= DisplayWaves)
		{
			Params.K.Set(1.0f, 0.0f);
			Params.W = 1024.0f;
			Params.A = 0.00001;
			Params.S = 0.0f;
		}
		else
		{
			if (ii == 0)
			{
				Params.K = WindDirection.GetSafeNormal();
			}
			else
			{
				Params.K = FVector2D(FVector(WindDirection.X, WindDirection.Y, 0.0f).RotateAngleAxis(Seed.FRandRange(-DirectionAngularSpread, DirectionAngularSpread), FVector::UpVector).GetSafeNormal());
			}

			Params.W = FMath::Lerp(MinWavelength, MaxWavelength, FMath::Pow(LastAlpha, WavelengthFalloff));
			Params.A = FMath::Max(FMath::Lerp(MinAmplitude, MaxAmplitude, FMath::Pow(LastAlpha, AmplitudeFalloff)), 0.0001f);
			Params.S = FMath::Lerp(LargeWaveSteepness, SmallWaveSteepness, FMath::Pow((float)ii / Quality, SteepnessFalloff));
		}
	}
}


void AGerstnerWaveController_C::GenerateSpectrum(FWaveSpectrumSettings_C InSpectrum, TArray<FWaterWaveParams>& OutWaves)
{
	FWaveSpectrumSettings_C LocalSpectrum = InSpectrum;
	OutWaves.Empty();
	FRandomStream LocaLSeed(LocalSpectrum.Seed);
	int32 Quality = GetQualityWaveCount();
	float LastAlpha = 0.0f;

	for (int ii = 0; ii < Quality; ++ii)
	{
		LastAlpha = FMath::Clamp(1.f - ((float)ii / (float)Quality) + LocaLSeed.FRandRange(Randomness * (-1.0f / (float)Quality), Randomness * (1.0f / (float)Quality)), 0.0f, 1.0f);

		FWaterWaveParams& Params = OutWaves.AddDefaulted_GetRef();
		if (ii >= DisplayWaves)
		{
			Params.Wavelength = 2000.0f;
			Params.Amplitude = 0.001f;
			Params.Steepness = 0.0f;
			Params.Direction = FVector::ForwardVector;
		}
		else
		{
			if (ii == 0)
			{
				Params.Direction = FVector(LocalSpectrum.WindDirection, 0.0f).GetSafeNormal();
			}
			else
			{
				Params.Direction = FVector(LocalSpectrum.WindDirection, 0.0f);
				Params.Direction = Params.Direction.RotateAngleAxis(LocaLSeed.FRandRange(-LocalSpectrum.DirectionAngularSpread, LocalSpectrum.DirectionAngularSpread), FVector::UpVector);
				Params.Direction.Normalize();
			}

			Params.Wavelength = FMath::Lerp(LocalSpectrum.MinWavelength, LocalSpectrum.MaxWavelength, FMath::Pow(LastAlpha, LocalSpectrum.WavelengthFalloff));
			Params.Amplitude = FMath::Max(FMath::Lerp(LocalSpectrum.MinAmplitude, LocalSpectrum.MaxAmplitude, FMath::Pow(LastAlpha, LocalSpectrum.AmplitudeFalloff)), 0.0001f);
			Params.Steepness = FMath::Lerp(LocalSpectrum.LargeWaveSteepness, LocalSpectrum.SmallWaveSteepness, FMath::Pow((float)ii / Quality, LocalSpectrum.SteepnessFalloff));
		}
	}
}
