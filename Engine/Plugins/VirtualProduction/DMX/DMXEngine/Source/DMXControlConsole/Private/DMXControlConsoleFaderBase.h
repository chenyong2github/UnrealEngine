// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DMXControlConsoleFaderBase.generated.h"

class UDMXControlConsoleFaderGroup;


/** Base class for a Fader in the DMX Control Console. */
UCLASS(Abstract)
class DMXCONTROLCONSOLE_API UDMXControlConsoleFaderBase
	: public UObject
{
	GENERATED_BODY()

public:
	/** Gets this Fader's index according to its Fader Group owner */
	virtual int32 GetIndex() const;

	/** Gets the owner Fader Group of this Fader */
	UDMXControlConsoleFaderGroup& GetOwnerFaderGroupChecked() const;

	/** Gets the name of the Fader */
	const FString& GetFaderName() const { return FaderName; };

	/** Sets the name of the Fader */
	virtual void SetFaderName(const FString& NewName); 

	/** Returns the Ending Channel to which to send DMX to */
	int32 GetEndingAddress() const { return EndingAddress; }

	/** Returns the current value of the fader */
	uint32 GetValue() const { return Value; }

	/** Sets the current value of the fader */
	virtual void SetValue(const uint32 NewValue);

	/** Gets the min value of the fader */
	uint32 GetMinValue() const { return MinValue; }

	/** Gets the max value of the fader */
	uint32 GetMaxValue() const { return MaxValue; }

	/** Gets wheter this Fader cans send DMX data */
	bool IsMuted() const { return bIsMuted; }

	/** Mutes/Unmutes this fader */
	void ToggleMute();

	/** Destroys this Fader */
	virtual void Destroy();

	// Property Name getters
	FORCEINLINE static FName GetFaderNamePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderBase, FaderName); }
	FORCEINLINE static FName GetEndingAddressPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderBase, EndingAddress); }
	FORCEINLINE static FName GetValuePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderBase, Value); }
	FORCEINLINE static FName GetMinValuePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderBase, MinValue); }
	FORCEINLINE static FName GetMaxValuePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderBase, MaxValue); }

protected:
	//~ Begin of UObject interface
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End of UObject interface

	/** Cached Name of the Fader */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "1"), Category = "DMX Fader")
	FString FaderName;

	/** The end channel Address to send DMX to */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "5"), Category = "DMX Fader")
	int32 EndingAddress = 1;

	/** The current Fader Value */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "6"), Category = "DMX Fader")
	uint32 Value = 0;

	/** The minimum Fader Value */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "7"), Category = "DMX Fader")
	uint32 MinValue = 0;

	/** The maximum Fader Value */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "8"), Category = "DMX Fader")
	uint32 MaxValue = 255;

	/** If true, the fader doesn't send DMX */
	bool bIsMuted = false;
};
