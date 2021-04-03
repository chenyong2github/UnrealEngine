// Copyright Epic Games, Inc. All Rights Reserved.

using System.Windows;


namespace nDisplayLauncher
{
	public enum UELogVerbosity
	{
		All = 0,
		Verbose,
		Log,
		Display,
		Warning,
		Error,
		Fatal
	}

	public partial class MainWindow
	{
		private void InitializeLog()
		{
			CtrlLogTab.DataContext = TheLauncher;
		}

		private void ctrlBtnSetVerbosityAll_Click(object sender, RoutedEventArgs e)
		{
			UELogVerbosity Verbosity = UELogVerbosity.Log;

			if (sender == ctrlBtnSetVerbosityAll)
			{
				Verbosity = UELogVerbosity.All;
			}
			else if (sender == ctrlBtnSetVerbosityVerbose)
			{
				Verbosity = UELogVerbosity.Verbose;
			}
			else if (sender == ctrlBtnSetVerbosityLog)
			{
				Verbosity = UELogVerbosity.Log;
			}
			else if (sender == ctrlBtnSetVerbosityDisplay)
			{
				Verbosity = UELogVerbosity.Display;
			}
			else if (sender == ctrlBtnSetVerbosityWarning)
			{
				Verbosity = UELogVerbosity.Warning;
			}
			else if (sender == ctrlBtnSetVerbosityError)
			{
				Verbosity = UELogVerbosity.Error;
			}
			else if (sender == ctrlBtnSetVerbosityFatal)
			{
				Verbosity = UELogVerbosity.Fatal;
			}
			else
			{
				return;
			}

			TheLauncher.SelectedVerbocityPlugin     = Verbosity;
			TheLauncher.SelectedVerbocityEngine     = Verbosity;
			TheLauncher.SelectedVerbocityConfig     = Verbosity;
			TheLauncher.SelectedVerbocityCluster    = Verbosity;
			TheLauncher.SelectedVerbocityGame       = Verbosity;
			TheLauncher.SelectedVerbocityGameMode   = Verbosity;
			TheLauncher.SelectedVerbocityInput      = Verbosity;
			TheLauncher.SelectedVerbocityVrpn       = Verbosity;
			TheLauncher.SelectedVerbocityNetwork    = Verbosity;
			TheLauncher.SelectedVerbocityNetworkMsg = Verbosity;
			TheLauncher.SelectedVerbocityRender     = Verbosity;
			TheLauncher.SelectedVerbocityRenderSync = Verbosity;
			TheLauncher.SelectedVerbocityBlueprint  = Verbosity;
		}
	}
}
