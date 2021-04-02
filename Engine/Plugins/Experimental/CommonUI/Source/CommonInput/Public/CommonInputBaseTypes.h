// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/SubclassOf.h"
#include "InputCoreTypes.h"
#include "Engine/StreamableManager.h"
#include "Engine/UserDefinedEnum.h"
#include "Templates/SharedPointer.h"
#include "Styling/SlateBrush.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPtr.h"
#include "CommonInputBaseTypes.generated.h"


class UTexture2D;
class UMaterial;
class UCommonInputSettings;

struct COMMONINPUT_API FCommonInputDefaults
{
	static const FName PlatformPC;
	static const FName GamepadGeneric;
};

UENUM(BlueprintType)
enum class ECommonInputType : uint8
{
	MouseAndKeyboard,
	Gamepad,
	Touch,
	Count
};

ENUM_RANGE_BY_COUNT(ECommonInputType, ECommonInputType::Count);

USTRUCT(Blueprintable)
struct COMMONINPUT_API FCommonInputKeyBrushConfiguration
{
	GENERATED_BODY()

public:
	FCommonInputKeyBrushConfiguration();

	const FSlateBrush& GetInputBrush() const { return KeyBrush; }

public:
	UPROPERTY(EditAnywhere, Category = "Key Brush Configuration")
	FKey Key;

	UPROPERTY(EditAnywhere, Category = "Key Brush Configuration")
	FSlateBrush KeyBrush;
};

USTRUCT(Blueprintable)
struct COMMONINPUT_API FCommonInputKeySetBrushConfiguration
{
	GENERATED_BODY()

public:
	FCommonInputKeySetBrushConfiguration();

	const FSlateBrush& GetInputBrush() const { return KeyBrush; }

public:
	UPROPERTY(EditAnywhere, Category = "Key Brush Configuration", Meta = (TitleProperty = "KeyName"))
	TArray<FKey> Keys;

	UPROPERTY(EditAnywhere, Category = "Key Brush Configuration")
	FSlateBrush KeyBrush;
};

/* Derive from this class to store the Input data. It is referenced in the Common Input Settings, found in the project settings UI. */
UCLASS(Abstract, Blueprintable, ClassGroup = Input, meta = (Category = "Common Input"))
class COMMONINPUT_API UCommonUIInputData : public UObject
{
	GENERATED_BODY()

public:
	virtual bool NeedsLoadForServer() const override;

public:
	UPROPERTY(EditDefaultsOnly, Category = "Properties", meta = (RowType = CommonInputActionDataBase))
	FDataTableRowHandle DefaultClickAction;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", meta = (RowType = CommonInputActionDataBase))
	FDataTableRowHandle DefaultBackAction;
};

/* Derive from this class to store the Input data. It is referenced in the Common Input Settings, found in the project settings UI. */
UCLASS(Abstract, Blueprintable, ClassGroup = Input, meta = (Category = "Common Input"))
class COMMONINPUT_API UCommonInputBaseControllerData : public UObject
{
	GENERATED_BODY()

public:
	virtual bool NeedsLoadForServer() const override;
	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, const FKey& Key) const;
	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys) const;

	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

public:
	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	ECommonInputType InputType;
	
	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (GetOptions = GetRegisteredGamepads))
	FName GamepadName;

	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	TSoftObjectPtr<UTexture2D> ControllerTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	TSoftObjectPtr<UTexture2D> ControllerButtonMaskTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (TitleProperty = "Key"))
	TArray<FCommonInputKeyBrushConfiguration> InputBrushDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (TitleProperty = "Keys"))
	TArray<FCommonInputKeySetBrushConfiguration> InputBrushKeySets;

	UFUNCTION()
	static const TArray<FName>& GetRegisteredGamepads();
};

USTRUCT()
struct COMMONINPUT_API FCommonInputPlatformBaseData
{
	GENERATED_BODY()

	friend class UCommonInputSettings;

public:
	FCommonInputPlatformBaseData()
	{
		bSupported = false;
		DefaultInputType = ECommonInputType::Gamepad;
		bSupportsMouseAndKeyboard = false;
		bSupportsGamepad = true;
		bCanChangeGamepadType = true;
		bSupportsTouch = false;
		DefaultGamepadName = FCommonInputDefaults::GamepadGeneric;
	}
	virtual ~FCommonInputPlatformBaseData() = default;

	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, FKey Key, ECommonInputType InputType, const FName& GamepadName) const;
	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys, ECommonInputType InputType,  const FName& GamepadName) const;


	ECommonInputType GetDefaultInputType() const
	{
		return DefaultInputType;
	};

	bool SupportsInputType(ECommonInputType InputType) const 
	{
		switch (InputType)
		{
		case ECommonInputType::MouseAndKeyboard:
		{
			return bSupportsMouseAndKeyboard;
		}
		break;
		case ECommonInputType::Gamepad:
		{
			return bSupportsGamepad;
		}
		break;
		case ECommonInputType::Touch:
		{
			return bSupportsTouch;
		}
		break;
		}
		return false;
	}

	const FName GetDefaultGamepadName() const
	{
		return DefaultGamepadName;
	};

	bool CanChangeGamepadType() const 
	{
		return bCanChangeGamepadType;
	}

	TArray<TSoftClassPtr<UCommonInputBaseControllerData>> GetControllerData()
	{
		return ControllerData;
	}

	static const TArray<FName>& GetRegisteredPlatforms();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	bool bSupported;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (EditCondition = "bSupported"))
	ECommonInputType DefaultInputType;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (EditCondition = "bSupported"))
	bool bSupportsMouseAndKeyboard;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad", Meta = (EditCondition = "bSupported"))
	bool bSupportsGamepad;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad", Meta = (EditCondition = "bSupportsGamepad"))
	FName DefaultGamepadName;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad", Meta = (EditCondition = "bSupportsGamepad"))
	bool bCanChangeGamepadType;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (EditCondition = "bSupported"))
	bool bSupportsTouch;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (TitleProperty = "Key", EditCondition = "bSupported"))
	TArray<TSoftClassPtr<UCommonInputBaseControllerData>> ControllerData;

	UPROPERTY(Transient)
	TArray<TSubclassOf<UCommonInputBaseControllerData>> ControllerDataClasses;
};

class FCommonInputBase
{
public:
	COMMONINPUT_API static FName GetCurrentPlatformName();

	COMMONINPUT_API static UCommonInputSettings* GetInputSettings();

	COMMONINPUT_API static void GetCurrentPlatformDefaults(ECommonInputType& OutDefaultInputType, FName& OutDefaultGamepadName);

	COMMONINPUT_API static FCommonInputPlatformBaseData GetCurrentBasePlatformData();
};
