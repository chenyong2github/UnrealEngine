// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Text.RegularExpressions;
using System.Drawing;
using EpicGames.Core;

#if WINDOWS
using System.Windows.Forms;
#endif

namespace Turnkey
{
	class HybridIOProvider : ConsoleIOProvider
	{
		static HybridIOProvider()
		{
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
			// Create a new instance of the form.
			Form Form1 = new Form();

			TextBox Label;
			Button CancelBtn;
			Button OkBtn;
			TextBox TextBox;

			TextBox = new TextBox();
			Label = new TextBox();
			CancelBtn = new Button();
			OkBtn = new Button();
			Form1.SuspendLayout();
			// 
			// Label
			// 
			Label.Anchor = ((AnchorStyles)(((AnchorStyles.Top | AnchorStyles.Left)
			| AnchorStyles.Right | AnchorStyles.Bottom)));
			Label.BackColor = System.Drawing.SystemColors.Control;
			Label.BorderStyle = BorderStyle.None;
			Label.Location = new System.Drawing.Point(13, 13);
			Label.Multiline = true;
			Label.Name = "Label";
			Label.ScrollBars = ScrollBars.Vertical;
			Label.ShortcutsEnabled = false;
			Label.Size = new System.Drawing.Size(775, 199);
			Label.TabIndex = 0;
			Label.TabStop = false;
			Label.Text = Prompt;

			// 
			// CancelBtn
			// 
			CancelBtn.Anchor = ((AnchorStyles)((AnchorStyles.Right | AnchorStyles.Bottom)));
			CancelBtn.DialogResult = DialogResult.Cancel;
			CancelBtn.Location = new System.Drawing.Point(679, 262);
			CancelBtn.Margin = new Padding(4);
			CancelBtn.Name = "CancelBtn";
			CancelBtn.Size = new System.Drawing.Size(109, 34);
			CancelBtn.TabIndex = 6;
			CancelBtn.Text = "Cancel";
			CancelBtn.UseVisualStyleBackColor = true;
			// 
			// TextBox
			// 
			TextBox.Anchor = ((AnchorStyles)(((AnchorStyles.Left | AnchorStyles.Bottom)
			| AnchorStyles.Right)));
			TextBox.Location = new System.Drawing.Point(13, 219);
			TextBox.Margin = new Padding(4);
			TextBox.Name = "TextBox";
			TextBox.Size = new System.Drawing.Size(772, 22);
			TextBox.TabIndex = 4;
			TextBox.Text = Default;
			// 
			// OkBtn
			// 
			OkBtn.Anchor = ((AnchorStyles)((AnchorStyles.Right | AnchorStyles.Bottom)));
			OkBtn.DialogResult = DialogResult.OK;
			OkBtn.Location = new System.Drawing.Point(562, 262);
			OkBtn.Margin = new Padding(4);
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
			Form1.AutoScaleMode = AutoScaleMode.Font;
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

		public override void PauseForUser(string Message, bool bAppendNewLine)
		{
			ShowDialog(Message, "", false);
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
				if (Option.StartsWith(";"))
				{
					FullPromptBuilder.AppendLine(" {0}", Option.Substring(1));
				}
				else
				{
					FullPromptBuilder.AppendLine(" ({0}) {1}", Index++, Option);
				}
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
