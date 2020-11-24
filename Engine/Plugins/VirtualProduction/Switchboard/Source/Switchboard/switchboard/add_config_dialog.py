# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard.config import CONFIG

from PySide2 import QtCore, QtWidgets

import os


class AddConfigDialog(QtWidgets.QDialog):
    def __init__(self, stylesheet, uproject_search_path, previous_engine_dir, parent):
        super().__init__(parent=parent, f=QtCore.Qt.WindowCloseButtonHint)

        self.config_name = None
        self.uproject = None
        self.engine_dir = None

        self.setStyleSheet(stylesheet)

        self.setWindowTitle("Add new Switchboard Configuration")

        self.form_layout =  QtWidgets.QFormLayout()
        self.name_line_edit = QtWidgets.QLineEdit()
        self.name_line_edit.textChanged.connect(self.on_name_changed)
        self.form_layout.addRow("Name", self.name_line_edit)

        self.uproject_line_edit = QtWidgets.QLineEdit()
        self.uproject_line_edit.textChanged.connect(self.on_uproject_changed)
        self.uproject_browse_button = QtWidgets.QPushButton("Browse")
        self.uproject_browse_button.clicked.connect(lambda: self.on_browse_uproject_path(uproject_search_path))

        uproject_layout = QtWidgets.QHBoxLayout()
        uproject_layout.addWidget(self.uproject_line_edit)
        uproject_layout.addWidget(self.uproject_browse_button)
        self.form_layout.addRow("uProject", uproject_layout)

        self.engine_dir_line_edit = QtWidgets.QLineEdit()
        if os.path.exists(previous_engine_dir): # re-use previous engine dir
            self.engine_dir_line_edit.setText(previous_engine_dir)
            self.engine_dir = previous_engine_dir
        self.engine_dir_line_edit.textChanged.connect(self.on_engine_dir_changed)
        self.engine_dir_browse_button = QtWidgets.QPushButton("Browse")
        self.engine_dir_browse_button.clicked.connect(self.on_browse_engine_dir)

        engine_dir_layout = QtWidgets.QHBoxLayout()
        engine_dir_layout.addWidget(self.engine_dir_line_edit)
        engine_dir_layout.addWidget(self.engine_dir_browse_button)
        self.form_layout.addRow("Engine Dir", engine_dir_layout)

        layout = QtWidgets.QVBoxLayout()
        layout.insertLayout(0, self.form_layout)

        self.p4_group = QtWidgets.QGroupBox("Perforce")
        self.p4_group.setCheckable(True)
        self.p4_group.setChecked(bool(CONFIG.P4_ENABLED.get_value()))

        self.p4_project_path_line_edit = QtWidgets.QLineEdit(CONFIG.P4_PROJECT_PATH.get_value())
        self.p4_engine_path_line_edit = QtWidgets.QLineEdit(CONFIG.P4_ENGINE_PATH.get_value())
        self.p4_workspace_line_edit = QtWidgets.QLineEdit(CONFIG.SOURCE_CONTROL_WORKSPACE.get_value())
        p4_layout = QtWidgets.QFormLayout()
        p4_layout.addRow("P4 Project Path", self.p4_project_path_line_edit)
        p4_layout.addRow("P4 Engine Path", self.p4_engine_path_line_edit)
        p4_layout.addRow("Workspace Name", self.p4_workspace_line_edit)
        self.p4_group.setLayout(p4_layout)
        layout.addWidget(self.p4_group)

        self.button_box = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(False)
        self.button_box.accepted.connect(self.accept)
        self.button_box.rejected.connect(self.reject)
        layout.addWidget(self.button_box)

        self.setLayout(layout)

        self.setMinimumWidth(450)

    def p4_settings(self):
        settings = {}
        settings['p4_enabled'] = self.p4_group.isChecked()
        settings['p4_workspace_name'] = self.p4_workspace_line_edit.text() if self.p4_group.isChecked() else None
        settings['p4_project_path'] = self.p4_project_path_line_edit.text() if self.p4_group.isChecked() else None
        settings['p4_engine_path'] = self.p4_engine_path_line_edit.text() if self.p4_group.isChecked() else None
        return settings

    def on_name_changed(self, text):
        self.config_name = text
        self.update_button_box()

    def on_uproject_changed(self, text):
        self.uproject = os.path.normpath(text)
        self.update_button_box()

    def on_browse_uproject_path(self, uproject_search_path):
        self.uproject, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select uProject file", self.engine_dir, "uProject (*.uproject)")
        self.uproject = os.path.normpath(self.uproject)
        self.uproject_line_edit.setText(self.uproject)

    def on_engine_dir_changed(self, text):
        self.engine_dir = os.path.normpath(text)
        self.update_button_box()

    def on_browse_engine_dir(self):
        self.engine_dir = QtWidgets.QFileDialog.getExistingDirectory(self, "Select UE4 engine directory")
        self.engine_dir = os.path.normpath(self.engine_dir)
        self.engine_dir_line_edit.setText(self.engine_dir)

    def update_button_box(self):
        self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(False)
        if self.config_name and self.uproject and self.engine_dir:
            if os.path.exists(self.uproject) and os.path.exists(self.engine_dir):
                self.button_box.button(QtWidgets.QDialogButtonBox.Ok).setEnabled(True)
