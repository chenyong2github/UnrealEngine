// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCore : ModuleRules
	{
		public DatasmithCore(ReadOnlyTargetRules Target)
            : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Cbor",
					"Core",
					"CoreUObject",
					"MeshDescription",
					"Messaging",
					"MessagingCommon",
					"RawMesh",
					"StaticMeshDescription",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Json",
					"XmlParser",
				}
			);
        }
	}
}