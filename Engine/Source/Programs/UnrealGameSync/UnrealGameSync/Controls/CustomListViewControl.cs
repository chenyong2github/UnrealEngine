// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;

namespace UnrealGameSync
{
	abstract class CustomListViewWidget
	{
		public ListViewItem Item
		{
			get;
			private set;
		}

		Rectangle? PreviousBounds;

		public Cursor Cursor
		{
			get;
			set;
		}

		public CustomListViewWidget(ListViewItem Item)
		{
			this.Item = Item;
			this.Cursor = Cursors.Arrow;
		}

		public void Invalidate()
		{
			PreviousBounds = null;
		}

		public virtual bool RequiresLayout()
		{
			return !PreviousBounds.HasValue;
		}

		public void ConditionalLayout(Control Owner, Rectangle Bounds)
		{
			if(RequiresLayout() || PreviousBounds.Value != Bounds)
			{
				using(Graphics Graphics = Owner.CreateGraphics())
				{
					Layout(Graphics, Bounds);
				}
				PreviousBounds = Bounds;
			}
		}

		public virtual void OnMouseMove(Point Location)
		{
		}

		public virtual void OnMouseLeave()
		{
		}

		public virtual void OnMouseDown(Point Location)
		{
		}

		public virtual void OnMouseUp(Point Location)
		{
		}

		public abstract void Layout(Graphics Graphics, Rectangle Bounds);
		public abstract void Render(Graphics Graphics);
	}

	class StatusLineListViewWidget : CustomListViewWidget
	{
		public StatusElementResources Resources;
		public StatusLine Line = new StatusLine();

		public HorizontalAlignment HorizontalAlignment
		{
			get; set;
		}

		Rectangle Bounds;
		StatusElement MouseDownElement;
		StatusElement MouseOverElement;

		public StatusLineListViewWidget(ListViewItem Item, StatusElementResources Resources)
			: base(Item)
		{
			this.Resources = Resources;
			this.HorizontalAlignment = HorizontalAlignment.Center;
		}

		public override bool RequiresLayout()
		{
			return base.RequiresLayout() || Line.RequiresLayout();
		}

		public override void OnMouseDown(Point Location)
		{
			StatusElement Element;
			if(Line.HitTest(Location, out Element))
			{
				MouseDownElement = Element;
				MouseDownElement.bMouseDown = true;

				Invalidate();
			}
		}

		public override void OnMouseUp(Point Location)
		{
			if(MouseDownElement != null)
			{
				StatusElement ClickElement = MouseDownElement;

				MouseDownElement.bMouseDown = false;
				MouseDownElement = null;

				if(ClickElement.Bounds.Contains(Location))
				{
					ClickElement.OnClick(Location);
				}

				Invalidate();
			}
		}

		public override void OnMouseMove(Point Location)
		{
			StatusElement NewMouseOverElement;
			Line.HitTest(Location, out NewMouseOverElement);

			if(MouseOverElement != NewMouseOverElement)
			{
				if(MouseOverElement != null)
				{
					Cursor = Cursors.Arrow;
					MouseOverElement.bMouseOver = false;
				}

				MouseOverElement = NewMouseOverElement;

				if(MouseOverElement != null)
				{
					Cursor = MouseOverElement.Cursor;
					MouseOverElement.bMouseOver = true;
				}

				Invalidate();
			}
		}

		public override void OnMouseLeave()
		{
			if(MouseOverElement != null)
			{
				MouseOverElement.bMouseOver = false;
				MouseOverElement = null;

				Invalidate();
			}
		}

		public override void Layout(Graphics Graphics, Rectangle Bounds)
		{
			this.Bounds = Bounds;

			int OffsetY = Bounds.Y + Bounds.Height / 2;
			Line.Layout(Graphics, new Point(Bounds.X, OffsetY), Resources);

			if(HorizontalAlignment == HorizontalAlignment.Center)
			{
				int OffsetX = Bounds.X + (Bounds.Width - Line.Bounds.Width) / 2;
				Line.Layout(Graphics, new Point(OffsetX, OffsetY), Resources);
			}
			else if(HorizontalAlignment == HorizontalAlignment.Right)
			{
				int OffsetX = Bounds.Right - Line.Bounds.Width;
				Line.Layout(Graphics, new Point(OffsetX, OffsetY), Resources);
			}
		}

		public override void Render(Graphics Graphics)
		{
			Graphics.IntersectClip(Bounds);

			Line.Draw(Graphics, Resources);
		}
	}

	partial class CustomListViewControl : ListView
	{
		VisualStyleRenderer SelectedItemRenderer;
		VisualStyleRenderer TrackedItemRenderer;
		public int HoverItem = -1;

		CustomListViewWidget MouseOverWidget;
		CustomListViewWidget MouseDownWidget;

		public CustomListViewControl()
		{
            if (Application.RenderWithVisualStyles) 
            { 
				SelectedItemRenderer = new VisualStyleRenderer("Explorer::ListView", 1, 3);
				TrackedItemRenderer = new VisualStyleRenderer("Explorer::ListView", 1, 2); 
			}
		}

		protected void ConditionalLayoutWidget(CustomListViewWidget Widget)
		{
			if(Widget.Item.Tag == Widget)
			{
				Widget.ConditionalLayout(this, Widget.Item.Bounds);
			}
			else
			{
				for(int Idx = 0; Idx < Widget.Item.SubItems.Count; Idx++)
				{
					ListViewItem.ListViewSubItem SubItem = Widget.Item.SubItems[Idx];
					if(SubItem.Tag == Widget)
					{
						Rectangle Bounds = SubItem.Bounds;
						for(int EndIdx = Idx + 1; EndIdx < Widget.Item.SubItems.Count && Widget.Item.SubItems[EndIdx].Tag == Widget; EndIdx++)
						{
							Rectangle EndBounds = Widget.Item.SubItems[EndIdx].Bounds;
							Bounds = new Rectangle(Bounds.X, Bounds.Y, EndBounds.Right - Bounds.X, EndBounds.Bottom - Bounds.Y);
						}
						Widget.ConditionalLayout(this, Bounds);
						break;
					}
				}
			}
		}

		protected void ConditionalRedrawWidget(CustomListViewWidget Widget)
		{
			if(Widget.Item.Index != -1 && Widget.RequiresLayout())
			{
				ConditionalLayoutWidget(Widget);
				RedrawItems(Widget.Item.Index, Widget.Item.Index, true);
			}
		}

		protected CustomListViewWidget FindWidget(Point Location)
		{
			return FindWidget(HitTest(Location));
		}

		protected CustomListViewWidget FindWidget(ListViewHitTestInfo HitTest)
		{
			if(HitTest.Item != null)
			{
				CustomListViewWidget Widget = HitTest.Item.Tag as CustomListViewWidget;
				if(Widget != null)
				{
					return Widget;
				}
			}
			if(HitTest.SubItem != null)
			{
				CustomListViewWidget Widget = HitTest.SubItem.Tag as CustomListViewWidget;
				if(Widget != null)
				{
					return Widget;
				}
			}
			return null;
		}

		protected override void OnMouseLeave(EventArgs e)
		{
			base.OnMouseLeave(e);

			if(MouseOverWidget != null)
			{
				MouseOverWidget.OnMouseLeave();
				ConditionalRedrawWidget(MouseOverWidget);
				MouseOverWidget = null;
				Invalidate();
			}

			if(HoverItem != -1)
			{
				HoverItem = -1;
				Invalidate();
			}
		}

		protected override void OnMouseDown(MouseEventArgs e)
		{
			base.OnMouseDown(e);

			if(MouseDownWidget != null)
			{
				MouseDownWidget.OnMouseUp(e.Location);
				ConditionalRedrawWidget(MouseDownWidget);
			}

			if((e.Button & MouseButtons.Left) != 0)
			{
				MouseDownWidget = FindWidget(e.Location);
			}
			else
			{
				MouseDownWidget = null;
			}

			if(MouseDownWidget != null)
			{
				MouseDownWidget.OnMouseDown(e.Location);
				ConditionalRedrawWidget(MouseDownWidget);
			}
		}

		protected override void OnMouseUp(MouseEventArgs e)
		{
			base.OnMouseUp(e);

			if(MouseDownWidget != null)
			{
				CustomListViewWidget Widget = MouseDownWidget;
				MouseDownWidget = null;

				Widget.OnMouseUp(e.Location);
				ConditionalRedrawWidget(Widget);
			}
		}

		protected override void OnMouseMove(MouseEventArgs e)
		{
			ListViewHitTestInfo HitTest = this.HitTest(e.Location);

			CustomListViewWidget NewMouseOverWidget = FindWidget(HitTest);
			if(MouseOverWidget != null && MouseOverWidget != NewMouseOverWidget)
			{
				Cursor = Cursors.Arrow;
				MouseOverWidget.OnMouseLeave();
				ConditionalRedrawWidget(MouseOverWidget);
			}
			MouseOverWidget = NewMouseOverWidget;
			if(MouseOverWidget != null)
			{
				Cursor = MouseOverWidget.Cursor;
				MouseOverWidget.OnMouseMove(e.Location);
				ConditionalRedrawWidget(MouseOverWidget);
			}

			int PrevHoverItem = HoverItem;
			HoverItem = (HitTest.Item == null)? -1 : HitTest.Item.Index;
			if(HoverItem != PrevHoverItem)
			{
				if(HoverItem != -1)
				{
					RedrawItems(HoverItem, HoverItem, true);
				}
				if(PrevHoverItem != -1 && PrevHoverItem < Items.Count)
				{
					RedrawItems(PrevHoverItem, PrevHoverItem, true);
				}
			}

			base.OnMouseMove(e);
		}
		public void DrawText(Graphics Graphics, Rectangle Bounds, HorizontalAlignment TextAlign, Color TextColor, string Text)
		{
			TextFormatFlags Flags = TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix;
			if (TextAlign == HorizontalAlignment.Left)
			{
				Flags |= TextFormatFlags.Left;
			}
			else if (TextAlign == HorizontalAlignment.Center)
			{
				Flags |= TextFormatFlags.HorizontalCenter;
			}
			else
			{
				Flags |= TextFormatFlags.Right;
			}
			TextRenderer.DrawText(Graphics, Text, Font, Bounds, TextColor, Flags);
		}

		public void DrawIcon(Graphics Graphics, Rectangle Bounds, Rectangle Icon)
		{
			float DpiScaleX = Graphics.DpiX / 96.0f;
			float DpiScaleY = Graphics.DpiY / 96.0f;

			float IconX = Bounds.Left + (Bounds.Width - 16 * DpiScaleX) / 2;
			float IconY = Bounds.Top + (Bounds.Height - 16 * DpiScaleY) / 2;

			Graphics.DrawImage(Properties.Resources.Icons, IconX, IconY, Icon, GraphicsUnit.Pixel);
		}

		public void DrawNormalSubItem(DrawListViewSubItemEventArgs e)
		{
			DrawText(e.Graphics, e.SubItem.Bounds, Columns[e.ColumnIndex].TextAlign, SystemColors.WindowText, e.SubItem.Text);
		}

		public void DrawCustomSubItem(Graphics Graphics, ListViewItem.ListViewSubItem SubItem)
		{
			CustomListViewWidget Widget = SubItem.Tag as CustomListViewWidget;
			if(Widget != null)
			{
				foreach(ListViewItem.ListViewSubItem OtherSubItem in Widget.Item.SubItems)
				{
					if(OtherSubItem.Tag == Widget)
					{
						if(OtherSubItem == SubItem)
						{
							ConditionalLayoutWidget(Widget);
							Widget.Render(Graphics);
						}
						break;
					}
				}
			}
		}

		public void DrawBackground(Graphics Graphics, ListViewItem Item)
		{
			if(Item.Selected)
			{
				DrawSelectedBackground(Graphics, Item.Bounds);
			}
			else if(Item.Index == HoverItem)
			{
				DrawTrackedBackground(Graphics, Item.Bounds);
			}
			else
			{
				DrawDefaultBackground(Graphics, Item.Bounds);
			}
		}

		public void DrawDefaultBackground(Graphics Graphics, Rectangle Bounds)
		{
			Graphics.FillRectangle(SystemBrushes.Window, Bounds);
		}

		public void DrawSelectedBackground(Graphics Graphics, Rectangle Bounds)
		{
			if(Application.RenderWithVisualStyles)
			{
				SelectedItemRenderer.DrawBackground(Graphics, Bounds);
			}
			else
			{
				Graphics.FillRectangle(SystemBrushes.ButtonFace, Bounds);
			}
		}

		public void DrawTrackedBackground(Graphics Graphics, Rectangle Bounds)
		{
			if(Application.RenderWithVisualStyles)
			{
				TrackedItemRenderer.DrawBackground(Graphics, Bounds);
			}
			else
			{
				Graphics.FillRectangle(SystemBrushes.Window, Bounds);
			}
		}
	}
}
