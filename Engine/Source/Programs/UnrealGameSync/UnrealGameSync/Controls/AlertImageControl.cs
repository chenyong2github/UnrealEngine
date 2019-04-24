// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;

namespace UnrealGameSync
{
	partial class AlertImageControl : UserControl
	{
		Image ImageValue;

		public Image Image
		{
			get { return ImageValue; }
			set { ImageValue = value; Invalidate(); }
		}

		public AlertImageControl()
		{
		}

		protected override void OnPaint(PaintEventArgs Event)
		{
			Event.Graphics.FillRectangle(SystemBrushes.Window, 0, 0, Width, Height);

			if(Image != null)
			{
				float Scale = Math.Min((float)Width / Image.Width, (float)Height / Image.Height);

				int ImageW = (int)(Image.Width * Scale);
				int ImageH = (int)(Image.Height * Scale);

				Event.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
				Event.Graphics.SmoothingMode = SmoothingMode.HighQuality;
				Event.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
				Event.Graphics.DrawImage(Image, (Width - ImageW) / 2, (Height - ImageH) / 2, ImageW, ImageH); 
			}
		}
	}
}
