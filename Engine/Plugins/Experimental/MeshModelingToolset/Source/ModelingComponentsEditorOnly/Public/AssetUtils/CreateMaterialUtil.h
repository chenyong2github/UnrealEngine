// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "Image/ImageDimensions.h"
#include "Image/ImageBuilder.h"

class UMaterial;
class UMaterialInterface;

namespace UE
{
namespace AssetUtils
{
	/**
	 * Result enum returned by Create() functions below to indicate succes/error conditions
	 */
	enum class ECreateMaterialResult
	{
		Ok = 0,
		InvalidPackage = 1,
		InvalidBaseMaterial = 2,
		NameError = 3,
		DuplicateFailed = 4,

		UnknownError = 100
	};

	/**
	 * Options for new UMaterial asset created by Create() functions below.
	 */
	struct MODELINGCOMPONENTSEDITORONLY_API FMaterialAssetOptions
	{
		// this package will be used if it is not null, otherwise a new package will be created at NewAssetPath
		UPackage* UsePackage = nullptr;
		FString NewAssetPath;

		bool bDeferPostEditChange = false;
	};

	/**
	 * Output information about a newly-created UMaterial, returned by Create functions below.
	 * Some fields may be null, if the relevant function did not create that type of object
	 */
	struct MODELINGCOMPONENTSEDITORONLY_API FMaterialAssetResults
	{
		UMaterial* NewMaterial = nullptr;
	};

	/**
	 * Create a new UMaterial Asset by duplicating the parent UMaterial of BaseMaterial
	 * @param Options defines configuration for the new UMaterial Asset
	 * @param ResultOut new UMaterial is returned here
	 * @return Ok, or error flag if some part of the creation process failed. On failure, no Asset is created.
	 */
	MODELINGCOMPONENTSEDITORONLY_API ECreateMaterialResult CreateDuplicateMaterial(
		UMaterialInterface* BaseMaterial,
		FMaterialAssetOptions& Options,
		FMaterialAssetResults& ResultsOut);

}  // end namespace UE
}  // end namespace AssetUtils

