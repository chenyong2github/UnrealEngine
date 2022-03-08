// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE
{
namespace Interchange
{
	namespace Materials
	{
		namespace Standard
		{
			namespace Nodes
			{
				namespace Add
				{
					const FName Name = TEXT("Add");

					namespace Inputs
					{
						const FName A = TEXT("A");
						const FName B = TEXT("B");
					}
				}

				namespace FlattenNormal
				{
					const FName Name = TEXT("FlattenNormal");

					namespace Inputs
					{
						const FName Normal = TEXT("Normal");
						const FName Flatness = TEXT("Flatness");
					}
				}

				namespace Lerp
				{
					const FName Name = TEXT("Lerp");

					namespace Inputs
					{
						const FName A = TEXT("A"); // Type: linear color
						const FName B = TEXT("B"); // Type: linear color
						const FName Factor = TEXT("Factor"); // Type: float
					}
				}

				namespace Multiply
				{
					const FName Name = TEXT("Multiply");

					namespace Inputs
					{
						const FName A = TEXT("A");
						const FName B = TEXT("B");
					}
				}

				namespace OneMinus
				{
					const FName Name = TEXT("OneMinus");

					namespace Inputs
					{
						const FName Input = TEXT("Input");
					}
				}

				namespace TextureCoordinate
				{
					const FName Name = TEXT("TextureCoordinate");

					namespace Inputs
					{
						const FName Index = TEXT("Index"); // Type: int
						const FName UTiling = TEXT("UTiling"); // Type: float
						const FName VTiling = TEXT("VTiling"); // Type: float
						const FName Offset = TEXT("Offset"); // Type: vec2
						const FName Scale = TEXT("Scale"); // Type: vec2
						const FName Rotate = TEXT("Rotate"); // Type: float, Range: 0-1
						const FName RotationCenter = TEXT("RotationCenter"); // Type: vec2
					}
				}

				namespace TextureSample
				{
					const FName Name = TEXT("TextureSample");

					namespace Inputs
					{
						const FName Coordinates = TEXT("Coordinates");
						const FName Texture = TEXT("TextureUid"); // Type: FString (unique id of a texture node)
					}

					namespace Outputs
					{
						const FName RGB = TEXT("RGB"); // Type: linear color
						const FName R = TEXT("R"); // Type: float
						const FName G = TEXT("G"); // Type: float
						const FName B = TEXT("B"); // Type: float
						const FName A = TEXT("A"); // Type: float
						const FName RGBA = TEXT("RGBA"); // Type: linear color
					}
				}
			}
		}

		namespace Common
		{
			namespace Parameters
			{
				const FName EmissiveColor = TEXT("EmissiveColor"); // Type: linear color
				const FName Normal = TEXT("Normal"); // Type: vector3f
				const FName Opacity = TEXT("Opacity"); // Type: float
				const FName Occlusion = TEXT("Occlusion"); // Type: float
				const FName IndexOfRefraction = TEXT("IOR"); // Type: float
			}
		}

		namespace Lambert
		{
			namespace Parameters
			{
				using namespace Common::Parameters;

				const FName DiffuseColor = TEXT("DiffuseColor"); // Type: linear color
			}
		}

		namespace Phong
		{
			namespace Parameters
			{
				using namespace Lambert::Parameters;

				const FName SpecularColor = TEXT("SpecularColor"); // Type: linear color
				const FName Shininess = TEXT("Shininess"); // Type: float, this is the specular exponent, expected range: 2-100
			}
		}

		namespace PBR
		{
			namespace Parameters
			{
				using namespace Common::Parameters;

				const FName BaseColor = TEXT("BaseColor"); // Type: vector3
				const FName Metallic = TEXT("Metallic"); // Type: float
				const FName Specular = TEXT("Specular"); // Type: float
				const FName Roughness = TEXT("Roughness"); // Type: float
			}
		}

		namespace ClearCoat
		{
			namespace Parameters
			{
				using namespace PBR::Parameters;

				const FName ClearCoat = TEXT("ClearCoat"); // Type: float
				const FName ClearCoatRoughness = TEXT("ClearCoatRoughness"); // Type: float
				const FName ClearCoatNormal = TEXT("ClearCoatNormal"); // Type: vector3
			}
		}

		namespace ThinTranslucent
		{
			namespace Parameters
			{
				using namespace PBR::Parameters;

				const FName TransmissionColor = TEXT("TransmissionColor"); // Type: vector3
			}
		}

		namespace Sheen
		{
			namespace Parameters
			{
				using namespace PBR::Parameters;

				const FName SheenColor = TEXT("SheenColor"); // Type: vector3
				const FName SheenRoughness = TEXT("SheenRoughness"); // Type: float
			}
		}
	}
}
}
