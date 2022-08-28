// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSkySphereConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFActorUtility.h"
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

	float SunRadius;
	if (SkyMaterial != nullptr && SkyMaterial->GetScalarParameterValue(TEXT("Sun Radius"), SunRadius))
	{
		JsonSkySphere.SunRadius = SunRadius;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export SunRadius for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float NoisePower1;
	if (SkyMaterial != nullptr && SkyMaterial->GetScalarParameterValue(TEXT("NoisePower1"), NoisePower1))
	{
		JsonSkySphere.NoisePower1 = NoisePower1;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export NoisePower1 for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float NoisePower2;
	if (SkyMaterial != nullptr && SkyMaterial->GetScalarParameterValue(TEXT("NoisePower2"), NoisePower2))
	{
		JsonSkySphere.NoisePower2 = NoisePower2;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export NoisePower2 for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float SunHeight;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Sun height"), SunHeight))
	{
		JsonSkySphere.SunHeight = SunHeight;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export SunHeight for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float SunBrightness;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Sun brightness"), SunBrightness))
	{
		JsonSkySphere.SunBrightness = SunBrightness;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export SunBrightness for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float StarsBrightness;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Stars brightness"), StarsBrightness))
	{
		JsonSkySphere.StarsBrightness = StarsBrightness;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export StarsBrightness for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float CloudSpeed;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Cloud speed"), CloudSpeed))
	{
		JsonSkySphere.CloudSpeed = CloudSpeed;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export CloudSpeed for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float CloudOpacity;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Cloud opacity"), CloudOpacity))
	{
		JsonSkySphere.CloudOpacity = CloudOpacity;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export CloudOpacity for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	float HorizonFalloff;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Horizon Falloff"), HorizonFalloff))
	{
		JsonSkySphere.HorizonFalloff = HorizonFalloff;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export HorizonFalloff for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	bool bColorsDeterminedBySunPosition;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Colors determined by sun position"), bColorsDeterminedBySunPosition))
	{
		JsonSkySphere.bColorsDeterminedBySunPosition = bColorsDeterminedBySunPosition;
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export bColorsDeterminedBySunPosition for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	FLinearColor ZenithColor;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Zenith Color"), ZenithColor))
	{
		JsonSkySphere.ZenithColor = FGLTFConverterUtility::ConvertColor(ZenithColor);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export ZenithColor for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	FLinearColor HorizonColor;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Horizon color"), HorizonColor))
	{
		JsonSkySphere.HorizonColor = FGLTFConverterUtility::ConvertColor(HorizonColor);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export HorizonColor for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	FLinearColor CloudColor;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Cloud color"), CloudColor))
	{
		JsonSkySphere.CloudColor = FGLTFConverterUtility::ConvertColor(CloudColor);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export CloudColor for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	FLinearColor OverallColor;
	if (FGLTFActorUtility::TryGetPropertyValue(SkySphereActor, TEXT("Overall Color"), OverallColor))
	{
		JsonSkySphere.OverallColor = FGLTFConverterUtility::ConvertColor(OverallColor);
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export OverallColor for Sky Sphere %s"), *SkySphereActor->GetName()));
	}

	// TODO: export color curves

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
