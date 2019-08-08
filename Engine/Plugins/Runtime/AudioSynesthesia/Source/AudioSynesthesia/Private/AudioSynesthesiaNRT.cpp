// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaNRT.h"


FText UAudioSynesthesiaNRTSettings::GetAssetActionName() const
{
	return FText::FromString("Audio Synesthesia NRT Settings"); 
}

UClass* UAudioSynesthesiaNRTSettings::GetSupportedClass() const 
{
	return UAudioSynesthesiaNRTSettings::StaticClass();
}

FColor UAudioSynesthesiaNRTSettings::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

FText UAudioSynesthesiaNRT::GetAssetActionName() const
{
	return FText::FromString("Audio Synesthesia NRT"); 
}

UClass* UAudioSynesthesiaNRT::GetSupportedClass() const
{
	return UAudioSynesthesiaNRT::StaticClass();
}

FColor UAudioSynesthesiaNRT::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

