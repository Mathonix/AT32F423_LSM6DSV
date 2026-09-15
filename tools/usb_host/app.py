"""Tk host application for the AT32F423 AHRS.

The device streams VOFA+ JustFloat by default and accepts the existing
AA 55 framed command protocol.  This GUI deliberately keeps the host-side
protocol backwards compatible while exposing configuration controls for
firmware versions that implement the optional mode/settings commands.
"""
from __future__ import annotations

import argparse
import csv
import math
import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk
from tkinter.scrolledtext import ScrolledText

import serial
from serial.tools import list_ports

from protocol import (
    CMD_ENTER_SETTINGS,
    CMD_EXIT_SETTINGS,
    CMD_PING,
    CMD_QUERY_STATUS,
    CMD_RECALIBRATE_GYRO,
    CMD_SET_FUSION_MODE,
    CMD_SET_CAN_NODE_ID, CMD_START_GYRO_CAL_60S, CMD_START_ACC_6FACE_CAL,
    CMD_SET_STREAM_MODE,
    CMD_SYSTEM_RESET,
    CMD_ZERO_YAW,
    MODE_6AXIS,
    MODE_9AXIS,
    MODE_9AXIS_RELATIVE,
    decode_binary,
    JustFloatDecoder,
    pack_command,
    unpack_binary,
)


class AttitudeCanvas(tk.Canvas):
    """Small dependency-free wire-frame attitude indicator."""

    def __init__(self, master, **kwargs):
        super().__init__(master, background="#10151d", highlightthickness=0, **kwargs)
        self.yaw = self.pitch = self.roll = 0.0
        self.bind("<Configure>", lambda _event: self.redraw())

    def set_pose(self, yaw: float, pitch: float, roll: float) -> None:
        self.yaw, self.pitch, self.roll = yaw, pitch, roll
        self.redraw()

    @staticmethod
    def rotate(v, yaw, pitch, roll):
        cy, sy = math.cos(yaw), math.sin(yaw)
        cp, sp = math.cos(pitch), math.sin(pitch)
        cr, sr = math.cos(roll), math.sin(roll)
        x, y, z = v
        # Z(yaw) * Y(pitch) * X(roll)
        x, y = cr * x - sr * y, sr * x + cr * y
        x, z = cp * x + sp * z, -sp * x + cp * z
        x, y = cy * x - sy * y, sy * x + cy * y
        return x, y, z

    def project(self, v, cx, cy, scale):
        x, y, z = v
        return cx + scale * (x - 0.45 * y), cy - scale * (z + 0.30 * y)

    def redraw(self):
        self.delete("all")
        w, h = max(self.winfo_width(), 220), max(self.winfo_height(), 180)
        cx, cy, scale = w / 2, h / 2, min(w, h) * 0.29
        self.create_text(10, 10, anchor="nw", fill="#aab7c4",
                         text=f"Y {self.yaw:7.2f}°   P {self.pitch:7.2f}°   R {self.roll:7.2f}°")
        vertices = [(-1, -0.65, -0.5), (1, -0.65, -0.5),
                    (1, 0.65, -0.5), (-1, 0.65, -0.5),
                    (-1, -0.65, 0.5), (1, -0.65, 0.5),
                    (1, 0.65, 0.5), (-1, 0.65, 0.5)]
        angles = tuple(math.radians(x) for x in (self.yaw, self.pitch, self.roll))
        p = [self.project(self.rotate(v, *angles), cx, cy, scale) for v in vertices]
        edges = ((0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6),
                 (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7))
        for a, b in edges:
            self.create_line(*p[a], *p[b], fill="#3388dd", width=2)
        # Body axes: X red, Y green, Z blue.
        origin = self.rotate((0, 0, 0), *angles)
        ox, oy = self.project(origin, cx, cy, scale)
        for vec, color, label in (((1.45, 0, 0), "#ff5555", "X"),
                                   ((0, 1.45, 0), "#55dd66", "Y"),
                                   ((0, 0, 1.45), "#5588ff", "Z")):
            ex, ey = self.project(self.rotate(vec, *angles), cx, cy, scale)
            self.create_line(ox, oy, ex, ey, fill=color, width=3, arrow=tk.LAST)
            self.create_text(ex + 8, ey, text=label, fill=color, anchor="w")


class PlotCanvas(tk.Canvas):
    """VOFA-like scrolling three-channel plot implemented with Tk only."""

    COLORS = ("#ff4d6d", "#4dd2ff", "#66dd66")

    def __init__(self, master, **kwargs):
        super().__init__(master, background="#0d1117", highlightthickness=0, **kwargs)
        self.samples = []
        self.max_samples = 240
        self.bind("<Configure>", lambda _event: self.redraw())

    def add(self, yaw, pitch, roll):
        self.samples.append((float(yaw), float(pitch), float(roll)))
        del self.samples[:-self.max_samples]
        self.redraw()

    def redraw(self):
        self.delete("all")
        w, h = max(self.winfo_width(), 300), max(self.winfo_height(), 150)
        pad = 28
        for i in range(5):
            y = pad + i * (h - 2 * pad) / 4
            self.create_line(pad, y, w - 8, y, fill="#202a35")
        self.create_line(pad, pad, pad, h - pad, fill="#607080")
        if len(self.samples) < 2:
            self.create_text(pad + 8, pad + 8, text="等待姿态数据…", anchor="nw", fill="#8795a5")
            return
        flat = [x for row in self.samples for x in row]
        lo, hi = min(flat), max(flat)
        if hi - lo < 1.0:
            mid = (hi + lo) / 2
            lo, hi = mid - 0.5, mid + 0.5
        def xy(index, value):
            x = pad + index * (w - pad - 8) / (len(self.samples) - 1)
            y = h - pad - (value - lo) * (h - 2 * pad) / (hi - lo)
            return x, y
        for ch, color in enumerate(self.COLORS):
            points = []
            for i, row in enumerate(self.samples):
                points.extend(xy(i, row[ch]))
            self.create_line(*points, fill=color, width=2, smooth=False)
        self.create_text(w - 8, 8, anchor="ne", fill=self.COLORS[0], text="YAW")
        self.create_text(w - 8, 24, anchor="ne", fill=self.COLORS[1], text="PITCH")
        self.create_text(w - 8, 40, anchor="ne", fill=self.COLORS[2], text="ROLL")
        self.create_text(4, 4, anchor="nw", fill="#8795a5", text=f"{lo:.1f} … {hi:.1f}°")


class App:
    def __init__(self, root, initial_port=None):
        self.root = root
        root.title("AT32 AHRS 上位机")
        root.geometry("1180x800")
        self.ser = None
        self.stop = threading.Event()
        self.events = queue.Queue(maxsize=20000)
        self.serial_lock = threading.Lock()
        self.reader_thread = None
        self.seq = 0
        self.csv_file = None
        self.csv_writer = None
        self.settings_mode = False
        self.pending_restart = False
        self.pending_exit_after_mode = False
        self.last_pose = (0.0, 0.0, 0.0)
        self.frame_count = 0
        self.port_items = {}
        self.mode_controls = []

        self.port = tk.StringVar(value=initial_port or "")
        self.baud = tk.StringVar(value="2000000")
        self.parse_mode = tk.StringVar(value="auto")
        self.parse_mode_value = "auto"
        self.just_decoder = JustFloatDecoder()
        self.selected_mode = tk.IntVar(value=MODE_9AXIS)
        self.immediate_restart = tk.BooleanVar(value=True)
        self.mode_text = tk.StringVar(value="当前模式：未知")
        self.settings_text = tk.StringVar(value="运行模式：未进入设置")
        self.vars = {k: tk.StringVar(value="--") for k in
                     ("yaw", "pitch", "roll", "temperature", "rate", "frames", "crc")}

        top = ttk.Frame(root, padding=8); top.pack(fill="x")
        ttk.Label(top, text="\u4e32\u53e3").pack(side="left")
        self.port_box = ttk.Combobox(top, textvariable=self.port, width=30, state="readonly")
        self.port_box.pack(side="left", padx=4)
        # ???????????????????????????
        self.port_box.configure(postcommand=self.refresh)
        ttk.Button(top, text="\u5237\u65b0\u4e32\u53e3", command=self.refresh).pack(side="left")
        ttk.Label(top, text="波特率").pack(side="left", padx=(16, 2))
        ttk.Entry(top, textvariable=self.baud, width=10).pack(side="left")
        self.connect_btn = ttk.Button(top, text="连接", command=self.toggle)
        self.connect_btn.pack(side="left", padx=8)
        ttk.Label(top, text="解析").pack(side="left")
        self.parse_box = ttk.Combobox(top, textvariable=self.parse_mode,
                                       values=["auto", "justfloat", "binary"], width=10,
                                       state="readonly")
        self.parse_box.pack(side="left")
        self.parse_box.bind("<<ComboboxSelected>>", self._parse_mode_changed)
        ttk.Label(top, textvariable=self.settings_text).pack(side="right")

        values = ttk.LabelFrame(root, text="实时姿态", padding=8); values.pack(fill="x", padx=8, pady=4)
        for i, key in enumerate(("yaw", "pitch", "roll", "temperature")):
            ttk.Label(values, text=key.upper()).grid(row=0, column=i, padx=22)
            ttk.Label(values, textvariable=self.vars[key], font=("Consolas", 18), width=14).grid(row=1, column=i, padx=22)
        status = ttk.Frame(root); status.pack(fill="x", padx=8)
        for label, key in (("速率", "rate"), ("帧数", "frames"), ("CRC", "crc")):
            ttk.Label(status, text=label + ":").pack(side="left")
            ttk.Label(status, textvariable=self.vars[key]).pack(side="left", padx=(3, 15))
        ttk.Label(status, textvariable=self.mode_text).pack(side="left")

        main = ttk.Panedwindow(root, orient="horizontal"); main.pack(fill="both", expand=True, padx=8, pady=4)
        left = ttk.Frame(main); right = ttk.Frame(main); main.add(left, weight=1); main.add(right, weight=1)
        self.attitude = AttitudeCanvas(left, height=360); self.attitude.pack(fill="both", expand=True)
        self.plot = PlotCanvas(right, height=360); self.plot.pack(fill="both", expand=True)

        settings = ttk.LabelFrame(root, text="设置模式（修改后需重启生效）", padding=8); settings.pack(fill="x", padx=8, pady=4)
        ttk.Button(settings, text="进入设置模式", command=self.enter_settings).pack(side="left", padx=3)
        ttk.Button(settings, text="退出设置模式", command=self.exit_settings).pack(side="left", padx=3)
        self.mode_controls = [
            ttk.Radiobutton(settings, text="\u516d\u8f74", variable=self.selected_mode, value=MODE_6AXIS),
            ttk.Radiobutton(settings, text="\u4e5d\u8f74", variable=self.selected_mode, value=MODE_9AXIS),
            ttk.Radiobutton(settings, text="\u4e5d\u8f74\u76f8\u5bf9\u89d2", variable=self.selected_mode, value=MODE_9AXIS_RELATIVE),
        ]
        for index, control in enumerate(self.mode_controls):
            control.pack(side="left", padx=(14, 3) if index == 0 else 3)
        self.mode_hint = ttk.Label(settings, text="\uff08\u8bf7\u5148\u8fdb\u5165\u8bbe\u7f6e\u6a21\u5f0f\uff09", foreground="#777777")
        self.mode_hint.pack(side="left", padx=(4, 8))
        ttk.Checkbutton(settings, text="修改后立即重启", variable=self.immediate_restart).pack(side="left", padx=(14, 3))
        ttk.Button(settings, text="应用模式设置", command=self.apply_settings).pack(side="left", padx=8)
        ttk.Button(settings, text="PING", command=lambda: self.send(CMD_PING)).pack(side="left", padx=3)
        ttk.Button(settings, text="Yaw归零", command=lambda: self.send(CMD_ZERO_YAW)).pack(side="left", padx=3)
        ttk.Button(settings, text="校准", command=lambda: self.send(CMD_RECALIBRATE_GYRO)).pack(side="left", padx=3)
        ttk.Button(settings, text="查询状态", command=lambda: self.send(CMD_QUERY_STATUS)).pack(side="left", padx=3)
        ttk.Button(settings, text="保存CSV", command=self.toggle_csv).pack(side="left", padx=3)

        self.log = ScrolledText(root, height=8, state="disabled"); self.log.pack(fill="both", expand=False, padx=8, pady=4)
        self._update_settings_controls()
        self.refresh(); self.root.after(50, self.poll)
        root.protocol("WM_DELETE_WINDOW", self.close)

    def _parse_mode_changed(self, _event=None):
        self.parse_mode_value = self.parse_mode.get()
        self.just_decoder = JustFloatDecoder()

    def refresh(self):
        """?????????????????????????"""
        current_device = self._port_device(self.port.get())
        try:
            found = list(list_ports.comports())
        except Exception as exc:
            self.log_line(f"???????{exc}")
            return
        self.port_items = {}
        display_values = []
        for info in found:
            description = (info.description or info.manufacturer or "????").strip()
            display = f"{info.device}  |  {description}"
            self.port_items[display] = info.device
            display_values.append(display)
        self.port_box["values"] = display_values
        selected = next((label for label, device in self.port_items.items()
                         if device == current_device), None)
        if selected:
            self.port.set(selected)
        else:
            preferred = next((label for label, info in
                              zip(display_values, found)
                              if info.vid == 0x2E3C and info.pid == 0xF401), None)
            self.port.set(preferred or (display_values[0] if display_values else ""))

    def _port_device(self, value):
        value = (value or "").strip()
        return self.port_items.get(value, value.split("  |  ", 1)[0].strip())

    def _update_settings_controls(self):
        state = "normal" if self.settings_mode else "disabled"
        for control in self.mode_controls:
            control.configure(state=state)
        if hasattr(self, "mode_hint"):
            self.mode_hint.configure(text="\uff08\u8bbe\u7f6e\u6a21\u5f0f\u5df2\u5f00\u542f\uff0c\u53ef\u4fee\u6539\uff09" if self.settings_mode
                                     else "\uff08\u8bf7\u5148\u8fdb\u5165\u8bbe\u7f6e\u6a21\u5f0f\uff09")

    def toggle(self):
        if self.ser: self.disconnect()
        else:
            try:
                device = self._port_device(self.port.get())
                if not device:
                    raise ValueError("??????")
                self.ser = serial.Serial(device, int(self.baud.get()), timeout=0.05, write_timeout=0.5)
                self.ser.reset_input_buffer()
                self.just_decoder = JustFloatDecoder()
                self.stop.clear()
                self.reader_thread = threading.Thread(target=self.reader, daemon=True, name="ahrs-serial-reader")
                self.reader_thread.start()
                self.connect_btn.config(text="断开"); self.log_line("已连接 " + self.port.get())
            except Exception as exc: messagebox.showerror("连接失败", str(exc))

    def disconnect(self):
        self.stop.set(); ser, self.ser = self.ser, None
        self.settings_mode = False
        self._update_settings_controls()
        if ser:
            try:
                with self.serial_lock:
                    ser.close()
            except Exception: pass
        self.connect_btn.config(text="连接"); self.log_line("已断开")

    def reader(self):
        binary, just = bytearray(), bytearray()
        last = time.monotonic(); count = 0
        ser = self.ser
        while not self.stop.is_set() and ser is self.ser and ser.is_open:
            try:
                chunk = ser.read(4096)
            except (serial.SerialException, OSError) as exc:
                if not self.stop.is_set(): self._event(("log", "???????" + str(exc)))
                break
            if not chunk: continue
            selected = self.parse_mode_value
            if selected in ("auto", "binary"):
                binary.extend(chunk)
                for msg, _seq, payload in unpack_binary(binary):
                    self._event(("binary", decode_binary(msg, payload)))
            if selected in ("auto", "justfloat"):
                just.extend(chunk)
                for vals in self.just_decoder.feed(just):
                    self._event(("pose", vals)); count += 1
            now = time.monotonic()
            if now - last >= 1.0:
                self._event(("rate", count)); count = 0; last = now
        self._event(("reader_stopped", None))

    def _event(self, item):
        try: self.events.put_nowait(item)
        except queue.Full:
            try: self.events.get_nowait()
            except queue.Empty: pass
            try: self.events.put_nowait(item)
            except queue.Full: pass

    def send(self, cmd, payload=b""):
        if not self.ser:
            messagebox.showwarning("???", "???? USB/UART")
            return False
        try:
            self.seq = (self.seq + 1) & 0xFF
            frame = pack_command(cmd, self.seq, payload)
            with self.serial_lock:
                ser = self.ser
                if ser is None or not ser.is_open: raise serial.SerialException("?????")
                ser.write(frame); ser.flush()
            self.log_line(f"?? CMD 0x{cmd:02X} payload={payload.hex() or '-'}")
            return True
        except Exception as exc:
            self.log_line("?????" + str(exc)); return False

    def enter_settings(self):
        if self.send(CMD_ENTER_SETTINGS):
            self.settings_text.set("?? MCU ACK")

    def exit_settings(self):
        if self.send(CMD_EXIT_SETTINGS):
            self.settings_text.set("???????? ACK")

    def apply_settings(self):
        if not self.settings_mode:
            messagebox.showwarning("??????", "????????????")
            return
        mode = self.selected_mode.get()
        if mode not in (MODE_6AXIS, MODE_9AXIS, MODE_9AXIS_RELATIVE):
            messagebox.showwarning("????", "??????????")
            return
        immediate = self.immediate_restart.get()
        if not self.send(CMD_SET_FUSION_MODE, bytes((mode, 1 if immediate else 0))): return
        self.pending_restart = not immediate
        self.pending_exit_after_mode = not immediate
        self.settings_text.set("?? MCU ACK")
        self.log_line("?????? MCU ACK" + ("??????" if immediate else ""))

    def set_can_id(self):
        if not self.settings_mode:
            messagebox.showwarning("??????", "????????")
            return
        try:
            node_id = int(self.can_id.get(), 0)
            if not 0 <= node_id <= 0x7FF: raise ValueError
        except ValueError:
            messagebox.showwarning("????", "CAN ?? ID ??? 0~0x7FF ???")
            return
        self.send(CMD_SET_CAN_NODE_ID, node_id.to_bytes(2, "little"))

    def start_gyro_cal_60s(self):
        if not self.settings_mode:
            messagebox.showwarning("??????", "????????")
            return
        if messagebox.askyesno("????", "????????60??????????????"):
            self.send(CMD_START_GYRO_CAL_60S)
            self.settings_text.set("?? 60 ??? MCU ACK")

    def start_acc_6face_cal(self):
        if not self.settings_mode:
            messagebox.showwarning("??????", "????????")
            return
        if messagebox.askyesno("????", "??????????????????"):
            self.send(CMD_START_ACC_6FACE_CAL)
            self.settings_text.set("?????? MCU ACK")

    def toggle_csv(self):
        if self.csv_file:
            self.csv_file.close(); self.csv_file = self.csv_writer = None; self.log_line("CSV 已关闭"); return
        name = time.strftime("ahrs_%Y%m%d_%H%M%S.csv")
        self.csv_file = open(name, "w", newline="", encoding="utf-8")
        self.csv_writer = csv.writer(self.csv_file); self.csv_writer.writerow(["time", "yaw", "pitch", "roll", "temperature"])
        self.log_line("保存 " + name)

    def poll(self):
        # The MCU can publish at 1 kHz. Drawing every queued sample makes Tk
        # spend all of its time repainting and Windows reports "Not responding".
        # Drain the queue quickly, retain all CSV samples, but render only the
        # newest pose once per UI tick.
        latest_pose = None
        csv_rows = []
        processed = 0
        try:
            while processed < 5000:
                kind, data = self.events.get_nowait()
                processed += 1
                if kind == "pose":
                    vals = list(data)
                    if len(vals) >= 3:
                        latest_pose = vals
                        self.frame_count += 1
                        if self.csv_writer:
                            temp = vals[3] if len(vals) > 3 else None
                            csv_rows.append([time.time(), vals[0], vals[1], vals[2], temp])
                elif kind == "rate":
                    self.vars["rate"].set(f"{data} Hz")
                elif kind == "binary":
                    if data.get("kind") == "attitude":
                        latest_pose = [data["yaw"], data["pitch"], data["roll"]]
                        self.frame_count += 1
                    else:
                        self.handle_binary(data)
                elif kind == "log":
                    self.log_line(data)
        except queue.Empty:
            pass

        if latest_pose is not None:
            yaw, pitch, roll = latest_pose[:3]
            temp = latest_pose[3] if len(latest_pose) > 3 else None
            self.last_pose = (yaw, pitch, roll)
            self.attitude.set_pose(yaw, pitch, roll)
            self.plot.add(yaw, pitch, roll)
            for key, val in (("yaw", yaw), ("pitch", pitch), ("roll", roll)):
                self.vars[key].set(f"{val: .5f}°")
            if temp is not None:
                self.vars["temperature"].set(f"{temp: .2f} °C")
            self.vars["frames"].set(str(self.frame_count))

        if csv_rows and self.csv_writer:
            self.csv_writer.writerows(csv_rows)
            self.csv_file.flush()

        self.root.after(20, self.poll)

    def handle_binary(self, data):
        kind = data.get("kind")
        if kind == "attitude": self.events.put(("pose", (data["yaw"], data["pitch"], data["roll"])))
        elif kind == "ack":
            cmd = data["cmd_id"]; status = data["status"]; detail = data["detail"]
            self.log_line(f"ACK cmd=0x{cmd:02X} status={status} detail={detail}")
            if cmd == CMD_ENTER_SETTINGS:
                if status == 0:
                    self.settings_mode = True
                    self._update_settings_controls()
                    self.settings_text.set("???????")
                else:
                    self.settings_text.set(f"?????status={status}, detail={detail}")
            elif cmd == CMD_EXIT_SETTINGS:
                if status == 0:
                    self.settings_mode = False
                    self._update_settings_controls()
                    self.settings_text.set("???????")
                else:
                    self.settings_text.set(f"?????status={status}, detail={detail}")
            elif cmd == CMD_SET_FUSION_MODE:
                if status == 0:
                    self.pending_restart = not self.immediate_restart.get()
                    self.settings_text.set("????????????????????" if self.pending_restart else "?????????????")
                    if self.pending_exit_after_mode and self.settings_mode:
                        self.pending_exit_after_mode = False
                        self.exit_settings()
                else:
                    self.pending_restart = False
                    self.pending_exit_after_mode = False
                    self.settings_text.set(f"?????status={status}, detail={detail}")
            elif cmd == CMD_SET_CAN_NODE_ID and status == 0:
                self.log_line(f"CAN ID ???? 0x{detail:03X}")
            elif cmd in (CMD_START_GYRO_CAL_60S, CMD_START_ACC_6FACE_CAL):
                self.settings_text.set("?????" if status == 0 else f"?????status={status}, detail={detail}")
        elif kind == "system":
            self.vars["temperature"].set(f"{data['temperature_c']:.2f} °C")
            self.mode_text.set(f"设备模式：{data.get('stream_mode', '?')}")
            self.log_line(f"系统 {data['fusion_hz']}Hz/{data['out_hz']}Hz mode={data['stream_mode']} CAN={data['can_ok']}")
        else: self.log_line(f"收到消息 0x{data.get('msg_id', 0):02X}")

    def log_line(self, text):
        self.log.config(state="normal"); self.log.insert("end", time.strftime("%H:%M:%S ") + text + "\n"); self.log.see("end"); self.log.config(state="disabled")

    def close(self):
        self.disconnect()
        if self.csv_file: self.csv_file.close()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(); parser.add_argument("--port"); args = parser.parse_args()
    root = tk.Tk(); App(root, args.port); root.mainloop()


if __name__ == "__main__": main()
