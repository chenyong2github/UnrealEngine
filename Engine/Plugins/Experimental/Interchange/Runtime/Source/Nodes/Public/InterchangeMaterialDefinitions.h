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

				namespace TextureSample
				{
					const FName Name = TEXT("TextureSample");

					namespace Inputs
					{
						const FName Texture = TEXT("TextureUid"); // Type: FString (unique id of a texture node)
						const FName UTiling = TEXT("UTiling"); // Type: float
						const FName VTiling = TEXT("VTiling"); // Type: float
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
				const FName Shininess = TEXT("Shininess"); // Type: float
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
	}
}
}
