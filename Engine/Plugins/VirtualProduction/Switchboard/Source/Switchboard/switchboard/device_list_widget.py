# Copyright Epic Games, Inc. All Rights Reserved.

from switchboard.devices.device_base import DeviceStatus, PluginHeaderWidgets
import switchboard.switchboard_widgets as sb_widgets

from PySide2 import QtCore, QtGui, QtWidgets


class DeviceListWidget(QtWidgets.QListWidget):
    signal_register_device_widget = QtCore.Signal(object)
    signal_remove_device = QtCore.Signal(object)
    signal_connect_all_plugin_devices_toggled = QtCore.Signal(str, bool)
    signal_open_all_plugin_devices_toggled = QtCore.Signal(str, bool)

    def __init__(self, name, parent=None):
        super().__init__(parent)

        self.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self.setFocusPolicy(QtCore.Qt.NoFocus)
        #self.setSelectionMode(QtWidgets.QAbstractItemView.NoSelection)
        self.setSelectionMode(QtWidgets.QAbstractItemView.MultiSelection)

        self._device_widgets = {}
        self._header_by_category_name = {}
        self.setMinimumSize(self.minimumSize().width(), 5)

    # Override all mouse events so that selection can be used for qss styling
    def mousePressEvent(self, e):
        pass

    def mouseReleaseEvent(self, e):
        pass

    def mouseDoubleClickEvent(self, e):
        pass

    def mouseMoveEvent(self, e):
        pass

    def contextMenuEvent(self, event):
        device_context_menu = QtWidgets.QMenu(self)
        device_context_menu.addAction("Remove Device", lambda: self.ask_to_confirm_device_removal(event.pos()))
        device_context_menu.exec_(event.globalPos())
        event.accept()

    def ask_to_confirm_device_removal(self, pos):
        item = self.itemAt(pos)
        item_widget = self.itemWidget(item)

        confirmation = QtWidgets.QMessageBox.question(self, "Device Removal Confirmation", "Are you sure you want to delete this device?")
        if confirmation == QtWidgets.QMessageBox.Yes:
            self.signal_remove_device.emit(item_widget.device_hash)

    def devices_in_category(self, category):
        header_item = self._header_by_category_name[category]
        header_row = self.row(header_item)
        device_widgets = []
        for row in range(header_row+1, self.count()):
            widget = self.itemWidget(self.item(row))
            if type(widget) == DeviceWidgetHeader:
                break
            device_widgets.append(widget)
        return device_widgets

    def on_device_removed(self, device_hash, device_type, *args):
        self._device_widgets.pop(device_hash)

        for i in range(self.count()):
            item = self.item(i)
            widget = self.itemWidget(item)
            if widget.device_hash == device_hash:
                self.takeItem(self.row(item))
                break

        category = device_type
        remaining_devices_in_category = self.devices_in_category(category)
        if len(remaining_devices_in_category) == 0:
            header_item = self._header_by_category_name[category]
            self.takeItem(self.row(header_item))
            self._header_by_category_name.pop(category)

    def on_connect_all_toggled(self, plugin_name, state):
        self.signal_connect_all_plugin_devices_toggled.emit(plugin_name, state)

    def on_open_all_toggled(self, plugin_name, state):
        self.signal_connect_all_plugin_devices_toggled.emit(plugin_name, state)

    def add_header(self, label_name, show_open_button, show_connect_button, show_changelist):
        device_item = QtWidgets.QListWidgetItem()
        device_item.setSizeHint(QtCore.QSize(self.parent().width(), sb_widgets.DEVICE_HEADER_LIST_WIDGET_HEIGHT))
        self.addItem(device_item)
        header_widget = DeviceWidgetHeader(label_name, show_open_button, show_connect_button, show_changelist)
        header_widget.signal_connect_all_toggled.connect(self.signal_connect_all_plugin_devices_toggled)
        header_widget.signal_open_all_toggled.connect(self.signal_open_all_plugin_devices_toggled)
        self.setItemWidget(device_item, header_widget)

        return device_item

    def add_device_widget(self, device):
        category = device.category_name
        if category not in self._header_by_category_name.keys():
            header_widget_config = device.plugin_header_widget_config()
            show_open_button = PluginHeaderWidgets.OPEN_BUTTON in header_widget_config
            show_connect_button = PluginHeaderWidgets.CONNECT_BUTTON in header_widget_config
            show_changelist = PluginHeaderWidgets.CHANGELIST_LABEL in header_widget_config
            self._header_by_category_name[category] = self.add_header(category, show_open_button, show_connect_button, show_changelist)

        header_item = self._header_by_category_name[category]
        header_row = self.row(header_item)

        device_item = QtWidgets.QListWidgetItem()
        device_item.setSizeHint(QtCore.QSize(self.parent().width(), sb_widgets.DEVICE_LIST_WIDGET_HEIGHT))
        self.insertItem(header_row+1, device_item)
        self.setItemWidget(device_item, device.widget)

        # Keep a dict for quick lookup
        self._device_widgets[device.device_hash] = device.widget

        self.signal_register_device_widget.emit(device.widget)
        # Force an update to put it in the correct position
        self.update_status(device, device.status)

        return device.widget

    def clear_widgets(self):
        self.clear()
        self._device_widgets = {}
        self._header_by_category_name = {}

    def widget_item_by_device(self, device):
        for i in range(self.count()):
            item = self.item(i)
            widget = self.itemWidget(item)

            if not widget:
                continue

            if widget.device_hash == device.device_hash:
                return item
        return None

    def device_widget_by_hash(self, device_hash):
        if device_hash not in self._device_widgets:
            return None

        return self._device_widgets[device_hash]

    def device_widgets(self):
        return self._device_widgets.values()

    def update_status(self, device, previous_status):
        widget_item = self.widget_item_by_device(device)
        device_widget = self.itemWidget(widget_item)

        # Tell the widget to update
        device_widget.update_status(device.status, previous_status)

    def update_category_status(self, category_name, devices):
        any_connected = False
        any_opened = False
        for device in devices:
            any_connected |= (device.status != DeviceStatus.DISCONNECTED)
            any_opened |= (device.status > DeviceStatus.CLOSED)

        header_item = self._header_by_category_name[category_name]
        header_widget = self.itemWidget(header_item)
        header_widget.update_connection_state(any_connected)
        header_widget.update_opened_state(any_connected, any_opened)


class DeviceWidgetHeader(QtWidgets.QWidget):

    signal_connect_all_toggled = QtCore.Signal(str, bool) # params: plugin_name, toggle_state
    signal_open_all_toggled = QtCore.Signal(str, bool) # params: plugin_name, toggle_state

    def __init__(self, name, show_open_button, show_connect_button, show_changelist, parent=None):
        super().__init__(parent)

        self.name = name
        self.ip_address_label = None
        self.device_hash = 0 # When list widget is looking for devices it checks the device_has

        self.layout = QtWidgets.QHBoxLayout()
        self.layout.setContentsMargins(0, 6, 17, 6)
        self.setLayout(self.layout)

        def __label(label_text):
            label = QtWidgets.QLabel()
            label.setText(label_text)
            label.setAlignment(QtCore.Qt.AlignCenter)
            label.setStyleSheet("font-weight: bold")
            return label

        self.name_label = __label(self.name + " Devices")
        self.ip_address_label = __label('IP Address')
        self.changelist_label = __label('Changelist') if show_changelist else None
        self.layout.addWidget(self.name_label)
        self.layout.addWidget(self.ip_address_label)

        spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
        self.layout.addItem(spacer)

        if self.changelist_label:
            self.layout.addWidget(self.changelist_label)
            spacer = QtWidgets.QSpacerItem(0, 20, QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Minimum)
            self.layout.addItem(spacer)

        self.open_button = None
        if show_open_button:
            self.open_button = sb_widgets.ControlQPushButton.create(':/icons/images/icon_open.png',
                                                        icon_hover=':/icons/images/icon_open_hover.png',
                                                        icon_disabled=':/icons/images/icon_open_disabled.png',
                                                        icon_on=':/icons/images/icon_close.png',
                                                        icon_hover_on=':/icons/images/icon_close_hover.png',
                                                        icon_disabled_on=':/icons/images/icon_close_disabled.png',
                                                        tool_tip=f'Start all connected {self.name} devices')
            self.layout.setAlignment(self.open_button, QtCore.Qt.AlignVCenter)
            self.open_button.clicked.connect(self.on_open_button_clicked)
            self.open_button.setEnabled(False)
            self.layout.addWidget(self.open_button)

        self.connect_button = None
        if show_connect_button:
            self.connect_button = sb_widgets.ControlQPushButton.create(':/icons/images/icon_connect.png',
                                                            icon_hover=':/icons/images/icon_connect_hover.png',
                                                            icon_disabled=':/icons/images/icon_connect_disabled.png',
                                                            icon_on=':/icons/images/icon_connected.png',
                                                            icon_hover_on=':/icons/images/icon_connected_hover.png',
                                                            icon_disabled_on=':/icons/images/icon_connected_disabled.png',
                                                            tool_tip=f'Connect all {self.name} devices')
            self.layout.setAlignment(self.connect_button, QtCore.Qt.AlignVCenter)
            self.connect_button.clicked.connect(self.on_connect_button_clicked)
            self.layout.addWidget(self.connect_button)

        self.name_label.setMinimumSize(QtCore.QSize(235, 0))
        self.ip_address_label.setMinimumSize(QtCore.QSize(100, 0))

    def showEvent(self, event):
        super().showEvent(event)

        self.find_ip_address_label()

    def find_ip_address_label(self):
        labels = self.findChildren(QtWidgets.QLabel)
        for label in labels:
            if label.text() == 'IP Address':
                self.ip_address_label = label
                return

    def resizeEvent(self, event):
        super().resizeEvent(event)

        if not self.ip_address_label:
            return

        width = event.size().width()

        if width < sb_widgets.DEVICE_WIDGET_HIDE_IP_ADDRESS_WIDTH:
            self.ip_address_label.hide()
        else:
            self.ip_address_label.show()

    def paintEvent(self, event):
        opt = QtWidgets.QStyleOption()
        opt.initFrom(self)
        painter = QtGui.QPainter(self)
        self.style().drawPrimitive(QtWidgets.QStyle.PE_Widget, opt, painter, self)

    def on_open_button_clicked(self, checked):
        button_state = checked
        plugin_name = self.name
        self.signal_open_all_toggled.emit(plugin_name, button_state)

    def on_connect_button_clicked(self, checked):
        button_state = checked
        plugin_name = self.name
        self.signal_connect_all_toggled.emit(plugin_name, button_state)

    def update_connection_state(self, any_devices_connected):
        if self.connect_button is None:
            return
        self.connect_button.setChecked(any_devices_connected)
        if any_devices_connected:
            self.connect_button.setToolTip(f"Disconnect all connected {self.name} devices")
        else:
            self.connect_button.setToolTip(f"Connect all {self.name} devices")

    def update_opened_state(self, any_devices_connected, any_devices_opened):
        if self.open_button is None:
            return
        self.open_button.setEnabled(any_devices_connected)
        self.open_button.setChecked(any_devices_opened)
        if any_devices_opened:
            self.open_button.setToolTip(f"Stop all running {self.name} devices")
        else:
            self.open_button.setToolTip(f"Start all connected {self.name} devices")
