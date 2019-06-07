// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ISlateStyle;

struct FLiveLinkEditorPrivate
{
	static TSharedPtr< class ISlateStyle > GetStyleSet();
};
