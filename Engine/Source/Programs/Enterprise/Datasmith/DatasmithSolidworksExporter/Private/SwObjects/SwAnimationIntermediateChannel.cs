// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using SolidWorks.Interop.sldworks;
using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	public class SwAnimationIntermediateChannel
	{
		private Dictionary<int, SwAnimationIntermediateKeyframe> id2key = new Dictionary<int, SwAnimationIntermediateKeyframe>();
		public List<SwAnimationIntermediateKeyframe> keyframes = new List<SwAnimationIntermediateKeyframe>();
		public Component2 Target;
		public int interpolation;

		public SwAnimationIntermediateKeyframe NewKeyframe(int Step, MathTransform LocalTransform, double Time = -1.0)
		{
			SwAnimationIntermediateKeyframe key = new SwAnimationIntermediateKeyframe();
			key.Step = Step;
			key.Time = Time;
			key.Owner = this;
			key.LocalMatrix = new Matrix4(MathUtil.ConvertFromSolidworksTransform(LocalTransform));
			
			keyframes.Add(key);
			id2key.Add(key.Step, key);
			return key;
		}

		public SwAnimationIntermediateKeyframe getKeyframe(int step)
		{
			SwAnimationIntermediateKeyframe res = null;
			if (!id2key.TryGetValue(step, out res))
				return null;
			return res;
		}
	}
}
