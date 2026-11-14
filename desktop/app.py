# 【修改】独立Qt桌面窗口，不使用网页和HTTP；算法仍由现有C程序执行。
from pathlib import Path
import sys

from PySide6.QtCore import QThread, QUrl, Qt, Signal, Slot
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QApplication, QComboBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout,
    QHeaderView, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit,
    QProgressBar, QPushButton, QTableWidget, QTableWidgetItem, QTabWidget,
    QVBoxLayout, QWidget, QAbstractItemView,
)

from operations import Operation


# 【修改】系统文件对话框只负责选择；所有覆盖操作在运行前统一确认。
class PathPicker(QWidget):
    def __init__(self, kind="open", default_name="", parent=None):
        super().__init__(parent)
        self.kind = kind
        self.default_name = default_name
        self.edit = QLineEdit()
        self.button = QPushButton("浏览…")
        self.button.clicked.connect(self.choose)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.edit, 1)
        layout.addWidget(self.button)

    def choose(self):
        start = self.edit.text().strip() or str(Path.home() / self.default_name)
        if self.kind == "directory":
            path = QFileDialog.getExistingDirectory(self, "选择输出目录", start)
        elif self.kind == "save":
            path, _ = QFileDialog.getSaveFileName(
                self, "保存文件", start, "所有文件 (*)",
                options=QFileDialog.Option.DontConfirmOverwrite,
            )
        else:
            path, _ = QFileDialog.getOpenFileName(self, "选择文件", start, "所有文件 (*)")
        if path:
            self.edit.setText(path)

    def path(self):
        text = self.edit.text().strip()
        if not text:
            raise ValueError("请选择本次操作需要的文件和输出位置。")
        return Path(text).expanduser().absolute()


# 【修改】只使用一个后台任务执行器，通过Qt信号更新界面。
class Worker(QThread):
    result = Signal(dict)

    def __init__(self, operation, parent=None):
        super().__init__(parent)
        self.operation = operation

    def run(self):
        self.result.emit(self.operation.run())


class MainWindow(QMainWindow):
    def __init__(self, program=None):
        super().__init__()
        self.setWindowTitle("哈夫曼编码工具")
        self.resize(900, 700)
        self.setMinimumSize(740, 520)
        self.program = program
        self.worker = None
        self.operation = None
        self.running_action = None
        self.retained = []
        self.pages = {}
        self.tabs = QTabWidget()
        self.setCentralWidget(self.tabs)
        self.setStyleSheet("""
            QTabWidget::pane { border: 1px solid #d9dee7; }
            QTabBar::tab { padding: 10px 24px; }
            QPushButton { padding: 5px 12px; }
            QPushButton#runButton { background: #2463d9; color: white; border: 0; border-radius: 4px; padding: 8px 24px; }
            QPushButton#runButton:disabled { background: #9aabc8; }
            QLineEdit, QComboBox { padding: 5px; }
            QGroupBox { font-weight: 600; }
        """)
        for action, title in [("encode", "编码"), ("decode", "解码"), ("compare", "比较")]:
            self.add_page(action, title)
        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setMaximumWidth(180)
        self.progress.hide()
        self.statusBar().addPermanentWidget(self.progress)

    # 【修改】用现成Qt表单和表格构建三个独立页面，不加入多余说明。
    def add_page(self, action, title):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(20, 24, 20, 20)
        layout.setSpacing(16)
        form = QFormLayout()
        form.setSpacing(14)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        pickers = {}
        for role, label in ({"encode": [("source", "原文文件")],
                             "decode": [("encoded", "密文文件（encoded.bin）"), ("table", "编码对照表（table.txt）")],
                             "compare": [("source", "原文文件"), ("decoded", "解码结果（GBK）")]}[action]):
            pickers[role] = PathPicker()
            pickers[role].edit.setAccessibleName(label)
            form.addRow(label, pickers[role])
        encoding = None
        if action != "decode":
            encoding = QComboBox()
            encoding.setAccessibleName("原文编码")
            for label, value in [("自动检测", "auto"), ("UTF-8", "utf8"), ("GBK", "gbk")]:
                encoding.addItem(label, value)
            form.addRow("原文编码", encoding)
        mode = None
        if action != "compare":
            mode = QComboBox()
            mode.setAccessibleName("压缩模式" if action == "encode" else "解码方式")
            choices = [("交付型 · delivery", "delivery"), ("单文件 · single", "single"), ("DEFLATE · evil", "evil")]
            if action == "decode":
                choices.insert(1, ("文本密文 · text", "text"))
            for label, value in choices:
                mode.addItem(label, value)
            form.addRow("压缩模式" if action == "encode" else "解码方式", mode)
        output = PathPicker("directory" if action == "encode" else "save",
                            "decoded.txt" if action == "decode" else "compare.txt" if action == "compare" else "")
        output.edit.setAccessibleName("输出目录" if action == "encode" else "保存位置")
        form.addRow("输出目录" if action == "encode" else "保存位置", output)
        layout.addLayout(form)
        button = QPushButton("开始" + title)
        button.setObjectName("runButton")
        button.clicked.connect(lambda checked=False, a=action: self.start_task(a))
        button_row = QHBoxLayout()
        button_row.addWidget(button)
        button_row.addStretch()
        layout.addLayout(button_row)

        result = QGroupBox("结果")
        result_layout = QVBoxLayout(result)
        status = QLabel()
        status.setWordWrap(True)
        stats = QLabel()
        stats.setWordWrap(True)
        result_layout.addWidget(status)
        result_layout.addWidget(stats)
        table = QTableWidget(0, 3)
        table.setHorizontalHeaderLabels(["文件", "大小", "用途"])
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        table.verticalHeader().hide()
        table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        table.setMinimumHeight(140)
        result_layout.addWidget(table, 1)
        open_file = QPushButton("打开文件")
        open_folder = QPushButton("打开所在文件夹")
        actions = QHBoxLayout()
        actions.addWidget(open_file)
        actions.addWidget(open_folder)
        actions.addStretch()
        result_layout.addLayout(actions)
        details_button = QPushButton("运行详情")
        details_button.setCheckable(True)
        log = QPlainTextEdit()
        log.setReadOnly(True)
        log.setMaximumHeight(140)
        log.hide()
        details_button.toggled.connect(log.setVisible)
        result_layout.addWidget(details_button, 0, Qt.AlignmentFlag.AlignLeft)
        result_layout.addWidget(log)
        result.hide()
        layout.addWidget(result, 1)
        layout.addStretch()
        page = dict(widget=widget, form=form, inputs=pickers, output=output, encoding=encoding, mode=mode,
                    button=button, result=result, status=status, stats=stats, table=table, log=log,
                    details_button=details_button, open_file=open_file, open_folder=open_folder, retained_dir=None)
        self.pages[action] = page
        open_file.clicked.connect(lambda checked=False, a=action: self.open_result(a, False))
        open_folder.clicked.connect(lambda checked=False, a=action: self.open_result(a, True))
        table.itemSelectionChanged.connect(lambda a=action: self.update_open_buttons(a))
        if action == "decode":
            mode.currentIndexChanged.connect(self.update_decode_fields)
        self.tabs.addTab(widget, title)

    def update_decode_fields(self):
        page = self.pages["decode"]
        mode = page["mode"].currentData()
        needs_table = mode in ("delivery", "text")
        page["form"].setRowVisible(page["inputs"]["table"], needs_table)
        names = {"delivery": "encoded.bin", "text": "encoded.txt", "single": "packed.bin", "evil": "evil.deflate"}
        label = f"密文文件（{names[mode]}）"
        page["form"].labelForField(page["inputs"]["encoded"]).setText(label)
        page["inputs"]["encoded"].edit.setAccessibleName(label)
        page["inputs"]["encoded"].edit.clear()

    # 【修改】启动前统一检查输入输出和覆盖，GUI不处理编译。
    def start_task(self, action):
        page = self.pages[action]
        mode = page["mode"].currentData() if page["mode"] is not None else "delivery"
        try:
            roles = list(page["inputs"])
            if action == "decode" and mode not in ("delivery", "text"):
                roles.remove("table")
            inputs = {role: page["inputs"][role].path() for role in roles}
            encoding = page["encoding"].currentData() if page["encoding"] is not None else "auto"
            operation = Operation(action, inputs, page["output"].path(), mode, encoding, self.program)
        except (ValueError, OSError) as error:
            QMessageBox.warning(self, "无法执行", str(error))
            return
        existing = [str(path) for path in operation.outputs if path.exists()]
        if existing:
            answer = QMessageBox.question(self, "覆盖文件", "以下文件已存在，是否覆盖？\n\n" + "\n".join(existing),
                                          QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                          QMessageBox.StandardButton.No)
            if answer != QMessageBox.StandardButton.Yes:
                return
        self.operation = operation
        self.running_action = action
        self.worker = Worker(operation, self)
        self.worker.result.connect(self.show_result)
        self.worker.finished.connect(self.finish_task)
        page["result"].hide()
        self.tabs.setEnabled(False)
        self.progress.show()
        self.statusBar().showMessage("正在处理…")
        self.worker.start()

    # 【修改】结果只解释CLI报告；不重新计算算法或文件差异。
    @Slot(dict)
    def show_result(self, result):
        page = self.pages[self.running_action]
        page["result"].show()
        page["status"].setText(result["message"])
        color = "#b42318" if not result["ok"] else "#926100" if result["code"] == 1 else "#18713c"
        page["status"].setStyleSheet(f"color: {color}; font-weight: 600;")
        page["stats"].setVisible("source_size" in result)
        if "source_size" in result:
            size, required = result["source_size"], result["required_size"]
            ratio = f"{required / size * 100:.2f}%" if size else "—"
            page["stats"].setText(f"原文件：{size:,}字节    解码所需合计：{required:,}字节    大小比例：{ratio}")
        table = page["table"]
        table.setRowCount(len(result["files"]))
        for row, file in enumerate(result["files"]):
            item = QTableWidgetItem(Path(file["path"]).name)
            item.setToolTip(file["path"])
            item.setData(Qt.ItemDataRole.UserRole, file["path"])
            table.setItem(row, 0, item)
            table.setItem(row, 1, QTableWidgetItem(f'{file["size"]:,}字节'))
            usage = "解码所需" if file["required"] else "验收文件" if self.running_action == "encode" else "处理结果"
            table.setItem(row, 2, QTableWidgetItem(usage))
        page["retained_dir"] = result.get("retained_dir")
        if self.operation.keep_workspace:
            self.retained.append(self.operation)
        page["log"].setPlainText(result["details"])
        page["details_button"].setChecked(not result["ok"])
        page["log"].setVisible(not result["ok"])
        # 【修改】结果由用户自行选择，不自动选中行。
        self.update_open_buttons(self.running_action)

    @Slot()
    def finish_task(self):
        self.worker.deleteLater()
        self.worker = None
        self.operation = None
        self.tabs.setEnabled(True)
        self.progress.hide()
        self.statusBar().clearMessage()

    def update_open_buttons(self, action):
        page = self.pages[action]
        selected = page["table"].currentRow() >= 0
        page["open_file"].setEnabled(selected)
        page["open_folder"].setEnabled(selected or page["retained_dir"] is not None)

    def open_result(self, action, folder):
        page = self.pages[action]
        row = page["table"].currentRow()
        if row >= 0:
            path = Path(page["table"].item(row, 0).data(Qt.ItemDataRole.UserRole))
            if folder:
                path = path.parent
        elif folder and page["retained_dir"] is not None:
            path = Path(page["retained_dir"])
        else:
            return
        if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(path))):
            QMessageBox.warning(self, "无法打开", str(path))

    # 【修改】关闭时先结束当前子进程和后台线程，再清理保留的临时结果。
    def closeEvent(self, event):
        if self.worker is not None and self.worker.isRunning():
            answer = QMessageBox.question(self, "退出", "正在处理，是否结束任务并退出？",
                                          QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                          QMessageBox.StandardButton.No)
            if answer != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
            self.operation.cancel()
            self.worker.wait()
            self.operation.cleanup()
        for operation in self.retained:
            operation.cleanup()
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.styleHints().setColorScheme(Qt.ColorScheme.Light)
    app.setPalette(app.style().standardPalette())
    font = app.font()
    font.setPointSize(11)
    app.setFont(font)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
