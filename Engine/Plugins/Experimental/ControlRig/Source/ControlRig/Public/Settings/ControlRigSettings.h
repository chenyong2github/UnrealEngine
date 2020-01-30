// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ControlRigSettings.h: Declares the ControlRigSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ControlRigGizmoLibrary.h"
#include "ControlRigSettings.generated.h"

class UStaticMesh;

/**
 * Default ControlRig settings.
 */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Control Rig"))
class CONTROLRIG_API UControlRigSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, config, Category = DefaultGizmo)
	TAssetPtr<UControlRigGizmoLibrary> DefaultGizmoLibrary;
#endif

	static UControlRigSettings * Get() { return CastChecked<UControlRigSettings>(UControlRigSettings::StaticClass()->GetDefaultObject()); }
};
