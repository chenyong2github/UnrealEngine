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
	public class SwAnimationIntermediateKeyframe
	{
		public int Step;
		public double Time;
		public MathTransform GlobalTm;
		public MathTransform RelativeTm;
		public SwAnimationIntermediateChannel Owner;
		public bool IsPadding;

		public SwAnimationIntermediateKeyframe()
		{
			IsPadding = false;
			Owner = null;
			Step = -1;
			Time = -1.0;
		}
	};
}
