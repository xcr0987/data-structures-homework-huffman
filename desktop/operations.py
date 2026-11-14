# 【修改】桌面操作只调用已有CLI，不重新实现编码、解码或文件比较。
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Lock


# 【修改】开发版从项目目录找C程序，交付版从GUI可执行文件旁边找。
def program_path():
    if getattr(sys, "frozen", False):
        # 【修改】macOS引擎随.app打包，位于PyInstaller包内目录。
        if sys.platform == "darwin":
            return Path(sys._MEIPASS) / "huffman"
        directory = Path(sys.executable).resolve().parent
    else:
        directory = Path(__file__).resolve().parent.parent
    return directory / ("huffman.exe" if os.name == "nt" else "huffman")


def same_file(left, right):
    left, right = Path(left), Path(right)
    if left.exists() and right.exists():
        return left.samefile(right)
    return left.resolve() == right.resolve()


# 【修改】复用CLI的标准文件名；编码仍保存全部验收文件。
def output_paths(action, mode, output):
    output = Path(output)
    if action != "encode":
        return [output]
    names = ["table.txt", "encoded.txt", "encoded.bin"]
    if mode == "single":
        names.append("packed.bin")
    elif mode == "evil":
        names.append("evil.deflate")
    return [output / name for name in names]


def error_message(details):
    messages = {
        "Ambiguous source encoding": "编码存在歧义，请手动选择UTF-8或GBK。",
        "Invalid source encoding": "来源编码不正确，或存在无法无损转换为GBK的字符。",
        "Invalid code table or bit stream": "码表或密文格式不正确。",
        "File I/O error": "C程序读写文件失败。",
        "Memory allocation failed": "C程序内存分配失败。",
        "Invalid argument": "命令参数不正确。",
    }
    for text, message in messages.items():
        if text in details:
            return message
    return "处理失败，请查看运行详情。"


def compare_message(report, code):
    if code == 0:
        count = re.fullmatch(r"MATCH: (\d+) bytes", report).group(1)
        return f"文件完全一致，共{int(count):,}字节。"
    position, detail = re.fullmatch(r"DIFFERENT at byte (\d+): (.+)", report).groups()
    if detail.endswith("ended"):
        side = "原文" if detail.startswith("left") else "解码结果"
        return f"文件长度不同：第{int(position):,}字节处，{side}先结束。"
    left, right = detail.split(" != ")
    return f"第{int(position):,}字节不同：原文0x{left}，解码结果0x{right}。"


# 【修改】一项任务拥有自己的子进程和临时目录，由窗口管理运行与退出。
class Operation:
    def __init__(self, action, inputs, output, mode="delivery", source_encoding="auto", program=None):
        self.action = action
        self.inputs = {role: Path(path) for role, path in inputs.items()}
        self.output = Path(output)
        self.mode = mode
        self.source_encoding = source_encoding
        self.program = Path(program) if program is not None else program_path()
        self.outputs = output_paths(action, mode, output)
        self.workspace = None
        self.keep_workspace = False
        self.process = None
        self.cancelled = False
        self.lock = Lock()
        # 【修改】执行前保护原文，覆盖确认由窗口一次性处理。
        for path in self.inputs.values():
            if not path.is_file():
                raise ValueError(f"输入文件不存在：{path}")
        for path in self.outputs:
            if not path.parent.is_dir():
                raise ValueError(f"输出目录不存在：{path.parent}")
            if path.is_dir():
                raise ValueError(f"输出位置是文件夹：{path}")
            if any(same_file(path, source) for source in self.inputs.values()):
                raise ValueError("输出文件不能覆盖输入文件。")

    def cancel(self):
        with self.lock:
            self.cancelled = True
            if self.process is not None and self.process.poll() is None:
                try:
                    self.process.kill()
                except ProcessLookupError:
                    pass

    def cleanup(self):
        if self.workspace is not None:
            self.workspace.cleanup()

    def run(self):
        result = {"ok": False, "code": -1, "message": "", "details": "", "files": []}
        self.workspace = TemporaryDirectory(prefix="huffman-desktop-")
        job = Path(self.workspace.name)
        names = {"source": "source.txt", "encoded": "encoded.dat", "table": "table.txt", "decoded": "decoded.txt"}
        try:
            for role, path in self.inputs.items():
                shutil.copyfile(path, job / names[role])
            if self.action == "encode":
                args = ["encode", "--input", "source.txt", "--from", self.source_encoding, "--mode", self.mode]
            elif self.action == "decode":
                args = ["decode", "--mode", self.mode, "--input", "encoded.dat", "--output", "decoded.txt"]
                if self.mode in ("delivery", "text"):
                    args += ["--table", "table.txt"]
            else:
                args = ["compare", "--left", "source.txt", "--right", "decoded.txt",
                        "--from", self.source_encoding, "--output", "compare.txt"]
            # 【修改】锁只保护“启动/取消”这一步；后台等待不会锁住窗口。
            with self.lock:
                if self.cancelled:
                    result["message"] = "已取消。"
                    return result
                try:
                    self.process = subprocess.Popen(
                        [str(self.program), *args], cwd=job, stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        text=True, encoding="utf-8", errors="replace",
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                    )
                except OSError as error:
                    result["message"] = "无法启动C程序。"
                    result["details"] = f"{self.program}\n{error}"
                    return result
            stdout, stderr = self.process.communicate()
            result["code"] = self.process.returncode
            result["details"] = stdout + stderr
            if self.cancelled:
                result["message"] = "已取消。"
                return result
            allowed = (0, 1) if self.action == "compare" else (0,)
            if result["code"] not in allowed:
                result["message"] = error_message(result["details"])
                return result

            if self.action == "encode":
                required = {"delivery": {"table.txt", "encoded.bin"},
                            "single": {"packed.bin"}, "evil": {"evil.deflate"}}[self.mode]
                sources = [job / "encode-output" / path.name for path in self.outputs]
                result["message"] = "编码完成。"
                result["source_size"] = (job / "source.txt").stat().st_size
                result["required_size"] = sum(path.stat().st_size for path in sources if path.name in required)
            else:
                required = set()
                sources = [job / ("decoded.txt" if self.action == "decode" else "compare.txt")]
                result["message"] = "解码完成。" if self.action == "decode" else compare_message(
                    sources[0].read_text(encoding="ascii").strip(), result["code"])

            # 【修改】Python负责中文保存路径；保存失败保留完整临时产物至窗口退出。
            try:
                for source, target in zip(sources, self.outputs):
                    if self.cancelled:
                        result["message"] = "已取消。"
                        return result
                    shutil.copyfile(source, target)
                    result["files"].append({"path": str(target), "size": source.stat().st_size,
                                            "required": source.name in required})
            except OSError as error:
                self.keep_workspace = True
                result["message"] = "保存失败，临时结果保留至窗口退出。"
                result["details"] += f"\n{error}\n临时结果：{job}"
                result["retained_dir"] = str(job)
                return result
            result["ok"] = True
            return result
        except OSError as error:
            result["message"] = "文件读写失败。"
            result["details"] += f"\n{error}"
            return result
        finally:
            with self.lock:
                self.process = None
            if not self.keep_workspace:
                self.cleanup()
