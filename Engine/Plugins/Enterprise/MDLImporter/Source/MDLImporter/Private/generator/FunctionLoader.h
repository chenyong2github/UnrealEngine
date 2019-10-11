// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Materials/MaterialFunction.h"

class UMaterialFunctionFactoryNew;

namespace Generator
{
	class FFunctionGenerator;

	enum class ECommonFunction
	{
		MakeFloat2 = 0,
		MakeFloat3,
		MakeFloat4,
		DitherTemporalAA,
		AdjustNormal,
		AngularDirection,
		ColorMap,
		GrayscaleMap,
		NormalMap,
		CarColorTable,
		CarFlakes,
		EstimateObjectThickness,
		VolumeAbsorptionColor,
		TranslucentOpacity,
		Count
	};

	class FFunctionLoader : FNoncopyable
	{
	public:
		FFunctionLoader();
		~FFunctionLoader();

		UMaterialFunction* Load(const FString& AssetName, int32 ArraySize = 0);
		UMaterialFunction* Load(const FString& AssetPath, const FString& AssetName, int32 ArraySize = 0);
		UMaterialFunction& Get(ECommonFunction Function);

		void SetAssetPath(const FString& FunctionsAssetPath);

	private:
		int32              GetVersion(const FString& AssetName) const;
		UMaterialFunction* Generate(const FString& AssetPath, const FString& AssetName, int32 ArraySize);

	private:
		struct FGenerationData
		{
			void (FFunctionGenerator::*Generator)(UMaterialFunction*, int32 ArraySize);
			int32 Version;
		};

		UMaterialFunctionFactoryNew*      FunctionFactory;
		TUniquePtr<FFunctionGenerator>    FunctionGenerator;
		TMap<FString, FGenerationData>    FunctionGenerateMap;
		TMap<FString, UMaterialFunction*> LoadedFunctions;
		TArray<UMaterialFunction*>        CommonFunctions;
		FString                           FunctionsAssetPath;
	};

	inline UMaterialFunction& FFunctionLoader::Get(ECommonFunction Function)
	{
		check(CommonFunctions.Num() > (int)Function);
		check(CommonFunctions[(int)Function] != nullptr);
		return *CommonFunctions[(int)Function];
	}

	inline UMaterialFunction* FFunctionLoader::Load(const FString& AssetName, int32 ArraySize)
	{
		return Load(FunctionsAssetPath, AssetName, ArraySize);
	}

	inline void FFunctionLoader::SetAssetPath(const FString& InFunctionsAssetPath)
	{
		FunctionsAssetPath = InFunctionsAssetPath;
	}
}  // namespace Generator
