#pragma once

#include "UObject/ObjectMacros.h"
#include "CoreMinimal.h"
#include "MediaPlayerLimits.generated.h"

UCLASS(hidecategories = (Object))
class MEDIAASSETS_API UMediaPlayerLimits : public UObject
{
	GENERATED_BODY()

public:
	UMediaPlayerLimits();

	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlayer")
	static bool ClaimPlayer();

	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlayer")
	static void ReleasePlayer();

private:
	static int32 CurrentPlayerCount;
	static int32 MaxPlayerCount;
	static FCriticalSection AccessLock;
};