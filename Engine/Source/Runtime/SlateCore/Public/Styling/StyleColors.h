// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateColor.h"
#include "StyleColors.generated.h"


// HEX Colors from sRGB Color space
#define COLOR( HexValue ) FLinearColor::FromSRGBColor(FColor::FromHex(HexValue))

UENUM()
enum class EStyleColor : uint8
{
	Black,
	Title,
	Foldout,
	Input,
	Background,
	Header,
	Dropdown,
	Hover,
	Hover2,
	White,
	White25,
	Highlight,
	Primary,
	PrimaryHover,
	PrimaryPress,
	Foreground,
	ForegroundHover,
	ForegroundInverted,
	ForegroundHeader,
	Select,
	SelectInactive,
	SelectParent,
	SelectHover,
	AccentBlue,
	AccentPurple,
	AccentPink,
	AccentRed,
	AccentOrange,
	AccentYellow,
	AccentGreen,
	AccentBrown,
	AccentBlack,
	AccentGray,
	AccentWhite,
	AccentFolder,

	MAX
};

UCLASS(Config=EditorSettings)
class SLATECORE_API UStyleColorTable : public UObject 
{
	GENERATED_BODY()
public:
	static UStyleColorTable& Get()
	{
		return *GetMutableDefault<UStyleColorTable>();
	}

	const FLinearColor& GetColor(EStyleColor Color)
	{
		return Colors[static_cast<int32>(Color)];
	}

	void SetColor(EStyleColor InColorId, FLinearColor InColor)
	{
		Colors[static_cast<int32>(InColorId)] = InColor;
	}

	UStyleColorTable();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			SaveConfig();
		}
	}
#endif

	void InitalizeDefaults();

private:
	UPROPERTY(EditAnywhere, Config, Category=Colors)
	FLinearColor Colors[EStyleColor::MAX];
};


struct SLATECORE_API FStyleColors
{
	static const FSlateColor Black;
	static const FSlateColor Title;
	static const FSlateColor Foldout;
	static const FSlateColor Input;
	static const FSlateColor Background;
	static const FSlateColor Header;
	static const FSlateColor Dropdown;
	static const FSlateColor Hover;
	static const FSlateColor Hover2;
	static const FSlateColor White;
	static const FSlateColor White25;
	static const FSlateColor Highlight;

	static const FSlateColor Primary;
	static const FSlateColor PrimaryHover;
	static const FSlateColor PrimaryPress;


	static const FSlateColor Foreground;
	static const FSlateColor ForegroundHover;
	static const FSlateColor ForegroundInverted;
	static const FSlateColor ForegroundHeader;

	static const FSlateColor Select;
	static const FSlateColor SelectInactive;
	static const FSlateColor SelectParent;
	static const FSlateColor SelectHover;

	static const FSlateColor AccentBlue;
	static const FSlateColor AccentPurple;
	static const FSlateColor AccentPink;
	static const FSlateColor AccentRed;
	static const FSlateColor AccentOrange;
	static const FSlateColor AccentYellow;
	static const FSlateColor AccentGreen;
	static const FSlateColor AccentBrown;
	static const FSlateColor AccentBlack;
	static const FSlateColor AccentGray;
	static const FSlateColor AccentWhite;
	static const FSlateColor AccentFolder;
};
