// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"


#include "NiagaraEditorDataBase.generated.h"

USTRUCT()
struct FNiagaraGraphViewSettings
{
	GENERATED_BODY()
public:
	FNiagaraGraphViewSettings()
		: Location(FVector2D::ZeroVector)
		, Zoom(0.0f)
		, bIsValid(false)
	{
	}

	FNiagaraGraphViewSettings(const FVector2D& InLocation, float InZoom)
		: Location(InLocation)
		, Zoom(InZoom)
		, bIsValid(true)
	{
	}

	const FVector2D& GetLocation() const { return Location; }
	float GetZoom() const { return Zoom; }
	bool IsValid() const { return bIsValid; }

private:
	UPROPERTY()
	FVector2D Location;

	UPROPERTY()
	float Zoom;

	UPROPERTY()
	bool bIsValid;
};

/** A base class for editor only data which supports post loading from the runtime owner object. */
UCLASS(MinimalAPI)
class UNiagaraEditorDataBase : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITORONLY_DATA
	virtual void PostLoadFromOwner(UObject* InOwner) PURE_VIRTUAL(UNiagaraEditorDataBase::PostLoadFromOwner, );
#endif
};

