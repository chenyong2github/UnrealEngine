// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync.Forms
{
	partial class NetCoreWindow : Form
	{
		UserSettings Settings;

		public NetCoreWindow(UserSettings Settings)
		{
			this.Settings = Settings;

			InitializeComponent();
		}

		private void SnoozeBtn_Click(object sender, EventArgs e)
		{
			Close();
		}

		private void DismissBtn_Click(object sender, EventArgs e)
		{
			Settings.bShowNetCoreInfo = false;
			Settings.Save();

			Close();
		}

		private void linkLabel1_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
		{
			System.Diagnostics.Process.Start("https://dotnet.microsoft.com/download/dotnet-core/current/runtime");
		}
	}
}
