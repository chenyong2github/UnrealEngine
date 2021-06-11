// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationSettings.h"

#include "SphericalLensDistortionModelHandler.h"

UCameraCalibrationSettings::UCameraCalibrationSettings()
{
	DefaultUndistortionDisplacementMaterials.Add(USphericalLensDistortionModelHandler::StaticClass(), TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/CameraCalibrationCore/Materials/M_SphericalUndistortionDisplacementMap.M_SphericalUndistortionDisplacementMap"))));
	DefaultDistortionDisplacementMaterials.Add(USphericalLensDistortionModelHandler::StaticClass(), TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/CameraCalibrationCore/Materials/M_SphericalDistortionDisplacementMap.M_SphericalDistortionDisplacementMap"))));
	DefaultDistortionMaterials.Add(USphericalLensDistortionModelHandler::StaticClass(), TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/CameraCalibrationCore/Materials/M_DistortionPostProcess.M_DistortionPostProcess"))));
}

FName UCameraCalibrationSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UCameraCalibrationSettings::GetSectionText() const
{
	return NSLOCTEXT("CameraCalibrationPlugin", "CameraCalibrationSettingsSection", "Camera Calibration");
}

FName UCameraCalibrationSettings::GetSectionName() const
{
	return TEXT("Camera Calibration");
}

#endif

ULensFile* UCameraCalibrationSettings::GetStartupLensFile() const
{
	return StartupLensFile.LoadSynchronous();
}

UMaterialInterface* UCameraCalibrationSettings::GetDefaultUndistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const
{
	const TSoftObjectPtr<UMaterialInterface>* UndistortionDisplacementMaterial = DefaultUndistortionDisplacementMaterials.Find(InModelHandler);
	if (!UndistortionDisplacementMaterial)
	{
		return nullptr;
	}
	return UndistortionDisplacementMaterial->LoadSynchronous();
}

UMaterialInterface* UCameraCalibrationSettings::GetDefaultDistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const
{
	const TSoftObjectPtr<UMaterialInterface>* DistortionDisplacementMaterial = DefaultDistortionDisplacementMaterials.Find(InModelHandler);
	if (!DistortionDisplacementMaterial)
	{
		return nullptr;
	}
	return DistortionDisplacementMaterial->LoadSynchronous();
}

UMaterialInterface* UCameraCalibrationSettings::GetDefaultDistortionMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const
{
	const TSoftObjectPtr<UMaterialInterface>* DistortionMaterial = DefaultDistortionMaterials.Find(InModelHandler);
	if (!DistortionMaterial)
	{
		return nullptr;
	}
	return DistortionMaterial->LoadSynchronous();
}

#if WITH_EDITOR
void UCameraCalibrationSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UCameraCalibrationSettings, DisplacementMapResolution))
	{
		DisplacementMapResolutionChangedDelegate.Broadcast(DisplacementMapResolution);
	}
	else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UCameraCalibrationSettings, CalibrationInputTolerance))
	{
		CalibrationInputToleranceChangedDelegate.Broadcast(CalibrationInputTolerance);
	}
}

FOnDisplacementMapResolutionChanged& UCameraCalibrationSettings::OnDisplacementMapResolutionChanged()
{
	return DisplacementMapResolutionChangedDelegate;
}

FOnCalibrationInputToleranceChanged& UCameraCalibrationSettings::OnCalibrationInputToleranceChanged()
{
	return CalibrationInputToleranceChangedDelegate;
}
#endif

FName UCameraCalibrationEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UCameraCalibrationEditorSettings::GetSectionText() const
{
	return NSLOCTEXT("CameraCalibrationEditorPlugin", "CameraCalibrationEditorSettingsSection", "Camera Calibration Editor");
}

FName UCameraCalibrationEditorSettings::GetSectionName() const
{
	return TEXT("Camera Calibration Editor");
}
#endif

ULensFile* UCameraCalibrationEditorSettings::GetUserLensFile() const
{
#if WITH_EDITOR
	return UserLensFile.LoadSynchronous();
#else
	return nullptr;
#endif
}

void UCameraCalibrationEditorSettings::SetUserLensFile(ULensFile* InLensFile)
{
#if WITH_EDITOR
	UserLensFile = InLensFile;
	SaveConfig();
#endif
}
