#!/usr/bin/env python3
"""
lprnet.py — LPRNet model for OCR (20-class output: 19 active + blank at 19).

Adapted from sirius-ai/LPRNet_Pytorch for industrial packaging OCR on RV1106.
Architecture simplified to match Rockchip's validated model — no small-inception
container; straight CNN backbone → BiLSTM → Conv1×1 projection → LogSoftmax.

Classes (kCharMap order):
    '0' '1' '2' '3' '4' '5' '6' '7' '8' '9' '/' ':' 'L' 'O' 'T' 'E' 'V' 'N' 'C'
    blank = 19  (index 19, after all 19 active chars)

Usage:
    from lprnet import LPRNet
    model = LPRNet(num_classes=20)
    log_probs = model(x)  # [B, 3, 24, 94] → [18, B, 20]
"""

import torch
import torch.nn as nn
import torch.nn.functional as F

# Character map matching kCharMap in ocr_engine.hpp
# 19 active characters at indices 0..18, blank at index 19.
# IMPORTANT: blank MUST be after ALL active chars to avoid conflict with 'C'.
CHARS = "0123456789/:LOTEVNC"
BLANK_IDX = 19


class LPRNet(nn.Module):
    """Lightweight CNN + BiLSTM for OCR on 24×94 BGR images.

    Architecture (Rockchip-validated):
        Conv2d(3→64, 3×3) + BN + ReLU + MaxPool(3×3, s=1)
        Conv2d(64→128, 3×3) + BN + ReLU + MaxPool(3×3, s=1)
        Conv2d(128→256, 3×3) + BN + ReLU
        Conv2d(256→256, 3×3) + BN + ReLU + MaxPool(3×3, s=1)
        AdaptiveAvgPool2d((1, 18))  → 18 timesteps × 256 channels
        BiLSTM(256→128, 2 layers)
        Conv2d(256→num_classes, 1×1)
        LogSoftmax(dim=-1)

    Output: [18, batch, num_classes]  — log-probabilities, NOT raw logits.
    num_classes = 20  (19 active chars + blank at index 19).
    """

    def __init__(self, num_classes: int = 20):
        super().__init__()

        # ---- CNN Backbone --------------------------------------------------
        self.backbone = nn.Sequential(
            # Stage 1: 3 → 64
            nn.Conv2d(3, 64, kernel_size=3, stride=1, padding=1, bias=False),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(kernel_size=3, stride=1, padding=1),
            # Stage 2: 64 → 128
            nn.Conv2d(64, 128, kernel_size=3, stride=1, padding=1, bias=False),
            nn.BatchNorm2d(128),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(kernel_size=3, stride=1, padding=1),
            # Stage 3: 128 → 256
            nn.Conv2d(128, 256, kernel_size=3, stride=1, padding=1, bias=False),
            nn.BatchNorm2d(256),
            nn.ReLU(inplace=True),
            # Stage 4: 256 → 256
            nn.Conv2d(256, 256, kernel_size=3, stride=1, padding=1, bias=False),
            nn.BatchNorm2d(256),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(kernel_size=3, stride=1, padding=1),
        )

        # ---- Width reduction to 18 timesteps -------------------------------
        # After backbone: [B, 256, 24, 94]
        # AdaptiveAvgPool reduces height→1 and width→18, giving [B, 256, 1, 18]
        # Each of the 18 columns becomes a BiLSTM timestep.
        self.width_reduce = nn.AdaptiveAvgPool2d((1, 18))

        # ---- BiLSTM --------------------------------------------------------
        # Input: [18, B, 256]  (seq_len, batch, features)
        # Output: [18, B, 2 * 128] = [18, B, 256]  (bidirectional)
        self.lstm = nn.LSTM(
            input_size=256,
            hidden_size=128,
            num_layers=2,
            batch_first=False,
            bidirectional=True,
            dropout=0.5,
        )

        # ---- Projection to num_classes -------------------------------------
        # Conv2d with 1×1 kernel: [B, 256, 1, 18] → [B, num_classes, 1, 18]
        self.projection = nn.Conv2d(256, num_classes, kernel_size=1, stride=1, bias=False)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Forward pass.

        Args:
            x: [batch, 3, 24, 94] — BGR float32 tensor, normalized [0, 1] range.

        Returns:
            log_probs: [18, batch, num_classes] — log-probabilities (20 classes).
        """
        # ---- CNN backbone --------------------------------------------------
        x = self.backbone(x)                     # [B, 256, 24, 94]

        # ---- Width reduction -----------------------------------------------
        x = self.width_reduce(x)                 # [B, 256, 1, 18]

        # ---- Squeeze spatial dims for BiLSTM -------------------------------
        x = x.squeeze(2)                         # [B, 256, 18]
        x = x.permute(2, 0, 1)                   # [18, B, 256]  (T, N, C)

        # ---- BiLSTM --------------------------------------------------------
        # dropout=0.5 is applied between layers; the 2-layer LSTM handles
        # the first layer's output being passed through dropout before layer 2.
        x, _ = self.lstm(x)                      # [18, B, 256]

        # ---- Reshape back for 1×1 convolution -----------------------------
        x = x.permute(1, 2, 0)                   # [B, 256, 18]
        x = x.unsqueeze(2)                       # [B, 256, 1, 18]

        # ---- Project to class scores ---------------------------------------
        x = self.projection(x)                   # [B, num_classes, 1, 18]
        x = x.squeeze(2)                         # [B, num_classes, 18]
        x = x.permute(2, 0, 1)                   # [18, B, num_classes]

        # ---- LogSoftmax (CTC expects log-probabilities) --------------------
        log_probs = F.log_softmax(x, dim=-1)     # [18, B, num_classes]

        return log_probs


# ---------------------------------------------------------------------------
# Quick shape verification
# ---------------------------------------------------------------------------
def _verify_shapes():
    """Run a quick forward pass to verify output shape is [18, 1, num_classes]."""
    model = LPRNet(num_classes=20)
    model.eval()
    dummy = torch.randn(1, 3, 24, 94)
    with torch.no_grad():
        out = model(dummy)
    expected = (18, 1, 20)
    assert out.shape == expected, f"Expected {expected}, got {out.shape}"
    print(f"[lprnet] Shape OK: {out.shape}")
    print(f"[lprnet] Values in range [{out.min():.3f}, {out.max():.3f}]")
    print(f"[lprnet] Sum per timestep (should be ~1.0): {out.exp().sum(dim=-1)[:3, 0]}")


if __name__ == "__main__":
    _verify_shapes()
