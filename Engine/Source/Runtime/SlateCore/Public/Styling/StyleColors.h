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
	WindowBorder,
	Foldout,
	Input,
	Recessed,
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
	Secondary, 
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

	/** Only user colors should be below this line
	 * To use user colors:
	 * 1. Set an unused user enum value below as the color value for an FSlateColor. E.g. FSlateColor MyCustomColor(EStyleColors::User1)
	 * 2. Set the actual color. E.g UStyleColorTable::Get().SetColor(EStyleColor::User1, FLinearColor::White)
	 * 3. Give it a display name if you want it to be configurable by editor users. E.g.  UStyleColorTable::Get().SetColorDisplayName(EUserStyleColor::User1, "My Color Name")
	 */
	User1,
	User2,
	User3,
	User4,
	User5,
	User6,
	User7,
	User8,
	User9,
	User10,
	User11,
	User12,
	User13,
	User14,
	User15,
	User16,

	MAX
};

USTRUCT()
struct FStyleColorList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = Colors)
	FLinearColor StyleColors[(int32)EStyleColor::MAX];

	FText DisplayNames[(int32)EStyleColor::MAX];
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
		return Colors.StyleColors[static_cast<int32>(Color)];
	}

	void SetColor(EStyleColor InColorId, FLinearColor InColor)
	{
		Colors.StyleColors[static_cast<int32>(InColorId)] = InColor;
	}

	void SetColorDisplayName(EStyleColor InColorId, FText DisplayName)
	{
		Colors.DisplayNames[static_cast<int32>(InColorId)] = DisplayName;
	}

	FText GetColorDisplayName(EStyleColor InColorId) const
	{
		return Colors.DisplayNames[static_cast<int32>(InColorId)];
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
	UPROPERTY(EditAnywhere, Config, Category = Colors)
	FStyleColorList Colors;
};

/**
 * Common/themeable colors used by all styles
 * Please avoid adding new generic colors to this list without discussion first
 */
struct SLATECORE_API FStyleColors
{
	static const FSlateColor Transparent;
	static const FSlateColor Black;
	static const FSlateColor Title;
	static const FSlateColor WindowBorder;
	static const FSlateColor Foldout;
	static const FSlateColor Input;
	static const FSlateColor Recessed;
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
	static const FSlateColor Secondary;

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
