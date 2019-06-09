// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"

#include "Algo/Accumulate.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "MeshTypes.h"
#include "UObject/ObjectMacros.h"
#include "USDImporter.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

namespace USDUtils
{
	template<typename T>
	T* FindOrCreateObject(UObject* InParent, const FString& InName, EObjectFlags Flags)
	{
		T* Object = FindObject<T>(InParent, *InName);

		if (!Object)
		{
			Object = NewObject<T>(InParent, FName(*InName), Flags);
		}

		return Object;
	}

	template<typename T>
	static T GetUSDValue(const pxr::UsdAttribute& Attribute)
	{
		T Value = T();
		if(Attribute)
		{
			Attribute.Get(&Value);
		}

		return Value;
	}
}

namespace USDToUnreal
{
	static pxr::TfToken GetUSDStageAxis(pxr::UsdStageRefPtr Stage)
	{
		if (Stage->HasAuthoredMetadata(pxr::UsdGeomTokens->upAxis))
		{
			pxr::TfToken Axis;
			Stage->GetMetadata(pxr::UsdGeomTokens->upAxis, &Axis);
			return Axis;
		}

		return pxr::UsdGeomTokens->z;
	}

	static FString ConvertString(const std::string& InString)
	{
		return FString(ANSI_TO_TCHAR(InString.c_str()));
	}

	static FString ConvertString(const char* InString)
	{
		return FString(ANSI_TO_TCHAR(InString));
	}

	static FName ConvertName(const char* InString)
	{
		return FName(InString);
	}

	static FName ConvertName(const std::string& InString)
	{
		return FName(InString.c_str());
	}

	static FLinearColor ConvertColor(const pxr::GfVec3f& InValue)
	{
		return FLinearColor(InValue[0], InValue[1], InValue[2]);
	}

	static FVector ConvertVector(const pxr::GfVec3f& InValue)
	{
		return FVector(InValue[0], InValue[1], InValue[2]);
	}

	static FVector ConvertVector(const pxr::UsdStageRefPtr& Stage, const pxr::GfVec3f& InValue)
	{
		FVector Value = ConvertVector(InValue);

		pxr::TfToken UpAxisValue = GetUSDStageAxis(Stage);
		const bool bIsZUp = ( UpAxisValue == pxr::UsdGeomTokens->z );

		if ( bIsZUp )
		{
			Value.Y = -Value.Y;
		}
		else
		{
			Swap( Value.Y, Value.Z );
		}

		return Value;
	}

	static FTransform ConvertTransform(bool bZUp, FTransform Transform)
	{
		// Translate
		FVector Translate = Transform.GetTranslation();

		if (bZUp)
		{
			Translate.Y = -Translate.Y;
		}
		else
		{
			Swap(Translate.Y, Translate.Z);
		}

		Transform.SetTranslation(Translate);

		FQuat Rotation = Transform.GetRotation();

		if (bZUp)
		{
			Rotation.X = -Rotation.X;
			Rotation.Z = -Rotation.Z;
		}
		else
		{
			Rotation = Rotation.Inverse();
			Swap(Rotation.Y, Rotation.Z);
		}

		Transform.SetRotation(Rotation);

		if(!bZUp)
		{
			FVector Scale = Transform.GetScale3D();
			Swap(Scale.Y, Scale.Z);
			Transform.SetScale3D(Scale);
		}

		return Transform;
	}

	static FMatrix ConvertMatrix(const pxr::GfMatrix4d& Matrix)
	{
		FMatrix UnrealMtx(
			FPlane(Matrix[0][0], Matrix[0][1], Matrix[0][2], Matrix[0][3]),
			FPlane(Matrix[1][0], Matrix[1][1], Matrix[1][2], Matrix[1][3]),
			FPlane(Matrix[2][0], Matrix[2][1], Matrix[2][2], Matrix[2][3]),
			FPlane(Matrix[3][0], Matrix[3][1], Matrix[3][2], Matrix[3][3])
		);

		return UnrealMtx;
	}

	static FTransform ConvertMatrix(const pxr::UsdStageRefPtr& Stage, const pxr::GfMatrix4d& InMatrix)
	{
		FMatrix Matrix = ConvertMatrix(InMatrix);
		FTransform Transform(Matrix);

		pxr::TfToken UpAxisValue = GetUSDStageAxis(Stage);

		return ConvertTransform( UpAxisValue == pxr::UsdGeomTokens->z, Transform );
	}
}

#endif // #if USE_USD_SDK

