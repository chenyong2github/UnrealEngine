// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace DatasmithRevitExporter
{
	public class DatasmithRevitParamGroupsDialog : Form
	{
		public static SortedList<string, int> AllBuiltinParamGroups { get; private set; }
		public CheckedListBox GroupsList { get; private set; }

		public DatasmithRevitParamGroupsDialog()
		{
			GroupsList = new CheckedListBox();
			GroupsList.Size = new Size(310, 350);

			if (AllBuiltinParamGroups == null)
			{
				AllBuiltinParamGroups = new SortedList<string, int>();

				foreach (Autodesk.Revit.DB.BuiltInParameterGroup BIP in Enum.GetValues(typeof(Autodesk.Revit.DB.BuiltInParameterGroup)))
				{
					string GroupLabel = Autodesk.Revit.DB.LabelUtils.GetLabelFor(BIP);

					if (!AllBuiltinParamGroups.ContainsKey(GroupLabel))
					{
						AllBuiltinParamGroups.Add(GroupLabel, (int)BIP);
					}
				}
			}

			foreach (var KV in AllBuiltinParamGroups)
			{
				GroupsList.Items.Add(KV.Key);
			}

			Button OKButton = new Button();
			OKButton.Name = "OKButton";
			OKButton.Text = DatasmithRevitResources.Strings.ExportOptionsDialog_ButtonOK;
			OKButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			OKButton.DialogResult = DialogResult.OK;
			OKButton.Location = new Point(211, 68);
			OKButton.Margin = new Padding(78, 0, 3, 3);
			OKButton.Size = new Size(75, 23);
			OKButton.TabIndex = 4;
			OKButton.UseVisualStyleBackColor = true;

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 1;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 2;
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.Size = new Size(300, 100);
			DialogLayout.TabIndex = 0;

			DialogLayout.Controls.Add(GroupsList, 0,0);
			DialogLayout.Controls.Add(OKButton, 0,1);

			Name = "AddGroups";
			Text = DatasmithRevitResources.Strings.SettingsDialog_AddGroupsDialogTitle; // "Add Groups";
			AcceptButton = OKButton;
			AutoScaleDimensions = new SizeF(6F, 13F);
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ClientSize = new Size(309, 114);
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			Padding = new Padding(10);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);
		}
	}

	public class DatasmithRevitSettingsDialog : Form
	{
		private CheckedListBox GroupsList;
		private TextBox ParamNameTextBox;
		private FMetadataSettings MetadataSettings;
		private Autodesk.Revit.DB.Document Document;
		private SortedList<string, int> AddedBuiltinParamGroups = new SortedList<string, int>();
		private List<string> AddedParamNames = new List<string>();

		public DatasmithRevitSettingsDialog(Autodesk.Revit.DB.Document InDocument)
		{
			Document = InDocument;
			FormClosing += OnClosing;

			MetadataSettings = FMetadataManager.CurrentSettings;

			ToolTip OptionToolTip = new ToolTip();
			OptionToolTip.AutoPopDelay = 10000; // milliseconds

			Label DescriptionLabel = new Label();
			DescriptionLabel.Name = "MatchParameterName";
			DescriptionLabel.Text = DatasmithRevitResources.Strings.SettingsDialog_LabelMetadataDescription;
			DescriptionLabel.Anchor = AnchorStyles.Left;
			DescriptionLabel.AutoSize = false;
			DescriptionLabel.TabIndex = 0;
			DescriptionLabel.Font = new Font(FontFamily.GenericSansSerif, 8f);
			DescriptionLabel.BorderStyle = BorderStyle.FixedSingle;
			DescriptionLabel.BackColor = Color.Wheat;
			DescriptionLabel.TextAlign = ContentAlignment.MiddleLeft;
			DescriptionLabel.Margin = new Padding(0, 10, 0, 20);
			DescriptionLabel.Width = 385;
			DescriptionLabel.Height = 55;

			Label ParamNameLabel = new Label();
			ParamNameLabel.Name = "MatchParameterNames";
			ParamNameLabel.Text = DatasmithRevitResources.Strings.SettingsDialog_LabelMatchParamNames;
			ParamNameLabel.Anchor = AnchorStyles.Left;
			ParamNameLabel.AutoSize = true;
			ParamNameLabel.TabIndex = 0;
			ParamNameLabel.TextAlign = ContentAlignment.MiddleLeft;
			ParamNameLabel.Margin = new Padding(0, 0, 0, 10);
			OptionToolTip.SetToolTip(ParamNameLabel, "Paramter names to match, separated by new lines (can be partial names).\nCase insensitive.");

			ParamNameTextBox = new TextBox();
			ParamNameTextBox.Width = 385;
			ParamNameTextBox.Height = 150;
			ParamNameTextBox.TabStop = false;
			ParamNameTextBox.Multiline = true;
			ParamNameTextBox.Margin = new Padding(0, 0, 0, 20);

			Label GroupsLabel = new Label();
			GroupsLabel.Name = "GroupsLabel";
			GroupsLabel.Text = DatasmithRevitResources.Strings.SettingsDialog_LabelMatchGroups;
			GroupsLabel.Anchor = AnchorStyles.Left;
			GroupsLabel.AutoSize = true;

			GroupsList = new CheckedListBox();
			GroupsList.Size = new Size(380, 200);

			Button AddGroupButton = new Button();
			AddGroupButton.Name = "Add";
			AddGroupButton.Text = DatasmithRevitResources.Strings.SettingsDialog_ButtonAddGroups;
			AddGroupButton.AutoSize = true;
			AddGroupButton.Anchor = AnchorStyles.Left;
			AddGroupButton.UseVisualStyleBackColor = true;
			AddGroupButton.Click += (s, e) =>
			{
				DatasmithRevitParamGroupsDialog Dlg = new DatasmithRevitParamGroupsDialog();
				if (DialogResult.OK == Dlg.ShowDialog())
				{
					bool bAddedNewGroups = false;
					foreach (int Index in Dlg.GroupsList.CheckedIndices)
					{
						string GroupLabel = DatasmithRevitParamGroupsDialog.AllBuiltinParamGroups.Keys[Index];
						if (!AddedBuiltinParamGroups.ContainsKey(GroupLabel))
						{
							int GroupId;
							if (DatasmithRevitParamGroupsDialog.AllBuiltinParamGroups.TryGetValue(GroupLabel, out GroupId))
							{
								AddedBuiltinParamGroups.Add(GroupLabel, GroupId);
								bAddedNewGroups = true;
							}
						}
					}

					if (bAddedNewGroups)
					{
						ReloadAddedGroupsList();
					}
				}
			};

			Button RemoveGroupButton = new Button();
			RemoveGroupButton.Name = "Remove";
			RemoveGroupButton.Text = DatasmithRevitResources.Strings.SettingsDialog_ButtonRemoveGroups;
			RemoveGroupButton.AutoSize = true;
			RemoveGroupButton.Anchor = AnchorStyles.Right;
			RemoveGroupButton.UseVisualStyleBackColor = true;
			RemoveGroupButton.Click += (s, e) =>
			{
				if (GroupsList.CheckedIndices.Count > 0)
				{
					int[] IndicesToRemove = new int[GroupsList.CheckedIndices.Count];
					GroupsList.CheckedIndices.CopyTo(IndicesToRemove, 0);
					Array.Sort(IndicesToRemove, (A, B) => B.CompareTo(A));

					foreach (int Index in IndicesToRemove)
					{
						GroupsList.Items.RemoveAt(Index);
						AddedBuiltinParamGroups.RemoveAt(Index);
					}
				}
			};

			TableLayoutPanel DialogLayout = new TableLayoutPanel();
			DialogLayout.Name = "DialogLayout";
			DialogLayout.ColumnCount = 2;
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.ColumnStyles.Add(new ColumnStyle());
			DialogLayout.RowCount = 6;
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.RowStyles.Add(new RowStyle());
			DialogLayout.AutoSize = true;
			DialogLayout.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			DialogLayout.Dock = DockStyle.Fill;
			DialogLayout.Location = new Point(10, 10);
			DialogLayout.TabIndex = 0;

			DialogLayout.Controls.Add(DescriptionLabel, 0,0);
			DialogLayout.Controls.Add(ParamNameLabel, 0, 1);
			DialogLayout.Controls.Add(ParamNameTextBox, 0, 2);
			DialogLayout.Controls.Add(GroupsLabel, 0, 3);
			DialogLayout.Controls.Add(GroupsList, 0, 4);
			DialogLayout.Controls.Add(AddGroupButton, 0, 5);
			DialogLayout.Controls.Add(RemoveGroupButton, 1, 5);

			DialogLayout.SetColumnSpan(ParamNameTextBox, 2);
			DialogLayout.SetColumnSpan(GroupsList, 2);
			DialogLayout.SetColumnSpan(DescriptionLabel, 2);

			Name = "MetadataExportFilter";
			Text = DatasmithRevitResources.Strings.SettingsDialog_DialogTitle;
			AutoScaleDimensions = new SizeF(6F, 13F);
			AutoScaleMode = AutoScaleMode.Font;
			AutoSize = true;
			AutoSizeMode = AutoSizeMode.GrowAndShrink;
			ClientSize = new Size(309, 114);
			FormBorderStyle = FormBorderStyle.FixedDialog;
			MaximizeBox = false;
			MinimizeBox = false;
			Padding = new Padding(10);
			SizeGripStyle = SizeGripStyle.Hide;
			StartPosition = FormStartPosition.CenterParent;

			Controls.Add(DialogLayout);

			if (MetadataSettings == null)
			{
				MetadataSettings = new FMetadataSettings();
			}
			else
			{
				StringBuilder SB = new StringBuilder();
				foreach (string ParamNameFilter in MetadataSettings.ParamNamesFilter)
				{
					if (ParamNameFilter.Length > 0)
					{
						SB.AppendLine(ParamNameFilter);
					}
				}
				ParamNameTextBox.Text = SB.ToString();

				foreach (int Group in MetadataSettings.ParamGroupsFilter)
				{
					string GroupLabel = Autodesk.Revit.DB.LabelUtils.GetLabelFor((Autodesk.Revit.DB.BuiltInParameterGroup)Group);
					AddedBuiltinParamGroups.Add(GroupLabel, Group);
				}

				ReloadAddedGroupsList();
			}
		}

		private void ReloadAddedGroupsList()
		{
			GroupsList.Items.Clear();

			foreach (var KV in AddedBuiltinParamGroups)
			{
				GroupsList.Items.Add(KV.Key);
			}
		}

		private void OnClosing(object InSender, FormClosingEventArgs InArgs)
		{
			MetadataSettings.ParamNamesFilter = ParamNameTextBox.Text.Split(new[] { Environment.NewLine }, StringSplitOptions.RemoveEmptyEntries);
			MetadataSettings.ParamGroupsFilter = AddedBuiltinParamGroups.Values;
			FMetadataManager.WriteSettings(Document, MetadataSettings);
		}
	}
}
