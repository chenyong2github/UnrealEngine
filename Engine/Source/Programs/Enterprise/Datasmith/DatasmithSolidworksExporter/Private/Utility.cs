// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Collections.Generic;
using System.Linq;
using System.Text;

using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith
{
	public static class Utility
	{
		public static bool IsSame(float v1, float v2)
		{
			return (Math.Abs(v2 - v1) <= 0.00000001f);
		}

		public static bool IsSame(double v1, double v2)
		{
			return (Math.Abs(v2 - v1) <= 0.00000001);
		}

		public static bool IsSame(Vec3 v1, Vec3 v2)
		{
			Vec3 d = v2 - v1;
			return ((d.x <= 0.00000001f) && (d.y <= 0.00000001f) && (d.z <= 0.00000001f));
		}

		public static bool IsSame(Color v1, Color v2)
		{
			return ((v1.R == v2.R) && (v1.G == v2.G) && (v1.B == v2.B));
		}

		public static bool IsSame(float a, float b, float epsilon = 0.00000001f)
		{
			const float MIN_NORMAL = 1.1754943508222875079687365372222e-38f;
			float absA = Math.Abs(a);
			float absB = Math.Abs(b);
			float diff = Math.Abs(a - b);
			if (a == b)
				return true;
			else if (a == 0 || b == 0 || absA + absB < MIN_NORMAL)
				return diff < (epsilon * MIN_NORMAL);
			return diff / (absA + absB) < epsilon;
		}

		public static bool IsSame(double a, double b, double epsilon = 0.00000001)
		{
			const double MIN_NORMAL = 1.1754943508222875079687365372222e-38;
			double absA = Math.Abs(a);
			double absB = Math.Abs(b);
			double diff = Math.Abs(a - b);
			if (a == b)
				return true;
			else if (a == 0 || b == 0 || absA + absB < MIN_NORMAL)
				return diff < (epsilon * MIN_NORMAL);
			return diff / (absA + absB) < epsilon;
		}

		public static float Max3(float a, float b, float c)
		{
			float m = (a > b) ? a : b;
			m = (m > c) ? m : c;
			return m;
		}

		public static float Rad2Deg { get { return (float)(180.0 / Math.PI); } }
		public static float Deg2Rad { get { return (float)(Math.PI / 180.0); } }

		public static Vec3 BarycentricToPoint(Vec3 bary, Vec3 v1, Vec3 v2, Vec3 v3)
		{
			return new Vec3((bary.x * v1.x) + (bary.y * v2.x) + (bary.z * v3.x), (bary.x * v1.y) + (bary.y * v2.y) + (bary.z * v3.y), (bary.x * v1.z) + (bary.y * v2.z) + (bary.z * v3.z));
		}

		public static Vec2 BarycentricToPoint(Vec3 bary, Vec2 v1, Vec2 v2, Vec2 v3)
		{
			return new Vec2((bary.x * v1.x) + (bary.y * v2.x) + (bary.z * v3.x), (bary.x * v1.y) + (bary.y * v2.y) + (bary.z * v3.y));
		}

		// Explicit version of the omonimous function in Vec2.
		//
		public static void RotateOnPlane(float cos, float sin, ref float u, ref float v)
		{
			float u1 = u;
			float v1 = v;
			u = (u1 * cos - v1 * sin);
			v = (u1 * sin + v1 * cos);
		}
	}
}
