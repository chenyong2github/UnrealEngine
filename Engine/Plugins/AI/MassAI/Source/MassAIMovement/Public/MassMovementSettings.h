// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassAIMovementTypes.h"
#include "MassMovementSettings.generated.h"

UCLASS(config = Mass, defaultconfig, meta = (DisplayName = "Mass Movement"))
class MASSAIMOVEMENT_API UMassMovementSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMassMovementSettings* Get()
	{
		return GetDefault<UMassMovementSettings>();
	}

	TConstArrayView<FMassMovementStyle> GetMovementStyles() const { return MovementStyles; }
	const FMassMovementStyle* GetMovementStyleByID(const FGuid ID) const; 
	
	TConstArrayView<FMassMovementConfig> GetMovementConfigs() const { return MovementConfigs; } 
	const FMassMovementConfig* GetMovementConfigByID(const FGuid ID) const;
	FMassMovementConfigHandle GetMovementConfigHandleByID(const FGuid ID) const;
	const FMassMovementConfig* GetMovementConfigByHandle(const FMassMovementConfigHandle Handle) const { return Handle.IsValid() ? &MovementConfigs[Handle.Index] : nullptr; } 

private:

	void UpdateConfigs();
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, config, Category = Movement);
    TArray<FMassMovementStyle> MovementStyles;

	UPROPERTY(EditAnywhere, config, Category = Movement);
	TArray<FMassMovementConfig> MovementConfigs;
};
