// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "EOSSharedSettings.generated.h"

UENUM(BlueprintType)
enum class EEosLogLevel : uint8
{
	Off,
	Fatal,
	Error,
	Warning,
	Info,
	Verbose,
	VeryVerbose
};

void LexFromString(EEosLogLevel& Value, const TCHAR* String);

enum class EOS_ELogLevel : int32_t;
EOS_ELogLevel ConvertLogLevel(const EEosLogLevel LogLevel);

/** Native version of the UObject based config data */
struct FEOSSharedSettings
{
	EEosLogLevel LogLevel = EEosLogLevel::Info;
};

UCLASS(Config=Engine, DefaultConfig)
class EOSSHARED_API UEOSSharedSettings :
	public UObject
{
	GENERATED_BODY()

public:

	/** Set the desired log level */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "EOS Settings")
	EEosLogLevel LogLevel = EEosLogLevel::Info;

	static FEOSSharedSettings GetSettings();
	FEOSSharedSettings ToNative() const;

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	static FEOSSharedSettings AutoGetSettings();
	static FEOSSharedSettings ManualGetSettings();
};
