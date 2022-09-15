// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class NNXUtils : ModuleRules
    {
        public NNXUtils(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"NNXCore"
				}
			);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"Core",
					"CoreUObject",
					"Engine",

					"NNX_ONNXRuntime",
					"NNX_ONNX_1_11_0",
					"NNX_ONNXRuntimeProto_1_11_0",
					"ORTHelper",
					"Protobuf"
				}
			);

			//PrivateDefinitions.Add("ONNX_ML");
		}
    }
}
