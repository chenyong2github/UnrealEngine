// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;
using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwBody
	{
		public SwPartDoc Doc { get; set; } = null;
		public SwMaterial Material { get; set; } = null;
		public List<SwFace> Faces { get; set; } = new List<SwFace>();
		public Body2 Body { get; set; } = null;

		public BoundingBox Bounds { get; private set; }

		public enum eQuality
		{
			Current,
			Low,
			Medium,
			High
		};

		public eQuality Quality { get; set; } = eQuality.Medium;

		public SwBody(SwPartDoc doc, Body2 body)
		{
			Doc = doc;
			Body = body;

			Bounds = new BoundingBox(body.GetBodyBox() as double[]);

			var face = body.GetFirstFace() as Face2;
			while (face != null)
			{
				Faces.Add(new SwFace(this, face));
				face = face.GetNextFace() as Face2;
			}
		}
	}
}
