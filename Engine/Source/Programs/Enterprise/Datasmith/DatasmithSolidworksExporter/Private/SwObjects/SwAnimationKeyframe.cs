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
	public class SwAnimationKeyframe
	{
		public double Time = 0.0;
		public double[] Value = new double[4] { 0.0, 0.0, 0.0, 1.0 };
	}
}
