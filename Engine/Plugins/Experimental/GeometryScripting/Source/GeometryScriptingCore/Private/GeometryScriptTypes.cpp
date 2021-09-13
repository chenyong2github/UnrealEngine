// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryBase.h"



FGeometryScriptDebugMessage UE::Geometry::MakeScriptError(EGeometryScriptErrorType ErrorTypeIn, const FText& MessageIn)
{
	UE_LOG(LogGeometry, Warning, TEXT("GeometryScriptError: %s"), *MessageIn.ToString() );

	return FGeometryScriptDebugMessage{ EGeometryScriptDebugMessageType::ErrorMessage, ErrorTypeIn, MessageIn };
}


void UE::Geometry::AppendError(UGeometryScriptDebug* Debug, EGeometryScriptErrorType ErrorTypeIn, const FText& MessageIn)
{
	FGeometryScriptDebugMessage Result = MakeScriptError(ErrorTypeIn, MessageIn);
	if (Debug != nullptr)
	{
		Debug->Append(Result);
	}
}