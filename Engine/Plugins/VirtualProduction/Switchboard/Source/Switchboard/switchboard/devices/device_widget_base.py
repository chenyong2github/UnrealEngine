# Copyright Epic Games, Inc. All Rights Reserved.
from switchboard.config import CONFIG
from switchboard.switchboard_widgets import FramelessQLineEdit
from switchboard.devices.device_base import Device, DeviceStatus
import switchboard.switchboard_widgets as sb_widgets

from PySide2 import QtWidgets, QtGui, QtCore

import os

ip_regex = QtCore.QRegExp("^\\s*((([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])\\.){3}([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5]))\\s*$")
ip_validator = QtGui.QRegExpValidator(ip_regex)

class DeviceWidgetItem(QtWidgets.QWidget):
    """
    Custom class to get QSS working correctly to achieve a look.
    This allows the QSS to set the style of the DeviceWidgetItem and change its color when recording
    """
    def __init__(self, parent=None):
        super().__init__(parent)

    def paintEvent(self, event):
        opt = QtWidgets.QStyleOption()
        opt.initFrom(self)
        painter = QtGui.QPainter(self)
        self.style().drawPrimitive(QtWidgets.QStyle.PE_Widget, opt, painter, self)


class DeviceWidget(QtWidgets.QWidget):
    signal_device_widget_connect = QtCore.Signal(object)
    signal_device_widget_disconnect = QtCore.Signal(object)
    signal_device_widget_open = QtCore.Signal(object)
    signal_device_widget_close = QtCore.Signal(object)
    signal_device_widget_sync = QtCore.Signal(object)
    signal_device_widget_build = QtCore.Signal(object)
    signal_device_widget_trigger_start_toggled = QtCore.Signal(object, bool)
    signal_device_widget_trigger_stop_toggled = QtCore.Signal(object, bool)

    signal_device_name_changed = QtCore.Signal(str)
    signal_ip_address_changed = QtCore.Signal(str)

    def __init__(self, name, device_hash, ip_address, icons, parent=None):
        super().__init__(parent)

        # Lookup device by a hash instead of name/ip_address
        self.device_hash = device_hash
        self.icons = icons

        self.status_icon = QtWidgets.QLabel()
        self.device_icon = QtWidgets.QLabel()

        self.name_validator = None
        self.name_line_edit = FramelessQLineEdit()
        self.name_line_edit.textChanged[str].connect(self.on_name_changed)
        self.name_line_edit.editingFinished.connect(self.on_name_edited)

        self.ip_address_line_edit = FramelessQLineEdit()
        self.ip_address_line_edit.setFont(QtGui.QFont("Roboto", 10))
        self.ip_address_line_edit.setValidator(ip_validator)
        self.ip_address_line_edit.editingFinished.connect(self.on_ip_address_edited)

        # Create a widget where the body of the item will go
        # This is made to allow the edit buttons to sit "outside" of the item
        self.widget = DeviceWidgetItem()
        self.edit_layout = QtWidgets.QHBoxLayout()
        self.edit_layout.setContentsMargins(0,0,0,0)
        self.setLayout(self.edit_layout)
        self.edit_layout.addWidget(self.widget)

        # Main layout where the contents of the item will love
        self.layout = QtWidgets.QHBoxLayout()
        self.layout.setContentsMargins(20, 2, 20, 2)
        self.layout.setSpacing(2)
        self.widget.setLayout(self.layout)

        self.layout.addWidget(self.status_icon)
        self.layout.addWidget(self.device_icon)
        self.layout.addWidget(self.name_line_edit)
        self.layout.addWidget(self.ip_address_line_edit)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
        self.layout.addItem(spacer)

        # Status Label
        self.status_icon.setGeometry(0, 0, 11, 1)
        pixmap = QtGui.QPixmap(f":/icons/images/status_blank_disabled.png")
        self.status_icon.setPixmap(pixmap)
        self.status_icon.resize(pixmap.width(), pixmap.height())

        # Device Label
        self.device_icon.setGeometry(0, 0, 40, 40)

        pixmap = self.icon_for_state("enabled").pixmap(QtCore.QSize(40, 40))
        self.device_icon.setPixmap(pixmap)
        self.device_icon.resize(pixmap.width(), pixmap.height())
        self.device_icon.setMinimumSize(QtCore.QSize(60, 40))
        self.device_icon.setAlignment(QtCore.Qt.AlignCenter)

        # Name Label
        self.name_line_edit.setText(name)
        self.name_line_edit.setFont(QtGui.QFont("Roboto", 14, QtGui.QFont.Bold))
        self.name_line_edit.setMaximumSize(QtCore.QSize(150, 40))
        # 20 + 11 + 60 + 150

        # IP Address Label
        self.ip_address_line_edit.setText(ip_address)
        self.ip_address_line_edit.setAlignment(QtCore.Qt.AlignCenter)
        self.ip_address_line_edit.setMaximumSize(QtCore.QSize(100, 40))

        # Store previous status for efficiency
        #self.previous_status = DeviceStatus.DISCONNECTED

        # Set style as disconnected
        for label in [self.name_line_edit, self.ip_address_line_edit]:
            label.setProperty("disconnected", True)
            label.setStyle(label.style())

        # Store the control buttons
        self.control_buttons = []

        self._add_control_buttons()

        self.help_tool_tip = QtWidgets.QToolTip()

    def can_sync(self):
        return False

    def can_build(self):
        return False

    def icon_for_state(self, state):
        if state in self.icons.keys():
            return self.icons[state]
        else:
            if "enabled" in self.icons.keys():
                return self.icons["enabled"]
            else:
                return QtGui.QIcon()

    def set_name_validator(self, name_validator):
        self.name_validator = name_validator

    def on_name_changed(self, text):
        if not self.name_validator:
            return
        if self.name_validator.validate(text, self.name_line_edit.cursorPosition()) == QtGui.QValidator.Invalid:
            rect = self.name_line_edit.parent().mapToGlobal(self.name_line_edit.geometry().topRight())
            self.help_tool_tip.showText(rect, "Names must be unique")

            sb_widgets.set_qt_property(self.name_line_edit, "input_error", True)
            self.name_line_edit.is_valid = False
        else:
            self.name_line_edit.is_valid = True
            sb_widgets.set_qt_property(self.name_line_edit, "input_error", False)
            self.help_tool_tip.hideText()

    def on_name_edited(self):
        new_value = self.name_line_edit.text()

        if self.name_line_edit.is_valid and self.name_line_edit.current_text != new_value:
            sb_widgets.set_qt_property(self.name_line_edit, "input_error", False)

            self.signal_device_name_changed.emit(new_value)

    def on_ip_address_edited(self):
        new_value = self.ip_address_line_edit.text()

        if self.ip_address_line_edit.is_valid and self.ip_address_line_edit.current_text != new_value:
            sb_widgets.set_qt_property(self.ip_address_line_edit, "input_error", False)

            self.signal_ip_address_changed.emit(new_value)

    def on_ip_address_changed(self, new_ip):
        self.ip_address_line_edit.setText(new_ip)

    def _add_control_buttons(self):
        pass

    def update_status(self, status, previous_status):
        # Status Icon
        if status >= DeviceStatus.READY:
            self.status_icon.setPixmap(QtGui.QPixmap(f":/icons/images/status_green.png"))
        elif status == DeviceStatus.DISCONNECTED:
            pixmap = QtGui.QPixmap(f":/icons/images/status_blank_disabled.png")
            self.status_icon.setPixmap(pixmap)
        elif status == DeviceStatus.OPEN:
            pixmap = QtGui.QPixmap(f":/icons/images/status_orange.png")
            self.status_icon.setPixmap(pixmap)
        else:
            self.status_icon.setPixmap(QtGui.QPixmap(f":/icons/images/status_blank.png"))

        # Device icon
        if status == DeviceStatus.DISCONNECTED:
            for label in [self.name_line_edit, self.ip_address_line_edit]:
                label.setProperty("disconnected", True)
                label.setStyle(label.style())

            pixmap = self.icon_for_state("disabled").pixmap(QtCore.QSize(40, 40))
            self.device_icon.setPixmap(pixmap)

            # Make the Name and IP editable
            self.name_line_edit.setReadOnly(False)
            self.ip_address_line_edit.setReadOnly(False)
        elif previous_status == DeviceStatus.DISCONNECTED and status > DeviceStatus.DISCONNECTED:
            for label in [self.name_line_edit, self.ip_address_line_edit]:
                label.setProperty("disconnected", False)
                label.setStyle(label.style())

            pixmap = self.icon_for_state("enabled").pixmap(QtCore.QSize(40, 40))
            self.device_icon.setPixmap(pixmap)

            # Make the Name and IP editable
            self.name_line_edit.setReadOnly(True)
            self.ip_address_line_edit.setReadOnly(True)

        # Handle coloring List Widget items if they are recording
        if status == DeviceStatus.RECORDING:
            sb_widgets.set_qt_property(self.widget, 'recording', True)
        else:
            sb_widgets.set_qt_property(self.widget, 'recording', False)

    def resizeEvent(self, event):
        super().resizeEvent(event)

        width = event.size().width()

        if width < sb_widgets.DEVICE_WIDGET_HIDE_IP_ADDRESS_WIDTH:
            self.ip_address_line_edit.hide()
        else:
            self.ip_address_line_edit.show()

    def add_control_button(self, *args, **kwargs):
        button = sb_widgets.ControlQPushButton.create(*args, **kwargs)
        self.control_buttons.append(button)
        self.layout.addWidget(button)

        return button


class AddDeviceDialog(QtWidgets.QDialog):
    def __init__(self, device_type, existing_devices, parent=None):
        super().__init__(parent=parent, f=QtCore.Qt.WindowCloseButtonHint)

        qss_file = os.path.join(CONFIG.SWITCHBOARD_DIR, "switchboard/ui/switchboard.qss")
        with open(qss_file, "r") as styling:
            self.setStyleSheet(styling.read())

        self.device_type = device_type
        self.setWindowTitle(f"Add {self.device_type} Device")

        self.name_field = QtWidgets.QLineEdit(self)

        self.ip_field = QtWidgets.QLineEdit(self)
        self.ip_field.setValidator(ip_validator)

        self.form_layout =  QtWidgets.QFormLayout()
        self.form_layout.addRow("Name", self.name_field)
        self.form_layout.addRow("IP Address", self.ip_field)

        layout = QtWidgets.QVBoxLayout()
        layout.insertLayout(0, self.form_layout)

        button_box = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        button_box.accepted.connect(lambda: self.accept())
        button_box.rejected.connect(lambda: self.reject())
        layout.addWidget(button_box)

        self.setLayout(layout)

    def add_name_validator(self, validator):
        if self.name_field:
            self.name_field.setValidator(validator)

    def devices_to_add(self):
        return [{"type": self.device_type, "name": self.name_field.text(), "ip_address": self.ip_field.text(), "kwargs": {}}]

    def devices_to_remove(self):
        return []
