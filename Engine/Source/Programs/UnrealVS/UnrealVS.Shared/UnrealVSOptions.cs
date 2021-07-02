// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.VisualStudio.Shell;
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace UnrealVS
{
	[Guid(GuidList.UnrealVSOptionsString)]
	public class UnrealVsOptions : DialogPage
	{
		public event EventHandler OnOptionsChanged;

		[Category("General")]
		[DisplayName("Hide Non-Game Startup Projects")]
		[Description("Shows only game projects in the startup project and batch-builder lists")]
		public bool HideNonGameStartupProjects { get; set; }

		[Category("Unreal.P4")]
		[DisplayName("Enable auto checkout on save")]
		[Description("Uses p4 ini / environment settings to automatically checkout files on save, use 'RunUAT P4WriteConfig' to initialize those settings")]
		public bool AllowUnrealVSCheckoutOnEdit { get; set; }

		[Category("Unreal.P4")]
		[DisplayName("Override VS compare options")]
		[Description("Unreal VS will override built in diff settings to the ideal for code, does not alter P4")]
		public bool AllowUnrealVSOverrideDiffSettings { get; set; }

		[Category("Unreal.P4")]
		[DisplayName("Allow perforce operations")]
		[Description("Uses p4 ini / environment settings to call P4 functionlality, use 'RunUAT P4WriteConfig' to initialize those settings")]
		public bool AllowUnrealVSP4 { get; set; }

		protected override void OnApply(PageApplyEventArgs e)
		{
			base.OnApply(e);

			if (e.ApplyBehavior == ApplyKind.Apply && OnOptionsChanged != null)
			{
				OnOptionsChanged(this, EventArgs.Empty);
			}
		}
	}


}
