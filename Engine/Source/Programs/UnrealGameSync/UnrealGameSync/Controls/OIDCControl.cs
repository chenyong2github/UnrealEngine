// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace UnrealGameSync.Controls
{
	public partial class OIDCControl : UserControl
	{
		private Font BadgeFont;
		private readonly OIDCTokenManager OidcManager;
		private readonly string ProviderIdentifier;

		public OIDCControl(OIDCTokenManager InOidcManager, string InProviderIdentifier, string ServiceName)
		{
			InitializeComponent();

			OIDCControlGroupBox.Text = ServiceName;
			OidcManager = InOidcManager;
			ProviderIdentifier = InProviderIdentifier;

			BadgeFont = new Font(this.Font.FontFamily, this.Font.SizeInPoints - 2, FontStyle.Bold);

			OIDCStatus ServiceStatus = OidcManager.GetStatusForProvider(ProviderIdentifier);
			LoginButton.Enabled = ServiceStatus == OIDCStatus.NotLoggedIn;
		}

		private void DrawBadge(Graphics Graphics, Rectangle BadgeRect, string BadgeText, Color BadgeColor, bool bMergeLeft, bool bMergeRight)
		{
			if (BadgeColor.A != 0)
			{
				using (GraphicsPath Path = new GraphicsPath())
				{
					Path.StartFigure();
					Path.AddLine(BadgeRect.Left + (bMergeLeft ? 1 : 0), BadgeRect.Top, BadgeRect.Left - (bMergeLeft ? 1 : 0), BadgeRect.Bottom);
					Path.AddLine(BadgeRect.Left - (bMergeLeft ? 1 : 0), BadgeRect.Bottom, BadgeRect.Right - 1 - (bMergeRight ? 1 : 0), BadgeRect.Bottom);
					Path.AddLine(BadgeRect.Right - 1 - (bMergeRight ? 1 : 0), BadgeRect.Bottom, BadgeRect.Right - 1 + (bMergeRight ? 1 : 0), BadgeRect.Top);
					Path.AddLine(BadgeRect.Right - 1 + (bMergeRight ? 1 : 0), BadgeRect.Top, BadgeRect.Left + (bMergeLeft ? 1 : 0), BadgeRect.Top);
					Path.CloseFigure();

					using (SolidBrush Brush = new SolidBrush(BadgeColor))
					{
						Graphics.FillPath(Brush, Path);
					}
				}

				TextRenderer.DrawText(Graphics, BadgeText, BadgeFont, BadgeRect, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.PreserveGraphicsClipping);
			}
		}

		private void StatusPanel_Paint(object sender, PaintEventArgs e)
		{
			OIDCStatus ServiceStatus = OidcManager.GetStatusForProvider(ProviderIdentifier);
			DrawBadge(e.Graphics, e.ClipRectangle, ToServiceDisplayText(ServiceStatus), ToServiceDisplayColor(ServiceStatus), false, false);
		}

		private Color ToServiceDisplayColor(OIDCStatus OidcStatus)
		{
			switch (OidcStatus)
			{
				case OIDCStatus.Connected:
				case OIDCStatus.TokenRefreshRequired:
					return Color.Green;
				case OIDCStatus.NotLoggedIn:
					return Color.Red;
				default:
					throw new ArgumentOutOfRangeException(nameof(OidcStatus), OidcStatus, null);
			}
		}

		private string ToServiceDisplayText(OIDCStatus OidcStatus)
		{
			switch (OidcStatus)
			{
				case OIDCStatus.Connected:
					return "Connected";
				case OIDCStatus.NotLoggedIn:
					return "Not Logged In";
				case OIDCStatus.TokenRefreshRequired:
					return "Refresh Pending";
				default:
					throw new ArgumentOutOfRangeException(nameof(OidcStatus), OidcStatus, null);
			}
		}

		private async void LoginButton_Click(object sender, EventArgs e)
		{
			try
			{
				await OidcManager.Login(ProviderIdentifier);

				Focus();

				OIDCStatus ServiceStatus = OidcManager.GetStatusForProvider(ProviderIdentifier);
				LoginButton.Enabled = ServiceStatus == OIDCStatus.NotLoggedIn;

				Refresh();
			}
			catch (LoginFailedException Exception)
			{
				MessageBox.Show(Exception.Message, "Login Failed", MessageBoxButtons.OK);
			}
		}
	}
}
