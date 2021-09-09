// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	public class SwAnimationIntermediateKeyframe
	{
		public int Step;
		public double Time;
		public MathTransform GlobalTm;
		public MathTransform RelativeTm;
		public SwAnimationIntermediateChannel Owner;
		public bool IsPadding;
		public Matrix4 LocalMatrix;

		public SwAnimationIntermediateKeyframe()
		{
			IsPadding = false;
			Owner = null;
			Step = -1;
			Time = -1.0;
		}
	};
}
