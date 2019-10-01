// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IMagicLeapPlugin.h"
#include "IMagicLeapPlanesModule.h"
#include "MagicLeapPlanesTypes.h"
#include "IMagicLeapTrackerEntity.h"
#include "Lumin/CAPIShims/LuminAPIPlanes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapPlanes, Verbose, All);

class FMagicLeapPlanesModule : public IMagicLeapPlanesModule, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapPlanesModule();

	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);

	/** IMagicLeapTrackerEntity interface */
	virtual void DestroyEntityTracker() override;

	virtual bool CreateTracker() override;
	virtual bool DestroyTracker() override;
	virtual bool IsTrackerValid() const override;
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultStaticDelegate& ResultDelegate) override;
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultDelegateMulti& ResultDelegate) override;

private:
#if WITH_MLSDK
	MLHandle SubmitPlanesQuery(const FMagicLeapPlanesQuery& QueryParams);

	struct FPlanesRequestMetaData
	{
		MLHandle ResultHandle;
		FMagicLeapPlanesResultDelegateMulti ResultDelegateDynamic;
		FMagicLeapPlanesResultStaticDelegate ResultDelegateStatic;
		TArray<MLPlane> ResultMLPlanes;
	};

	TArray<FPlanesRequestMetaData> PendingRequests;
	MLHandle Tracker;
#endif //WITH_MLSDK
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
};

inline FMagicLeapPlanesModule& GetMagicLeapPlanesModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapPlanesModule>("MagicLeapPlanes");
}
