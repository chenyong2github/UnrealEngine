// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataProviders/AIDataProvider_QueryParams.h"
#include "EnvironmentQuery/EnvQueryManager.h"

void UAIDataProvider_QueryParams::BindData(const UObject& Owner, int32 RequestId)
{
	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(&Owner);
	if (QueryManager)
	{
		FloatValue = QueryManager->FindNamedParam(RequestId, ParamName);

		// int param was encoded directly in the float value
		IntValue = *((int32*)&FloatValue);

		// bool param was encoded as -1.0f (false) or 1.0f (true) in the float value
		BoolValue = FloatValue > 0.f;
	}
	else
	{
		IntValue = 0;
		FloatValue = 0.0f;
		BoolValue = false;
	}
}

FString UAIDataProvider_QueryParams::ToString(FName PropName) const
{
	return FString::Printf(TEXT("QueryParam.%s"), *ParamName.ToString());
}
