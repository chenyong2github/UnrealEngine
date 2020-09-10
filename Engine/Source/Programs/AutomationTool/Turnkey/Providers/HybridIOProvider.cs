// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Text.RegularExpressions;
using System.Drawing;
using Tools.DotNETCommon;

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

			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);
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

		private string ShowDialog(string Prompt, string Default, bool bIsList)
		{
			string Result;
			if (UnrealBuildTool.BuildHostPlatform.Current.Platform == UnrealBuildTool.UnrealTargetPlatform.Mac)
			{
				Result = ShowMacDialog(Prompt, Default);
			}
			else
			{
				Result = Microsoft.VisualBasic.Interaction.InputBox(Prompt, "Turnkey Input", Default);
			}

			if (string.IsNullOrEmpty(Result) && bIsList)
			{
				return "0";
			}

			return Result;
/*
			// Create a new instance of the form.
			Form Form1 = new Form();

			System.Windows.Forms.TextBox Label;
			System.Windows.Forms.Button CancelBtn;
			System.Windows.Forms.Button OkBtn;
			System.Windows.Forms.TextBox TextBox;

			TextBox = new System.Windows.Forms.TextBox();
			Label = new System.Windows.Forms.TextBox();
			CancelBtn = new System.Windows.Forms.Button();
			OkBtn = new System.Windows.Forms.Button();
			Form1.SuspendLayout();
			// 
			// Label
			// 
			Label.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left)
			| System.Windows.Forms.AnchorStyles.Right | System.Windows.Forms.AnchorStyles.Bottom)));
			Label.BackColor = System.Drawing.SystemColors.Control;
			Label.BorderStyle = System.Windows.Forms.BorderStyle.None;
			Label.Location = new System.Drawing.Point(13, 13);
			Label.Multiline = true;
			Label.Name = "Label";
			Label.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
			Label.ShortcutsEnabled = false;
			Label.Size = new System.Drawing.Size(775, 199);
			Label.TabIndex = 0;
			Label.TabStop = false;
			Label.Text = Prompt;

			// 
			// CancelBtn
			// 
			CancelBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Right | System.Windows.Forms.AnchorStyles.Bottom)));
			CancelBtn.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			CancelBtn.Location = new System.Drawing.Point(679, 262);
			CancelBtn.Margin = new System.Windows.Forms.Padding(4);
			CancelBtn.Name = "CancelBtn";
			CancelBtn.Size = new System.Drawing.Size(109, 34);
			CancelBtn.TabIndex = 6;
			CancelBtn.Text = "Cancel";
			CancelBtn.UseVisualStyleBackColor = true;
			// 
			// TextBox
			// 
			TextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Bottom)
			| System.Windows.Forms.AnchorStyles.Right)));
			TextBox.Location = new System.Drawing.Point(13, 219);
			TextBox.Margin = new System.Windows.Forms.Padding(4);
			TextBox.Name = "TextBox";
			TextBox.Size = new System.Drawing.Size(772, 22);
			TextBox.TabIndex = 4;
			TextBox.Text = Default;
			// 
			// OkBtn
			// 
			OkBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Right | System.Windows.Forms.AnchorStyles.Bottom)));
			OkBtn.DialogResult = System.Windows.Forms.DialogResult.OK;
			OkBtn.Location = new System.Drawing.Point(562, 262);
			OkBtn.Margin = new System.Windows.Forms.Padding(4);
			OkBtn.Name = "OkBtn";
			OkBtn.Size = new System.Drawing.Size(109, 34);
			OkBtn.TabIndex = 5;
			OkBtn.Text = "Ok";
			OkBtn.UseVisualStyleBackColor = true;
			// 
			// Form1
			// 
			Form1.AcceptButton = OkBtn;
			Form1.StartPosition = FormStartPosition.CenterScreen;
			Form1.AutoScaleDimensions = new System.Drawing.SizeF(8F, 16F);
			Form1.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
			Form1.CancelButton = CancelBtn;
			Form1.ClientSize = new System.Drawing.Size(800, 315);
			Form1.Controls.Add(CancelBtn);
			Form1.Controls.Add(OkBtn);
			Form1.Controls.Add(TextBox);
			Form1.Controls.Add(Label);
			Form1.Name = "Form1";
			Form1.Text = "Make selection";
			Form1.ResumeLayout(false);
			Form1.PerformLayout();

			if (!bIsList)
			{
				Form1.Height -= 150;
				Form1.PerformLayout();
			}

			// Display the form as a modal dialog box.
			Form1.ShowDialog();

			string Result = null;

			// Determine if the OK button was clicked on the dialog box.
			if (Form1.DialogResult == DialogResult.OK)
			{
				Result = TextBox.Text;
			}

			Form1.Dispose();
			return Result;
*/
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
