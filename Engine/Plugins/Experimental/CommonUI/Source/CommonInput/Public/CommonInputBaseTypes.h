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
#include "Engine/PlatformSettings.h"
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

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, EditAnywhere, Category = "Editor")
	int32 SetButtonImageHeightTo = 0;
#endif

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

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

UCLASS(config = Game, defaultconfig)
class COMMONINPUT_API UCommonInputPlatformSettings : public UPlatformSettings
{
	GENERATED_BODY()

	friend class UCommonInputSettings;

public:
	UCommonInputPlatformSettings();

	virtual void PostInitProperties() override;

	static UCommonInputPlatformSettings* Get()
	{
		return UPlatformSettings::GetSettingsForPlatform<UCommonInputPlatformSettings>();
	}

	bool TryGetInputBrush(FSlateBrush& OutBrush, FKey Key, ECommonInputType InputType, const FName& GamepadName) const;
	bool TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys, ECommonInputType InputType,  const FName& GamepadName) const;

	ECommonInputType GetDefaultInputType() const
	{
		return DefaultInputType;
	}

	bool SupportsInputType(ECommonInputType InputType) const;

	const FName GetDefaultGamepadName() const
	{
		return DefaultGamepadName;
	}

	bool CanChangeGamepadType() const 
	{
		return bCanChangeGamepadType;
	}

	TArray<TSoftClassPtr<UCommonInputBaseControllerData>> GetControllerData()
	{
		return ControllerData;
	}


#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif

protected:
	void InitializeControllerData() const;
	virtual void InitializePlatformDefaults();

	UPROPERTY(config, EditAnywhere, Category = "Default")
	ECommonInputType DefaultInputType;

	UPROPERTY(config, EditAnywhere, Category = "Default")
	bool bSupportsMouseAndKeyboard;

	UPROPERTY(config, EditAnywhere, Category = "Default")
	bool bSupportsTouch;

	UPROPERTY(config, EditAnywhere, Category = "Default")
	bool bSupportsGamepad;

	UPROPERTY(config, EditAnywhere, Category = "Default", Meta = (EditCondition = "bSupportsGamepad"))
	FName DefaultGamepadName;

	UPROPERTY(config, EditAnywhere, Category = "Default", Meta = (EditCondition = "bSupportsGamepad"))
	bool bCanChangeGamepadType;

	UPROPERTY(config, EditAnywhere, Category = "Default", Meta = (TitleProperty = "Key"))
	TArray<TSoftClassPtr<UCommonInputBaseControllerData>> ControllerData;

	UPROPERTY(Transient)
	mutable TArray<TSubclassOf<UCommonInputBaseControllerData>> ControllerDataClasses;
};

/* DEPRECATED Legacy! */
USTRUCT()
struct COMMONINPUT_API FCommonInputPlatformBaseData
{
	GENERATED_BODY()

	friend class UCommonInputSettings;

public:
	FCommonInputPlatformBaseData()
	{
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
	ECommonInputType DefaultInputType;

	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	bool bSupportsMouseAndKeyboard;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad")
	bool bSupportsGamepad;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad", Meta = (EditCondition = "bSupportsGamepad"))
	FName DefaultGamepadName;

	UPROPERTY(EditDefaultsOnly, Category = "Gamepad", Meta = (EditCondition = "bSupportsGamepad"))
	bool bCanChangeGamepadType;

	UPROPERTY(EditDefaultsOnly, Category = "Properties")
	bool bSupportsTouch;

	UPROPERTY(EditDefaultsOnly, Category = "Properties", Meta = (TitleProperty = "Key"))
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
};
