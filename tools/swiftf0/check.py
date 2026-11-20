"""Compares the C++ SwiftF0 (check.cpp) with the ONNX original on a WAV file.

    python check.py model.onnx voice.wav ./swiftf0check
"""
import subprocess
import sys
import wave

import numpy as np
import onnxruntime as ort
from scipy.signal import resample_poly

with wave.open(sys.argv[2]) as w:
    sr, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
    data = np.frombuffer(w.readframes(w.getnframes()), {2: np.int16, 4: np.int32}[width]).astype(np.float64)
x = data.reshape(-1, ch).mean(axis=1) / (2 ** (8 * width - 1))
x16 = resample_poly(x, 16000, sr).astype(np.float32)
x16.tofile("/tmp/claude-501/check16k.f32")
s = ort.InferenceSession(sys.argv[1])
pitch, conf = s.run(None, {"audio": x16[None], "fmin": np.array(50, np.float32), "fmax": np.array(1000, np.float32)})
out = subprocess.run([sys.argv[3], "/tmp/claude-501/check16k.f32"], capture_output=True, text=True).stdout.split("\n")
cpp = {int(l.split()[0]): (float(l.split()[1]), float(l.split()[2])) for l in out if l.strip()}
n = len(pitch[0])
frames = range(12, n - 12)  # the batch model zero-pads its edges; streaming starts from silence
dp, dc, voiced_ok, both = [], [], 0, 0
for k in frames:
    p, c = cpp[k]
    dc.append(abs(c - conf[0][k]))
    if conf[0][k] >= 0.5 and c >= 0.5:
        both += 1
        dp.append(abs(1200 * np.log2(p / pitch[0][k])))
    voiced_ok += (c >= 0.5) == (conf[0][k] >= 0.5)
print(f"frames {len(frames)}  voicing agreement {100 * voiced_ok / len(frames):.2f}%  "
      f"confidence error max {max(dc):.5f}  pitch error max {max(dp) if dp else 0:.3f} cents (mean {np.mean(dp) if dp else 0:.4f})  voiced {both}")
