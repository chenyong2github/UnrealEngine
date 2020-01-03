// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK
#include "USDConversionUtils.h"
#include "USDMemory.h"

#include "Containers/StringConv.h"

#include <string>

#include "USDIncludesStart.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

namespace UsdToUnreal
{
	static FString ConvertString( const std::string& InString )
	{
		return FString( ANSI_TO_TCHAR( InString.c_str() ) );
	}

	static FString ConvertString( std::string&& InString )
	{
		TUsdStore< std::string > UsdString( MoveTemp( InString ) ); // Store the temporary so that it gets destroyed with the USD allocator

		return FString( ANSI_TO_TCHAR( UsdString.Get().c_str() ) );
	}

	static FString ConvertString( const char* InString )
	{
		return FString( ANSI_TO_TCHAR( InString ) );
	}

	static FString ConvertPath( const pxr::SdfPath& Path )
	{
		return ConvertString( Path.GetString().c_str() );
	}

	static FName ConvertName( const char* InString )
	{
		return FName( InString );
	}

	static FName ConvertName( const std::string& InString )
	{
		return FName( InString.c_str() );
	}

	static FName ConvertName( std::string&& InString )
	{
		TUsdStore< std::string > UsdString( MoveTemp( InString ) ); // Store the temporary so that it gets destroyed with the USD allocator

		return FName( UsdString.Get().c_str() );
	}

	static FLinearColor ConvertColor( const pxr::GfVec3f& InValue )
	{
		return FLinearColor( InValue[0], InValue[1], InValue[2] );
	}

	static FLinearColor ConvertColor( const pxr::GfVec4f& InValue )
	{
		return FLinearColor( InValue[0], InValue[1], InValue[2], InValue[3] );
	}

	static FVector2D ConvertVector( const pxr::GfVec2f& InValue )
	{
		return FVector2D( InValue[0], InValue[1] );
	}

	static FVector ConvertVector( const pxr::GfVec3f& InValue )
	{
		return FVector( InValue[0], InValue[1], InValue[2] );
	}

	static FVector ConvertVector( const pxr::UsdStageRefPtr& Stage, const pxr::GfVec3f& InValue )
	{
		FVector Value = ConvertVector( InValue );

		pxr::TfToken UpAxisValue = UsdUtils::GetUsdStageAxis( Stage );
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

	static FTransform ConvertTransform( bool bZUp, FTransform Transform )
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

	static FMatrix ConvertMatrix( const pxr::GfMatrix4d& Matrix )
	{
		FMatrix UnrealMatrix(
			FPlane(Matrix[0][0], Matrix[0][1], Matrix[0][2], Matrix[0][3]),
			FPlane(Matrix[1][0], Matrix[1][1], Matrix[1][2], Matrix[1][3]),
			FPlane(Matrix[2][0], Matrix[2][1], Matrix[2][2], Matrix[2][3]),
			FPlane(Matrix[3][0], Matrix[3][1], Matrix[3][2], Matrix[3][3])
		);

		return UnrealMatrix;
	}

	static FTransform ConvertMatrix( const pxr::TfToken& UpAxis, const pxr::GfMatrix4d& InMatrix )
	{
		FMatrix Matrix = ConvertMatrix( InMatrix );
		FTransform Transform( Matrix );

		Transform = ConvertTransform( UpAxis == pxr::UsdGeomTokens->z, Transform );

		return Transform;
	}

	static FTransform ConvertMatrix( const pxr::UsdStageRefPtr& Stage, const pxr::GfMatrix4d& InMatrix )
	{
		pxr::TfToken UpAxis = UsdUtils::GetUsdStageAxis(Stage);

		return ConvertMatrix( UpAxis, InMatrix );
	}
}

namespace UnrealToUsd
{
	static TUsdStore< std::string > ConvertString( const TCHAR* InString )
	{
		return MakeUsdStore< std::string >( TCHAR_TO_ANSI( InString ) );
	}

	static TUsdStore< pxr::SdfPath > ConvertPath( const TCHAR* InString )
	{
		return MakeUsdStore< pxr::SdfPath >( TCHAR_TO_ANSI( InString ) );
	}

	static TUsdStore< std::string > ConvertName( const FName& InName )
	{
		return MakeUsdStore< std::string >( TCHAR_TO_ANSI( *InName.ToString() ) );
	}

	static pxr::GfVec2f ConvertVector( const FVector2D& InValue )
	{
		return pxr::GfVec2f( InValue[0], InValue[1] );
	}

	static pxr::GfVec3f ConvertVector( const FVector& InValue )
	{
		return pxr::GfVec3f( InValue[0], InValue[1], InValue[2] );
	}

	static pxr::GfVec3f ConvertVector( const pxr::UsdStageRefPtr& Stage, const FVector& InValue )
	{
		pxr::GfVec3f Value = ConvertVector( InValue );

		pxr::TfToken UpAxisValue = UsdUtils::GetUsdStageAxis( Stage );
		const bool bIsZUp = ( UpAxisValue == pxr::UsdGeomTokens->z );

		if ( bIsZUp )
		{
			Value[1] = -Value[1];
		}
		else
		{
			Swap( Value[1], Value[2] );
		}

		return Value;
	}

	inline pxr::GfMatrix4d ConvertMatrix( const FMatrix& Matrix )
	{
		pxr::GfMatrix4d UsdMatrix(
			Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2], Matrix.M[0][3],
			Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2], Matrix.M[1][3],
			Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2], Matrix.M[2][3],
			Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2], Matrix.M[3][3]
		);

		return UsdMatrix;
	}

	inline pxr::GfMatrix4d ConvertTransform( const pxr::UsdStageRefPtr& Stage, const FTransform& Transform )
	{
		pxr::TfToken UpAxisValue = UsdUtils::GetUsdStageAxis( Stage );

		FTransform TransformInUsdSpace = UsdToUnreal::ConvertTransform( UpAxisValue == pxr::UsdGeomTokens->z, Transform );

		return ConvertMatrix( TransformInUsdSpace.ToMatrixWithScale() );
	}
}

#endif // #if USE_USD_SDK
