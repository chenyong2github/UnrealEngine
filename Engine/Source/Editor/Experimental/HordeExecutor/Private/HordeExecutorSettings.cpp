// Copyright Epic Games, Inc. All Rights Reserved.

#include "HordeExecutorSettings.h"
#include "Misc/ConfigCacheIni.h"

UHordeExecutorSettings::UHordeExecutorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Target = TEXT("localhost:5001");
	PemRootCertificates = TEXT("C:\\Users\\JoeKirchoff\\Downloads\\localhost2.cer");
}
