// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VariantManagerUtils.h"
#include "UObject/UnrealType.h"

namespace VariantManagerUtils
{
	bool IsBuiltInStructProperty(const UProperty* Property)
	{
		bool bIsBuiltIn = false;

		const UStructProperty* StructProp = Cast<const UStructProperty>(Property);
		if (StructProp && StructProp->Struct)
		{
			FName StructName = StructProp->Struct->GetFName();

			bIsBuiltIn =
				StructName == NAME_Rotator ||
				StructName == NAME_Color ||
				StructName == NAME_LinearColor ||
				StructName == NAME_Vector ||
				StructName == NAME_Quat ||
				StructName == NAME_Vector4 ||
				StructName == NAME_Vector2D ||
				StructName == NAME_IntPoint;
		}

		return bIsBuiltIn;
	}
};
