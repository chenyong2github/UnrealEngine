// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshPaintSettings.h"

#include "Misc/ConfigCacheIni.h"

UPaintBrushSettings::UPaintBrushSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	BrushRadius(128.0f),
	BrushStrength(0.5f),
	BrushFalloffAmount(0.5f),
	bEnableFlow(true),
	bOnlyFrontFacingTriangles(true),
	ColorViewMode(EMeshPaintColorViewMode::Normal)	
{
	BrushRadiusMin = 0.01f, BrushRadiusMax = 250000.0f;

	GConfig->GetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushRadius"), BrushRadius, GEditorPerProjectIni);
	BrushRadius = FMath::Clamp(BrushRadius, BrushRadiusMin, BrushRadiusMax);

	GConfig->GetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushStrength"), BrushStrength, GEditorPerProjectIni);
	BrushStrength = FMath::Clamp(BrushRadius, 0.f, 1.f);

	GConfig->GetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushFalloff"), BrushFalloffAmount, GEditorPerProjectIni);
	BrushFalloffAmount = FMath::Clamp(BrushFalloffAmount, 0.f, 1.f);
}

UPaintBrushSettings::~UPaintBrushSettings()
{
}

void UPaintBrushSettings::SetBrushRadius(float InRadius)
{
	BrushRadius = (float)FMath::Clamp(InRadius, BrushRadiusMin, BrushRadiusMax);
	GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushRadius"), BrushRadius, GEditorPerProjectIni);
}

void UPaintBrushSettings::SetBrushStrength(float InStrength)
{
	BrushStrength = FMath::Clamp(InStrength, 0.f, 1.f);
	GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushStrength"), BrushStrength, GEditorPerProjectIni);
}

void UPaintBrushSettings::SetBrushFalloff(float InFalloff)
{
	BrushFalloffAmount = FMath::Clamp(InFalloff, 0.f, 1.f);
	GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushFalloff"), BrushFalloffAmount, GEditorPerProjectIni);
}

void UPaintBrushSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPaintBrushSettings, BrushRadius) && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushRadius"), BrushRadius, GEditorPerProjectIni);
	}

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPaintBrushSettings, BrushStrength) && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushStrength"), BrushStrength, GEditorPerProjectIni);
	}

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPaintBrushSettings, BrushFalloffAmount) && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		GConfig->SetFloat(TEXT("MeshPaintEdit"), TEXT("DefaultBrushFalloff"), BrushFalloffAmount, GEditorPerProjectIni);
	}
}