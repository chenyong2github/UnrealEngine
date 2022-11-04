// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	
	public class ChaosFlesh : ModuleRules
	{
		public ChaosFlesh(ReadOnlyTargetRules Target) : base(Target)
		{
			// We can do specializations for PUBLIC platforms in this file; otherwise it
			// may not be defined in any given workspace, and references to it will fail.
			// You can do a string comparison ala:
			// \code
			//		Target.Platform.ToString() == "XXX"
			// \endcode
			// But that may break our NDA policy which forbids even mentioning NDA'd
			// platforms in engine code.  A less fragile way to go about it would be to
			// add a platform specific build file:
			//
			// ChaosFlesh_XXX.Build.cs:
			// \code
			//    namespace UnrealBuildTool.Rules
			//    {
			//        public class ChaosFlesh_XXX : ChaosFlesh
			//        {
			//            public ChaosFlesh_XXX(ReadOnlyTargetRules Target) : base(Target) 
			//            { <Do XXX specific dances here> }
			//        }
			//    }
			// \endcode
			//
			// However, adding one of these build files for a public platform will clash with
			// specializations that the build system makes.  In practice, if it's in
			// Engine\Source\Programs\UnrealBuildTool\Platform it is public, otherwise it's not.
			if (Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.Linux ||
				Target.Platform == UnrealTargetPlatform.Mac)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
				PrivateDefinitions.Add("USE_ZLIB=1");
			}

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
					"InputCore",
				}
			);

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}

}
