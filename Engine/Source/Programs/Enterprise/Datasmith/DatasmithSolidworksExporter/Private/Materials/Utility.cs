// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Materials
{
    [ComVisible(false)]
    public static class Utility
    {
        public static Color ConvertColor(int abgr)
        {
            int a = (int)(byte)(abgr >> 24);
            int b = (int)(byte)(abgr >> 16);
            int g = (int)(byte)(abgr >> 8);
            int r = (int)(byte)abgr;
            return Color.FromArgb(a, r, g, b);
        }
    }
}
