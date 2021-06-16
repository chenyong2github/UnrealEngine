// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace RigVMPythonUtils
{
	FString NameToPep8(const FString& Name)
	{
		// Wish we could use PyGenUtil::PythonizeName, but unfortunately it's private
	
		const FString NameNoSpaces = Name.Replace(TEXT(" "), TEXT("_"));
		FString Result;

		for (const TCHAR& Char : NameNoSpaces)
		{
			if (FChar::IsUpper(Char))
			{
				if (!Result.IsEmpty() && !Result.EndsWith(TEXT("_")))
				{
					Result.AppendChar(TEXT('_'));
				}
				Result.AppendChar(FChar::ToLower(Char));
			}
			else
			{
				Result.AppendChar(Char);
			}
		}
		return Result;
	}

	FString TransformToPythonString(const FTransform& Transform)
	{
		return FString::Printf(TEXT("unreal.Transform(location=[%f,%f,%f],rotation=[%f,%f,%f],scale=[%f,%f,%f])"),
			Transform.GetLocation().X,
			Transform.GetLocation().Y,
			Transform.GetLocation().Z,
			Transform.Rotator().Pitch,
			Transform.Rotator().Yaw,
			Transform.Rotator().Roll,
			Transform.GetScale3D().X,
			Transform.GetScale3D().Y,
			Transform.GetScale3D().Z);
	}

	FString LinearColorToPythonString(const FLinearColor& Color)
	{
		return FString::Printf(TEXT("unreal.LinearColor(%f, %f, %f, %f)"),
			Color.R, Color.G, Color.B, Color.A);
	}


}