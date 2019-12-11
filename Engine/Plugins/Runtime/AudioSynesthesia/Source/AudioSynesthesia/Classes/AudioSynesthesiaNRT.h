// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioAnalyzerNRT.h"
#include "AudioSynesthesiaNRT.generated.h"

/** UAudioSynesthesiaNRTSettings
 *
 * Defines asset actions for derived UAudioSynthesiaNRTSettings subclasses.
 */
UCLASS(Abstract, Blueprintable)
class AUDIOSYNESTHESIA_API UAudioSynesthesiaNRTSettings : public UAudioAnalyzerNRTSettings
{
	GENERATED_BODY()

	public:

		FText GetAssetActionName() const override;

		const TArray<FText>& GetAssetActionSubmenus() const;

		UClass* GetSupportedClass() const override;

		FColor GetTypeColor() const override;
};

/** UAudioSynesthesiaNRT
 *
 * Defines asset actions for derived UAudioSynthesiaNRT subclasses.
 */
UCLASS(Abstract, Blueprintable)
class AUDIOSYNESTHESIA_API UAudioSynesthesiaNRT : public UAudioAnalyzerNRT
{
	GENERATED_BODY()

	public:

		FText GetAssetActionName() const override;

		const TArray<FText>& GetAssetActionSubmenus() const;

		UClass* GetSupportedClass() const override;

		FColor GetTypeColor() const override;
};

