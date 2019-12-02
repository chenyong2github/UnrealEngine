// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	partial class IssueSettingsWindow : Form
	{
		UserSettings Settings;

		public IssueSettingsWindow(UserSettings Settings)
		{
			this.Settings = Settings;

			InitializeComponent();

			if(Settings.NotifyUnassignedMinutes < 0)
			{
				NotifyUnassignedCheckBox.Checked = false;
				NotifyUnassignedTextBox.Text = "5";
			}
			else
			{
				NotifyUnassignedCheckBox.Checked = true;
				NotifyUnassignedTextBox.Text = Settings.NotifyUnassignedMinutes.ToString();
			}

			if (Settings.NotifyUnacknowledgedMinutes < 0)
			{
				NotifyUnacknowledgedCheckBox.Checked = false;
				NotifyUnacknowledgedTextBox.Text = "5";
			}
			else
			{
				NotifyUnacknowledgedCheckBox.Checked = true;
				NotifyUnacknowledgedTextBox.Text = Settings.NotifyUnacknowledgedMinutes.ToString();
			}

			if (Settings.NotifyUnresolvedMinutes < 0)
			{
				NotifyUnresolvedCheckBox.Checked = false;
				NotifyUnresolvedTextBox.Text = "20";
			}
			else
			{
				NotifyUnresolvedCheckBox.Checked = true;
				NotifyUnresolvedTextBox.Text = Settings.NotifyUnresolvedMinutes.ToString();
			}

			UpdateEnabledTextBoxes();
		}

		private void UpdateEnabledTextBoxes()
		{
			NotifyUnassignedTextBox.Enabled = NotifyUnassignedCheckBox.Checked;
			NotifyUnacknowledgedTextBox.Enabled = NotifyUnacknowledgedCheckBox.Checked;
			NotifyUnresolvedTextBox.Enabled = NotifyUnresolvedCheckBox.Checked;
		}

		private void NotifyUnassignedCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledTextBoxes();
		}

		private void NotifyUnacknowledgedCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledTextBoxes();
		}

		private void NotifyUnresolvedCheckBox_CheckedChanged(object sender, EventArgs e)
		{
			UpdateEnabledTextBoxes();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			int NewNotifyUnresolvedMinutes = -1;
			if(NotifyUnresolvedCheckBox.Checked)
			{
				ushort NewNotifyUnresolvedMinutesValue;
				if(!ushort.TryParse(NotifyUnresolvedTextBox.Text, out NewNotifyUnresolvedMinutesValue))
				{
					MessageBox.Show("Invalid time");
					return;
				}
				NewNotifyUnresolvedMinutes = NewNotifyUnresolvedMinutesValue;
			}

			int NewNotifyUnacknowledgedMinutes = -1;
			if (NotifyUnacknowledgedCheckBox.Checked)
			{
				ushort NewNotifyUnacknowledgedMinutesValue;
				if (!ushort.TryParse(NotifyUnacknowledgedTextBox.Text, out NewNotifyUnacknowledgedMinutesValue))
				{
					MessageBox.Show("Invalid time");
					return;
				}
				NewNotifyUnacknowledgedMinutes = NewNotifyUnacknowledgedMinutesValue;
			}

			int NewNotifyUnassignedMinutes = -1;
			if(NotifyUnassignedCheckBox.Checked)
			{
				ushort NewNotifyUnassignedMinutesValue;
				if(!ushort.TryParse(NotifyUnassignedTextBox.Text, out NewNotifyUnassignedMinutesValue))
				{
					MessageBox.Show("Invalid time");
					return;
				}
				NewNotifyUnassignedMinutes = NewNotifyUnassignedMinutesValue;
			}

			Settings.NotifyUnresolvedMinutes = NewNotifyUnresolvedMinutes;
			Settings.NotifyUnacknowledgedMinutes = NewNotifyUnacknowledgedMinutes;
			Settings.NotifyUnassignedMinutes = NewNotifyUnassignedMinutes;
			Settings.Save();

			DialogResult = DialogResult.Cancel;
			Close();
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}
	}
}
