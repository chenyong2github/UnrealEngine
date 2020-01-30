// Copyright Epic Games, Inc. All Rights Reserved.

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
	partial class AlertButtonControl : Button
	{
		public enum AlertButtonTheme
		{
			Normal,
			Green,
			Red,
			Strong,
			Custom
		}

		[TypeConverter(typeof(ExpandableObjectConverter))]
		public struct AlertButtonColors
		{
			public Color ForeColor
			{
				get; set;
			}

			public Color BorderColor
			{
				get; set;
			}

			public Color BackgroundColor1
			{
				get; set;
			}

			public Color BackgroundColor2
			{
				get; set;
			}

			public Color BackgroundColorHover1
			{
				get; set;
			}

			public Color BackgroundColorHover2
			{
				get; set;
			}

			public Color BackgroundColorDown1
			{
				get; set;
			}

			public Color BackgroundColorDown2
			{
				get; set;
			}
		}

		AlertButtonTheme ThemeValue;
		AlertButtonColors Colors;
		AlertButtonColors CustomColorsValue;
		bool bMouseOver;
		bool bMouseDown;

		public AlertButtonTheme Theme
		{
			get { return ThemeValue; }
			set { ThemeValue = value; UpdateThemeColors(); }
		}

		public AlertButtonColors CustomColors
		{
			get { return CustomColorsValue; }
			set { CustomColorsValue = value; UpdateThemeColors(); }
		}

		public AlertButtonControl()
		{
			Theme = AlertButtonTheme.Normal;
			UpdateThemeColors();
		}

		private void UpdateThemeColors()
		{
			switch(Theme)
			{
				case AlertButtonTheme.Normal:
					Colors.ForeColor = Color.FromArgb(64, 86, 106);
					Colors.BorderColor = Color.FromArgb(230, 232, 235);
					Colors.BackgroundColor1 = Color.FromArgb(255, 255, 255);
					Colors.BackgroundColor2 = Color.FromArgb(244, 245, 247);
					Colors.BackgroundColorHover1 = Color.FromArgb(244, 245, 247);
					Colors.BackgroundColorHover2 = Color.FromArgb(244, 245, 247);
					Colors.BackgroundColorDown1 = Color.FromArgb(234, 235, 237);
					Colors.BackgroundColorDown2 = Color.FromArgb(234, 235, 237);
					break;
				case AlertButtonTheme.Green:
					Colors.ForeColor = Color.FromArgb(255, 255, 255);
					Colors.BorderColor = Color.FromArgb(143, 199, 156);
					Colors.BackgroundColor1 = Color.FromArgb(116, 192, 134);
					Colors.BackgroundColor2 = Color.FromArgb(99, 175, 117);
					Colors.BackgroundColorHover1 = Color.FromArgb(99, 175, 117);
					Colors.BackgroundColorHover2 = Color.FromArgb(99, 175, 117);
					Colors.BackgroundColorDown1 = Color.FromArgb(90, 165, 107);
					Colors.BackgroundColorDown2 = Color.FromArgb(90, 165, 107);
					break;
				case AlertButtonTheme.Red:
					Colors.ForeColor = Color.FromArgb(255, 255, 255);
					Colors.BorderColor = Color.FromArgb(230, 232, 235);
					Colors.BackgroundColor1 = Color.FromArgb(222, 108, 86);
					Colors.BackgroundColor2 = Color.FromArgb(214, 69, 64);
					Colors.BackgroundColorHover1 = Color.FromArgb(214, 69, 64);
					Colors.BackgroundColorHover2 = Color.FromArgb(214, 69, 64);
					Colors.BackgroundColorDown1 = Color.FromArgb(204, 59, 54);
					Colors.BackgroundColorDown2 = Color.FromArgb(204, 59, 54);
					break;
				case AlertButtonTheme.Strong:
					Colors.ForeColor = Color.FromArgb(255, 255, 255);
					Colors.BorderColor = Color.FromArgb(230, 232, 235);
					Colors.BackgroundColor1 = Color.FromArgb(200, 74, 49);
					Colors.BackgroundColor2 = Color.FromArgb(200, 74, 49); 
					Colors.BackgroundColorHover1 = Color.FromArgb(222, 108, 86);
					Colors.BackgroundColorHover2 = Color.FromArgb(222, 108, 86);
					Colors.BackgroundColorDown1 = Color.FromArgb(204, 59, 54);
					Colors.BackgroundColorDown2 = Color.FromArgb(204, 59, 54);
					break;
				case AlertButtonTheme.Custom:
					Colors = CustomColorsValue;
					break;
			}

			base.ForeColor = Colors.ForeColor;
			Invalidate();
		}

		protected override void OnMouseDown(MouseEventArgs mevent)
		{
			base.OnMouseDown(mevent);

			bMouseDown = true;
			Invalidate();
		}

		protected override void OnMouseUp(MouseEventArgs mevent)
		{
			base.OnMouseUp(mevent);

			bMouseDown = false;
			Invalidate();
		}

		protected override void OnMouseEnter(EventArgs e)
		{
			base.OnMouseHover(e);

			bMouseOver = true;
			Invalidate();
		}

		protected override void OnMouseLeave(EventArgs e)
		{
			base.OnMouseLeave(e);

			bMouseOver = false;
			Invalidate();
		}

		protected override void OnPaint(PaintEventArgs Event)
		{
			Event.Graphics.FillRectangle(SystemBrushes.Window, 0, 0, Width, Height);
			using(GraphicsPath Path = new GraphicsPath())
			{
				const int Diameter = 4;

				Path.StartFigure();
				Path.AddArc(Width - 1 - Diameter, Height - 1 - Diameter, Diameter, Diameter, 0, 90);
				Path.AddArc(0, Height - 1 - Diameter, Diameter, Diameter, 90, 90);
				Path.AddArc(0, 0, Diameter, Diameter, 180, 90);
				Path.AddArc(Width - 1 - Diameter, 0, Diameter, Diameter, 270, 90);
				Path.CloseFigure();

				Color BackgroundColorMin = (bMouseDown && bMouseOver)? Colors.BackgroundColorDown1 : bMouseOver? Colors.BackgroundColorHover1 : Colors.BackgroundColor1;
				Color BackgroundColorMax = (bMouseDown && bMouseOver)? Colors.BackgroundColorDown2 : bMouseOver? Colors.BackgroundColorHover2 : Colors.BackgroundColor2;

				using(LinearGradientBrush Brush = new LinearGradientBrush(new Point(0, 0), new Point(0, Height), BackgroundColorMin, BackgroundColorMax))
				{
					Event.Graphics.FillPath(Brush, Path);
				}
				using(Pen SolidPen = new Pen(Colors.BorderColor))
				{
					Event.Graphics.DrawPath(SolidPen, Path);
				}
			}

			TextRenderer.DrawText(Event.Graphics, Text, Font, new Rectangle(0, 0, Width, Height), ForeColor, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
		}
	}
}
