// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Text.RegularExpressions;
using System.Drawing;
using Tools.DotNETCommon;

#if WINDOWS
using System.Windows.Forms;
#endif

namespace Turnkey
{
	class HybridIOProvider : ConsoleIOProvider
	{
		// @todo turnkey: if this is really bad on Mono, then we can set it in the manifest, but that means changing AutomationTool's and ATLaunchers' manifests
		[System.Runtime.InteropServices.DllImport("user32.dll")]
		private static extern bool SetProcessDPIAware();
		static HybridIOProvider()
		{
			// make the form look good on modern displays!
			if (!UnrealBuildTool.Utils.IsRunningOnMono && Environment.OSVersion.Version.Major >= 6)
			{
				SetProcessDPIAware();
			}

#if WINDOWS
			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);
#endif

		}

		private string ShowMacDialog(string Prompt, string Default)
		{
			string Params = string.Format("-e 'display dialog \"{0}\" with title \"Turnkey Input\" default answer \"{1}\"'", Prompt.Replace("\n", "\\n").Replace("\t", "\\t"), Default);
			string OSAOutput = UnrealBuildTool.Utils.RunLocalProcessAndReturnStdOut("osascript", Params);

			// blank string means user canceled, which goes to stderr
			if (OSAOutput == "")
			{
				return null;
			}

			// regex the result
			Match Match = Regex.Match(OSAOutput, "text returned:(.*)$");
			if (!Match.Success)
			{
				return null;
			}

			// return the text in the dialog box
			return Match.Groups[1].Value;
		}

#if WINDOWS
		private string ShowFormsDialogs(string Prompt, string Default)
		{
			// TODO: Make sure this dialog actually looks like we expect
			Size size = new Size(200, 70);
			Form inputBox = new Form();

			inputBox.FormBorderStyle = FormBorderStyle.FixedDialog;
			inputBox.ClientSize = size;
			inputBox.Text = "Turnkey Input";

			TextBox textBox = new TextBox();
			textBox.Size = new Size(size.Width - 10, 23);
			textBox.Location = new Point(5, 5);
			textBox.Text = Prompt;
			inputBox.Controls.Add(textBox);

			Button okButton = new Button();
			okButton.DialogResult = DialogResult.OK;
			okButton.Name = "okButton";
			okButton.Size = new Size(75, 23);
			okButton.Text = "&OK";
			okButton.Location = new Point(size.Width - 80 - 80, 39);
			inputBox.Controls.Add(okButton);

			Button cancelButton = new Button();
			cancelButton.DialogResult = DialogResult.Cancel;
			cancelButton.Name = "cancelButton";
			cancelButton.Size = new Size(75, 23);
			cancelButton.Text = "&Cancel";
			cancelButton.Location = new Point(size.Width - 80, 39);
			inputBox.Controls.Add(cancelButton);

			inputBox.AcceptButton = okButton;
			inputBox.CancelButton = cancelButton;

			DialogResult result = inputBox.ShowDialog();
			if (result == DialogResult.OK)
			{
				return textBox.Text;
			}
			else
			{
				return Default;
			}
		}
#endif

		private string ShowDialog(string Prompt, string Default, bool bIsList)
		{
// disable unreachable code warning as this fails on linux builds due to this method not being implemented
#pragma warning disable 0162
			string Result;
#if WINDOWS
			Result = ShowFormsDialogs(Prompt, Default);
#elif OSX
			Result = ShowMacDialog(Prompt, Default);
#else
			throw new NotImplementedException("Linux dialog not implemented");
#endif
			if (string.IsNullOrEmpty(Result) && bIsList)
			{
				return "0";
			}

			return Result;
#pragma warning restore
		}

		public override string ReadInput(string Prompt, string Default, bool bAppendNewLine)
		{
			return ShowDialog(Prompt, Default, false);
		}

		public override int ReadInputInt(string Prompt, List<string> Options, bool bIsCancellable, int DefaultValue, bool bAppendNewLine)
		{
			StringBuilder FullPromptBuilder = new StringBuilder();

			// start with given prompt
			FullPromptBuilder.Append(Prompt);
			if (bAppendNewLine)
			{
				FullPromptBuilder.AppendLine("");
			}

			// now add the options given
			int Index = 1;
			foreach (string Option in Options)
			{
				FullPromptBuilder.AppendLine(" ({0}) {1}", Index++, Option);
			}

			string FullPrompt = FullPromptBuilder.ToString();

			// go until good choice
			while (true)
			{
				string ChoiceString = ShowDialog(FullPrompt, DefaultValue >= 0 ? DefaultValue.ToString() : null, true);

				if (ChoiceString == null)
				{
					return bIsCancellable ? 0 : -1;
				}

				int Choice;
				if (Int32.TryParse(ChoiceString, out Choice) == false || Choice < 0 || Choice >= Options.Count + (bIsCancellable ? 1 : 0))
				{
					if (Choice < 0 && bIsCancellable)
					{
						return 0;
					}
					TurnkeyUtils.Log("Invalid choice");
				}
				else
				{
					return Choice;
				}
			}
		}
	}
}
