 // Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class RigLogicModule : ModuleRules
    {
        private string ModulePath
        {
            get { return ModuleDirectory; }
        }

        public RigLogicModule(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDefinitions.Add("RL_SHARED=1"); //used instead of #define so cpp files can do conditional compilation


            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
					"RigVM",
                    "ControlRig",
                    "RigLogicLib",
                    "MessageLog",
                    "RigVM",
					"Projects"
                } 
                );

            if (Target.Type == TargetType.Editor)
            {
                PublicDependencyModuleNames.Add("UnrealEd");
            }

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"AnimationCore",
                    "ControlRig",
					"RenderCore",
					"RHI"
				}
             );
        }
    }
}
