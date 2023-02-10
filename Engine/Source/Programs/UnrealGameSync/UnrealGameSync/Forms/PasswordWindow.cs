// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Windows.Forms;

namespace UnrealGameSync
{
	public partial class PasswordWindow : Form
	{
		public string Password { get; private set; }

		public PasswordWindow(string prompt, string password)
		{
			InitializeComponent();

			Password = password;

			PromptLabel.Text = prompt;
			PasswordTextBox.Text = password;
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			Password = PasswordTextBox.Text;
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{

		}
	}
}
