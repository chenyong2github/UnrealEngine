// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/Activity/ActivityColumn.h"
#include "Session/Activity/PredefinedActivityColumns.h"

namespace UE::MultiUserServer::PackageViewerColumns
{
	DECLARE_DELEGATE_RetVal_OneParam(TOptional<int64>, FGetNumericValueFromPackageActivity, const FConcertSessionActivity& /*Activity*/);
	using FGetSizeOfPackageActivity = FGetNumericValueFromPackageActivity;
	using FGetVersionOfPackageActivity = FGetNumericValueFromPackageActivity;
	
	enum class EPredefinedPackageColumnOrder : int32
	{
		Size = static_cast<int32>(ConcertSharedSlate::ActivityColumn::EPredefinedColumnOrder::ClientName) + 1,
		Version = Size + 1
	};
	
	extern const FName SizeColumnId;
	extern const FName VersionColumnId;
	
	FActivityColumn SizeColumn(FGetSizeOfPackageActivity GetEventDataFunc);
	FActivityColumn VersionColumn(FGetVersionOfPackageActivity GetEventDataFunc);
};
