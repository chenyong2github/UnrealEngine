// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionSettings.h"

#include "SphericalLensDistortionModelHandler.h"

ULensDistortionSettings::ULensDistortionSettings()
{
	DefaultDisplacementMaterials.Add(USphericalLensDistortionModelHandler::StaticClass(), TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/LensDistortion/Materials/M_SphericalDistortionDisplacementMap.M_SphericalDistortionDisplacementMap"))));
	DefaultDistortionMaterials.Add(USphericalLensDistortionModelHandler::StaticClass(), TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/LensDistortion/Materials/M_DistortionPostProcess.M_DistortionPostProcess"))));
}

FName ULensDistortionSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText ULensDistortionSettings::GetSectionText() const
{
	return NSLOCTEXT("LensDistortionPlugin", "LensDistortionSettingsSection", "Lens Distortion");
}

#endif

ULensFile* ULensDistortionSettings::GetStartupLensFile() const
{
	return StartupLensFile.LoadSynchronous();
}

UMaterialInterface* ULensDistortionSettings::GetDefaultDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const
{
	const TSoftObjectPtr<UMaterialInterface>* DisplacementMaterial = DefaultDisplacementMaterials.Find(InModelHandler);
	if (!DisplacementMaterial)
	{
		return nullptr;
	}
	return DisplacementMaterial->LoadSynchronous();
}

UMaterialInterface* ULensDistortionSettings::GetDefaultDistortionMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const
{
	const TSoftObjectPtr<UMaterialInterface>* DistortionMaterial = DefaultDistortionMaterials.Find(InModelHandler);
	if (!DistortionMaterial)
	{
		return nullptr;
	}
	return DistortionMaterial->LoadSynchronous();
}

FName ULensDistortionEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText ULensDistortionEditorSettings::GetSectionText() const
{
	return NSLOCTEXT("LensDistortionEditorPlugin", "LensDistortionEditorSettingsSection", "Lens Distortion");
}
#endif

ULensFile* ULensDistortionEditorSettings::GetUserLensFile() const
{
#if WITH_EDITOR
	return UserLensFile.LoadSynchronous();
#else
	return nullptr;
#endif
}

void ULensDistortionEditorSettings::SetUserLensFile(ULensFile* InLensFile)
{
#if WITH_EDITOR
	UserLensFile = InLensFile;
	SaveConfig();
#endif
}
