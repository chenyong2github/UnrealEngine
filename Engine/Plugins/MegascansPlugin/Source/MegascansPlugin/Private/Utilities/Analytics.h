// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"




class FAnalytics
{
private:
	FAnalytics() = default;
	static TSharedPtr<FAnalytics> AnalyticsInst;
	FString GetUEVersion();

    


public:
	static TSharedPtr<FAnalytics> Get();
    void SendAnalytics(TSharedPtr<FJsonObject> JsonObject);
	TSharedPtr<FJsonObject> GenerateAnalyticsJson();
	TSharedPtr<FJsonObject> GenerateBlendAnalyticsJson( );
	
};