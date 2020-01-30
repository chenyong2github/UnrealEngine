// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NiagaraAnimNotifies : ModuleRules
{
    public NiagaraAnimNotifies(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.AddRange(
            new string[] {
                "NiagaraAnimNotifies/Private",
            });

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Niagara" });
    }
}
