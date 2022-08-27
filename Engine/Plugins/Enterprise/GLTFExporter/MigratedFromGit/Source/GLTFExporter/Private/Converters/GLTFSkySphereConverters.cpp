// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSkySphereConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Converters/GLTFCurveUtility.h"
#include "Builders/GLTFContainerBuilder.h"

FGLTFJsonSkySphereIndex FGLTFSkySphereConverter::Convert(const AActor* SkySphereActor)
{
	const UBlueprint* Blueprint = FGLTFActorUtility::GetBlueprintFromActor(SkySphereActor);
	if (!FGLTFActorUtility::IsSkySphereBlueprint(Blueprint))
	{
		return FGLTFJsonSkySphereIndex(INDEX_NONE);
	}

	FGLTFJsonSkySphere JsonSkySphere;
	SkySphereActor->GetName(JsonSkySphere.Name);

	const UStaticMeshComponent* StaticMeshComponent = nullptr;
	FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("SkySphereMesh"), StaticMeshComponent);

	if (StaticMeshComponent != nullptr)
	{
		const USceneComponent* ParentComponent = StaticMeshComponent->GetAttachParent();
		const FName SocketName = StaticMeshComponent->GetAttachSocketName();

		const FTransform Transform = StaticMeshComponent->GetComponentTransform();
		const FTransform ParentTransform = ParentComponent->GetSocketTransform(SocketName);
		const FTransform RelativeTransform = Transform.GetRelativeTransform(ParentTransform);

		JsonSkySphere.Scale = FGLTFConverterUtility::ConvertScale(RelativeTransform.GetScale3D());
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export Scale for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent != nullptr ? StaticMeshComponent->GetStaticMesh() : nullptr;
	if (StaticMesh)
	{
		JsonSkySphere.SkySphereMesh = Builder.GetOrAddMesh(StaticMesh, -1, nullptr, { FGLTFMaterialUtility::GetDefault() });
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export SkySphereMesh for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	const UMaterialInstance* SkyMaterial = nullptr;
	FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Sky material"), SkyMaterial);

	if (const UTexture2D* SkyTexture = GetSkyTexture(SkyMaterial))
	{
		JsonSkySphere.SkyTexture = Builder.GetOrAddTexture(SkyTexture);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export SkyTexture for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	if (const UTexture2D* CloudsTexture = GetCloudsTexture(SkyMaterial))
	{
		JsonSkySphere.CloudsTexture = Builder.GetOrAddTexture(CloudsTexture);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export CloudsTexture for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	if (const UTexture2D* StarsTexture = GetStarsTexture(SkyMaterial))
	{
		JsonSkySphere.StarsTexture = Builder.GetOrAddTexture(StarsTexture);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export StarsTexture for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	const ADirectionalLight* DirectionalLight = nullptr;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Directional light actor"), DirectionalLight))
	{
		JsonSkySphere.DirectionalLight = Builder.GetOrAddNode(DirectionalLight);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export DirectionalLight for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	ConvertScalarParameter(*SkySphereActor, SkyMaterial, TEXT("Sun Radius"), TEXT("SunRadius"), JsonSkySphere.SunRadius);
	ConvertScalarParameter(*SkySphereActor, SkyMaterial, TEXT("NoisePower1"), TEXT("NoisePower1"), JsonSkySphere.NoisePower1);
	ConvertScalarParameter(*SkySphereActor, SkyMaterial, TEXT("NoisePower2"), TEXT("NoisePower2"), JsonSkySphere.NoisePower2);

	ConvertProperty(*SkySphereActor, TEXT("Sun height"), TEXT("SunHeight"), JsonSkySphere.SunHeight);
	ConvertProperty(*SkySphereActor, TEXT("Sun brightness"), TEXT("SunBrightness"), JsonSkySphere.SunBrightness);
	ConvertProperty(*SkySphereActor, TEXT("Stars brightness"), TEXT("StarsBrightness"), JsonSkySphere.StarsBrightness);
	ConvertProperty(*SkySphereActor, TEXT("Cloud speed"), TEXT("CloudSpeed"), JsonSkySphere.CloudSpeed);
	ConvertProperty(*SkySphereActor, TEXT("Cloud opacity"), TEXT("CloudOpacity"), JsonSkySphere.CloudOpacity);
	ConvertProperty(*SkySphereActor, TEXT("Horizon Falloff"), TEXT("HorizonFalloff"), JsonSkySphere.HorizonFalloff);
	ConvertProperty(*SkySphereActor, TEXT("Colors determined by sun position"), TEXT("bColorsDeterminedBySunPosition"), JsonSkySphere.bColorsDeterminedBySunPosition);

	ConvertColorProperty(*SkySphereActor, TEXT("Zenith Color"), TEXT("ZenithColor"), JsonSkySphere.ZenithColor);
	ConvertColorProperty(*SkySphereActor, TEXT("Horizon color"), TEXT("HorizonColor"), JsonSkySphere.HorizonColor);
	ConvertColorProperty(*SkySphereActor, TEXT("Cloud color"), TEXT("CloudColor"), JsonSkySphere.CloudColor);
	ConvertColorProperty(*SkySphereActor, TEXT("Overall Color"), TEXT("OverallColor"), JsonSkySphere.OverallColor);

	ConvertColorCurveProperty(*SkySphereActor, TEXT("Zenith color curve"), TEXT("ZenithColorCurve"), JsonSkySphere.ZenithColorCurve);
	ConvertColorCurveProperty(*SkySphereActor, TEXT("Horizon color curve"), TEXT("HorizonColorCurve"), JsonSkySphere.HorizonColorCurve);
	ConvertColorCurveProperty(*SkySphereActor, TEXT("Cloud color curve"), TEXT("CloudColorCurve"), JsonSkySphere.CloudColorCurve);

	return Builder.AddSkySphere(JsonSkySphere);
}

const UTexture2D* FGLTFSkySphereConverter::GetSkyTexture(const UMaterialInstance* SkyMaterial) const
{
	// TODO: the texture isn't exposed as a parameter on the default sky material. We should try to find
	// a way of extracting and returning the correct texture from the material anyway.

	static UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineSky/T_Sky_Blue.T_Sky_Blue"));
	return Texture;
}

const UTexture2D* FGLTFSkySphereConverter::GetCloudsTexture(const UMaterialInstance* SkyMaterial) const
{
	// TODO: the texture isn't exposed as a parameter on the default sky material. We should try to find
	// a way of extracting and returning the correct texture from the material anyway.

	static UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineSky/T_Sky_Clouds_M.T_Sky_Clouds_M"));
	return Texture;
}

const UTexture2D* FGLTFSkySphereConverter::GetStarsTexture(const UMaterialInstance* SkyMaterial) const
{
	// TODO: the texture isn't exposed as a parameter on the default sky material. We should try to find
	// a way of extracting and returning the correct texture from the material anyway.

	static UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineSky/T_Sky_Stars.T_Sky_Stars"));
	return Texture;
}

template <class ValueType>
void FGLTFSkySphereConverter::ConvertProperty(const AActor& Actor, const TCHAR* PropertyName, const TCHAR* ExportedPropertyName, ValueType& OutValue) const
{
	if (!FGLTFActorUtility::TryGetPropertyValue(&Actor, PropertyName, OutValue))
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to export %s for Sky Sphere %s"),
			ExportedPropertyName,
			*Actor.GetName()));
	}
}

void FGLTFSkySphereConverter::ConvertColorProperty(const AActor& Actor, const TCHAR* PropertyName, const TCHAR* ExportedPropertyName, FGLTFJsonColor4& OutValue) const
{
	FLinearColor LinearColor;
	if (FGLTFActorUtility::TryGetPropertyValue(&Actor, PropertyName, LinearColor))
	{
		OutValue = FGLTFConverterUtility::ConvertColor(LinearColor);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to export %s for Sky Sphere %s"),
			ExportedPropertyName,
			*Actor.GetName()));
	}
}

void FGLTFSkySphereConverter::ConvertColorCurveProperty(const AActor& Actor, const TCHAR* PropertyName, const TCHAR* ExportedPropertyName, FGLTFJsonSkySphereColorCurve& OutValue) const
{
	const UCurveLinearColor* ColorCurve = nullptr;
	FGLTFActorUtility::TryGetPropertyValue(&Actor, PropertyName, ColorCurve);

	if (ColorCurve != nullptr)
	{
		if (FGLTFCurveUtility::HasAnyAdjustment(*ColorCurve))
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Adjustments for %s in Sky Sphere %s are not supported"),
				ExportedPropertyName,
				*Actor.GetName()));
		}

		OutValue.ComponentCurves.AddDefaulted(3);

		for (uint32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
		{
			const FRichCurve& FloatCurve = ColorCurve->FloatCurves[ComponentIndex];
			const uint32 KeyCount = FloatCurve.Keys.Num();

			TArray<FGLTFJsonSkySphereColorCurve::FKey>& ComponentKeys = OutValue.ComponentCurves[ComponentIndex].Keys;
			ComponentKeys.AddUninitialized(KeyCount);

			uint32 KeyIndex = 0;

			for (const FRichCurveKey& Key: FloatCurve.Keys)
			{
				ComponentKeys[KeyIndex++] = { Key.Time, Key.Value };
			}
		}
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to export %s for Sky Sphere %s"),
			ExportedPropertyName,
			*Actor.GetName()));
	}
}

void FGLTFSkySphereConverter::ConvertScalarParameter(const AActor& Actor, const UMaterialInstance* Material, const TCHAR* ParameterName, const TCHAR* ExportedPropertyName, float& OutValue) const
{
	if (Material == nullptr || !Material->GetScalarParameterValue(ParameterName, OutValue))
	{
		Builder.AddWarningMessage(FString::Printf(
			TEXT("Failed to export %s for Sky Sphere %s"),
			ExportedPropertyName,
			*Actor.GetName()));
	}
}
