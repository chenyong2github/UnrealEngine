// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DataTable.h"
#include "UObject/NameTypes.h"
#include "DMXProtocolTypes.h"
#include "DMXAttribute.h"
#include "DMXInterpolation.h"
#include "DMXFixtureComponent.generated.h"


USTRUCT(BlueprintType)
struct FDMXChannelData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "DMX Channel")
	FDMXAttributeName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channel")
	float MinValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channel")
	float MaxValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Channel")
	float DefaultValue;

	FDMXChannelData()
	{
		MinValue = 0.0f;
		MaxValue = 1.0f;
		DefaultValue = 0.0f;
	}
};


UCLASS(meta=(IsBlueprintBase=false))
class DMXFIXTURES_API UDMXFixtureComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	virtual void OnComponentCreated() override;

public:	
	UDMXFixtureComponent();

	// A cell represent one "lens" in a light fixture
	// i.e.: Single light fixture contains one cell but Matrix fixtures contain multiple cells
	// Also, a cell can have multiple channels (single, double)
	TArray<FCell> Cells;
	FCell* CurrentCell;

	// Parameters---------------------------------------
	UPROPERTY(EditAnywhere, Category = "DMX Parameters", meta=(DisplayPriority = 0))
	bool IsEnabled;

	UPROPERTY(EditAnywhere, Category = "DMX Parameters", meta = (DisplayPriority = 1))
	bool UsingMatrixData;

	UPROPERTY(EditAnywhere, Category = "DMX Parameters")
	float SkipThreshold;

	UPROPERTY(EditAnywhere, Category = "DMX Parameters")
	bool UseInterpolation;

	UPROPERTY(EditAnywhere, Category = "DMX Parameters")
	float InterpolationScale;

	// Functions-----------------------------------------
	UFUNCTION(BlueprintCallable, Category = "DMX")
	class ADMXFixtureActor* GetParentFixtureActor();

	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<FLinearColor> GetTextureCenterColors(UTexture2D* TextureAtlas, int numTextures);

	// Blueprint event
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX")
	void InterpolateComponent(float DeltaSeconds);

	// Blueprint event
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "DMX")
	void InitializeComponent();

	void Initialize();
	void ApplySpeedScale();

	// override me
	virtual void SetBitResolution(TMap<FDMXAttributeName, EDMXFixtureSignalFormat> map) {};
	virtual void SetRangeValue() {};
	virtual void InitCells(int NCells);
	virtual void SetCurrentCell(int Index);
};

