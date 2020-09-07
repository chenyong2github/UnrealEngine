// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Drawing;
using System.Windows.Forms;
using UnrealGameSync.Controls;

namespace UnrealGameSync.Forms
{
	public partial class OIDCLoginWindow : Form
	{
		public OIDCLoginWindow(OIDCTokenManager OidcManager)
		{
			InitializeComponent();

			int LastYPosition = 10;
			foreach (OIDCTokenManager.ProviderInfo Provider in OidcManager.Providers.Values)
			{
				OIDCControl ServiceControl = new OIDCControl(OidcManager, Provider.Identifier, Provider.DisplayName)
				{
					Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
					Location = new Point(10, LastYPosition)
				};
				Controls.Add(ServiceControl);

				LastYPosition = ServiceControl.Size.Height + ServiceControl.Location.Y + 10;
			}
		}

		private void DoneButton_Click(object sender, EventArgs e)
		{
			Close();
		}
	}
}
