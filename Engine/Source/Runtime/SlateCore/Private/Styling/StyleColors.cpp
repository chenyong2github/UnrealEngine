// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/StyleColors.h"


const FSlateColor FStyleColors::Black = EStyleColor::Black;
const FSlateColor FStyleColors::Title = EStyleColor::Title;
const FSlateColor FStyleColors::Foldout = EStyleColor::Foldout;
const FSlateColor FStyleColors::Input = EStyleColor::Input;
const FSlateColor FStyleColors::Background = EStyleColor::Background;
const FSlateColor FStyleColors::Header = EStyleColor::Header;
const FSlateColor FStyleColors::Dropdown = EStyleColor::Dropdown;
const FSlateColor FStyleColors::Hover = EStyleColor::Hover;
const FSlateColor FStyleColors::Hover2 = EStyleColor::Hover2;

const FSlateColor FStyleColors::White = EStyleColor::White;
const FSlateColor FStyleColors::White25 = EStyleColor::White25;
const FSlateColor FStyleColors::Highlight = EStyleColor::Highlight;

const FSlateColor FStyleColors::Primary = EStyleColor::Primary;
const FSlateColor FStyleColors::PrimaryHover = EStyleColor::PrimaryHover;
const FSlateColor FStyleColors::PrimaryPress = EStyleColor::PrimaryPress;

const FSlateColor FStyleColors::Foreground = EStyleColor::Foreground;
const FSlateColor FStyleColors::ForegroundHover = EStyleColor::ForegroundHover;
const FSlateColor FStyleColors::ForegroundInverted = EStyleColor::ForegroundInverted;
const FSlateColor FStyleColors::ForegroundHeader = EStyleColor::ForegroundHeader;

const FSlateColor FStyleColors::Select = EStyleColor::Select;
const FSlateColor FStyleColors::SelectInactive = EStyleColor::SelectInactive;
const FSlateColor FStyleColors::SelectParent = EStyleColor::SelectParent;
const FSlateColor FStyleColors::SelectHover = EStyleColor::SelectHover;
// if select ==  primary shouldnt we have a select pressed which is the same as primary press?

const FSlateColor FStyleColors::AccentBlue = EStyleColor::AccentBlue;
const FSlateColor FStyleColors::AccentPurple = EStyleColor::AccentPurple;
const FSlateColor FStyleColors::AccentPink = EStyleColor::AccentPink;
const FSlateColor FStyleColors::AccentRed = EStyleColor::AccentRed;
const FSlateColor FStyleColors::AccentOrange = EStyleColor::AccentOrange;
const FSlateColor FStyleColors::AccentYellow = EStyleColor::AccentYellow;
const FSlateColor FStyleColors::AccentGreen = EStyleColor::AccentGreen;
const FSlateColor FStyleColors::AccentBrown = EStyleColor::AccentBrown;
const FSlateColor FStyleColors::AccentBlack = EStyleColor::AccentBlack;
const FSlateColor FStyleColors::AccentGray = EStyleColor::AccentGray;
const FSlateColor FStyleColors::AccentWhite = EStyleColor::AccentWhite;
const FSlateColor FStyleColors::AccentFolder = EStyleColor::AccentFolder;


UStyleColorTable::UStyleColorTable()
{
	InitalizeDefaults();
}

void UStyleColorTable::InitalizeDefaults()
{
	SetColor(EStyleColor::Black, COLOR("#000000FF"));
	SetColor(EStyleColor::Title, COLOR("#151515FF"));
	SetColor(EStyleColor::Foldout, COLOR("0F0F0FFF"));
	SetColor(EStyleColor::Input, COLOR("#1A1A1AFF"));
	SetColor(EStyleColor::Background, COLOR("#242424FF"));
	SetColor(EStyleColor::Header, COLOR("#2F2F2FFF"));
	SetColor(EStyleColor::Dropdown, COLOR("#383838FF"));
	SetColor(EStyleColor::Hover, COLOR("#575757FF"));
	SetColor(EStyleColor::Hover2, COLOR("#808080FF"));

	SetColor(EStyleColor::White, COLOR("#FFFFFFFF"));
	SetColor(EStyleColor::White25, COLOR("#FFFFFF40"));
	SetColor(EStyleColor::Highlight, COLOR("#0078D7FF"));

	SetColor(EStyleColor::Primary, COLOR("#26BBFFFF"));
	SetColor(EStyleColor::PrimaryHover, COLOR("#6FD2FFFF"));
	SetColor(EStyleColor::PrimaryPress, COLOR("#1989BCFF"));

	SetColor(EStyleColor::Foreground, COLOR("#A6A6A6FF"));
	SetColor(EStyleColor::ForegroundHover, COLOR("#FFFFFFFF"));
	SetColor(EStyleColor::ForegroundInverted, GetColor(EStyleColor::Input));
	SetColor(EStyleColor::ForegroundHeader, COLOR("#C8C8C8FF"));

	SetColor(EStyleColor::Select, GetColor(EStyleColor::Primary));
	SetColor(EStyleColor::SelectInactive, COLOR("#99B3BFFF"));
	SetColor(EStyleColor::SelectParent, COLOR("#2C323AFF"));
	SetColor(EStyleColor::SelectHover, GetColor(EStyleColor::Background));
	// if select ==  primary shouldnt we have a select pressed which is the same as primary press?

	SetColor(EStyleColor::AccentBlue, COLOR("#26BBFFFF"));
	SetColor(EStyleColor::AccentPurple, COLOR("#A139BFFF"));
	SetColor(EStyleColor::AccentPink, COLOR("#FF729CFF"));
	SetColor(EStyleColor::AccentRed, COLOR("#FF4040FF"));
	SetColor(EStyleColor::AccentOrange, COLOR("#FE9B07FF"));
	SetColor(EStyleColor::AccentYellow, COLOR("#FFDC1AFF"));
	SetColor(EStyleColor::AccentGreen, COLOR("#8BC24AFF"));
	SetColor(EStyleColor::AccentBrown, COLOR("#804D39FF"));
	SetColor(EStyleColor::AccentBlack, COLOR("#242424FF"));
	SetColor(EStyleColor::AccentGray, COLOR("#808080FF"));
	SetColor(EStyleColor::AccentWhite, COLOR("#FFFFFFFF"));
	SetColor(EStyleColor::AccentFolder, COLOR("#B68F55FF"));
}

