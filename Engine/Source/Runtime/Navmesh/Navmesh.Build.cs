// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Navmesh : ModuleRules
	{
		public Navmesh(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicIncludePaths.Add("Runtime/Navmesh/Public");
			PrivateIncludePaths.Add("Runtime/Navmesh/Private");

            PrivateDependencyModuleNames.AddRange(new string[] { "Core" });

			if (Target.bCompileNavmeshSegmentLinks)
			{
				PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=0");
			}

			if (Target.bCompileNavmeshClusterLinks)
			{
				PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=0");
			}
		}
	}
}
