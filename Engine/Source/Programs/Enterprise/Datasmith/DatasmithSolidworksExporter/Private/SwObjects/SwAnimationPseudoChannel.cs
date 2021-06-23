// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.IO;
using System.Drawing;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swmotionstudy;
using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	public class SwAnimationPseudoChannel
	{
		private Dictionary<int, SwAnimationPseudoKeyframe> id2key = new Dictionary<int, SwAnimationPseudoKeyframe>();
		public Component2 Target;
		public int interpolation;
		public List<SwAnimationPseudoKeyframe> keyframes;

		public SwAnimationPseudoChannel()
		{
		}

		public SwAnimationPseudoKeyframe newKeyframe(int step, double time = -1.0)
		{
			SwAnimationPseudoKeyframe key = new SwAnimationPseudoKeyframe();
			key.Step = step;
			key.Time = time;
			key.Owner = this;
			keyframes.Add(key);
			id2key.Add(key.Step, key);
			return key;
		}

		public SwAnimationPseudoKeyframe getKeyframe(int step)
		{
			SwAnimationPseudoKeyframe res = null;
			if (!id2key.TryGetValue(step, out res))
				return null;
			return res;
		}
	}
}
