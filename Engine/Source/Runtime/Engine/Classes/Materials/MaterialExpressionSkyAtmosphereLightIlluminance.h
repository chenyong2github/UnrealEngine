// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionSkyAtmosphereLightIlluminance.generated.h"

UCLASS()
class UMaterialExpressionSkyAtmosphereLightIlluminance : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** Index of the atmosphere light to sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MaterialExpressionTextureCoordinate, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	int32 LightIndex;

	/** World position of the sample. If not specified, the pixel world position will be used. */
	UPROPERTY()
	FExpressionInput WorldPosition;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface
};



UCLASS()
class UMaterialExpressionSkyAtmosphereLightDiskLuminance : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** Index of the atmosphere light to sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MaterialExpressionTextureCoordinate, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	int32 LightIndex;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface
};



UCLASS()
class UMaterialExpressionSkyAtmosphereAerialPerspective : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** World position of the sample. If not specified, the pixel world position will be used. Larger distance will result in more fog. Please make sure .SkyAtmosphere.AerialPerspectiveLUT.Depth is set far enough to have fog data.
		If you are scaling the sky dome pixel world position, make sure it is centered around the origin.*/
	UPROPERTY()
	FExpressionInput WorldPosition;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface
};



UCLASS()
class UMaterialExpressionSkyAtmosphereDistantLightScatteredLuminance : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface
};


