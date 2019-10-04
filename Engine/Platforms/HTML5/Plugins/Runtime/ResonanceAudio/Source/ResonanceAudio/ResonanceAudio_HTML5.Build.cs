//
// Copyright (C) Google Inc. 2017. All rights reserved.
//

namespace UnrealBuildTool.Rules
{
    public class ResonanceAudio_HTML5 : ResonanceAudio
    {
		protected virtual bool bSupportsProceduralMesh { get { return false; } }

		public ResonanceAudio_HTML5(ReadOnlyTargetRules Target) : base(Target)
        {
        }
    }
}
