// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

namespace UE::MLDeformer
{
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerEditorStyle
		: public FSlateStyleSet
	{
	public:
		FMLDeformerEditorStyle();
		~FMLDeformerEditorStyle();

		static FMLDeformerEditorStyle& Get();
	};

}	// namespace UE::MLDeformer
