#!/usr/bin/env python3
"""
train.py — Train LPRNet on synthetic dataset + export ONNX.

Usage:
    python train.py --data-dir ./dataset --epochs 100 --batch-size 128 \\
                    --lr 0.001 --output ./lprnet.onnx

The dataset directory should have:
    dataset/
        train/
            images/  (24×94 BGR .png files)
            labels/  (.txt files, one label per line)
        test/
            images/
            labels/

Labels use the CHAR_SET: 0123456789/:LOTEVNC
"""

import argparse
import os
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader
from torch.utils.tensorboard import SummaryWriter

from lprnet import LPRNet, CHARS, BLANK_IDX


# ---------------------------------------------------------------------------
# Character encoding helpers
# ---------------------------------------------------------------------------

def encode_label(label: str) -> List[int]:
    """Map a label string to integer indices using CHARS."""
    indices = []
    for ch in label:
        idx = CHARS.find(ch)
        if idx == -1:
            raise ValueError(f"Character {ch!r} not in CHARS ({CHARS!r})")
        indices.append(idx)
    return indices


def decode_ctc(log_probs: torch.Tensor) -> List[str]:
    """Greedy CTC decode: argmax per timestep → collapse repeats → remove blanks.

    Args:
        log_probs: [W, batch, num_classes] log-probabilities.

    Returns:
        List of decoded strings, one per batch item.
    """
    # Get argmax per timestep: [W, batch]
    preds = log_probs.argmax(dim=-1)  # [W, B]
    preds = preds.cpu().numpy()       # [W, B]

    batch_size = preds.shape[1]
    decoded = []
    for b in range(batch_size):
        prev = -1
        chars = []
        for t in range(preds.shape[0]):
            c = int(preds[t, b])
            if c != BLANK_IDX and c != prev:
                if c < len(CHARS):
                    chars.append(CHARS[c])
            prev = c
        decoded.append("".join(chars))
    return decoded


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------

class OCRDataset(Dataset):
    """Reads (image, label) pairs from a dataset split directory.

    Directory layout:
        {split}/
            images/  (24×94 BGR .png files)
            labels/  (.txt files, same stem as images)
    """

    def __init__(self, split_dir: str):
        self.img_dir = Path(split_dir) / "images"
        self.lbl_dir = Path(split_dir) / "labels"

        # List all .png files sorted for deterministic ordering
        self.image_paths = sorted(self.img_dir.glob("*.png"))
        if not self.image_paths:
            raise FileNotFoundError(
                f"No .png images found in {self.img_dir.resolve()}"
            )

        # Verify matching labels exist
        missing = 0
        for img_p in self.image_paths:
            lbl_p = self.lbl_dir / img_p.with_suffix(".txt").name
            if not lbl_p.is_file():
                missing += 1
        if missing > 0:
            raise FileNotFoundError(
                f"{missing} label(s) missing in {self.lbl_dir.resolve()}"
            )

    def __len__(self) -> int:
        return len(self.image_paths)

    def __getitem__(self, idx: int) -> Tuple[torch.Tensor, torch.Tensor, int]:
        """Return (image_tensor, label_tensor, label_length).

        Returns:
            image: [3, 24, 94] float32 tensor, normalized to [0, 1].
            label: 1-D int64 tensor of character indices.
            length: int, number of characters in the label.
        """
        img_path = self.image_paths[idx]
        lbl_path = self.lbl_dir / img_path.with_suffix(".txt").name

        # Load image (BGR, uint8)
        import cv2
        img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        if img is None:
            raise IOError(f"Failed to read image: {img_path}")
        # Ensure 24×94 BGR
        img = cv2.resize(img, (94, 24), interpolation=cv2.INTER_LINEAR)

        # Normalize to [0, 1] float32
        img = img.astype(np.float32) / 255.0

        # HWC → CHW
        img = torch.from_numpy(img).permute(2, 0, 1).float()

        # Load label
        label_str = lbl_path.read_text(encoding="ascii").strip()
        label_indices = encode_label(label_str)
        label_tensor = torch.tensor(label_indices, dtype=torch.long)

        return img, label_tensor, len(label_indices)


def collate_fn(batch):
    """Collate function for DataLoader with variable-length labels.

    Returns:
        images: [batch, 3, 24, 94] float32
        targets: 1-D int64 tensor of all concatenated label indices
        input_lengths: [batch] int64 tensor, all 18 (fixed timesteps)
        target_lengths: [batch] int64 tensor, actual label lengths
        target_list: list of label tensors (for accuracy eval)
    """
    images = torch.stack([item[0] for item in batch], dim=0)

    targets_list = [item[1] for item in batch]
    target_lengths = torch.tensor([item[2] for item in batch], dtype=torch.long)

    # Concatenate all labels into 1-D tensor
    targets = torch.cat(targets_list, dim=0)

    # Fixed input lengths: 18 timesteps per sample
    input_lengths = torch.full((len(batch),), fill_value=18, dtype=torch.long)

    return images, targets, input_lengths, target_lengths, targets_list


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

def character_accuracy(
    log_probs: torch.Tensor,
    target_list: List[torch.Tensor],
) -> float:
    """Compute character-wise accuracy (non-blank, non-collapsed).

    Greedy decodes log_probs, then compares each decoded character
    against the ground truth label. Returns (correct / total).
    """
    decoded = decode_ctc(log_probs)

    total_chars = 0
    correct_chars = 0
    for pred_str, gt_tensor in zip(decoded, target_list):
        gt_str = "".join(CHARS[i] for i in gt_tensor.tolist())
        total_chars += len(gt_str)
        for p, g in zip(pred_str, gt_str):
            if p == g:
                correct_chars += 1
    if total_chars == 0:
        return 1.0
    return correct_chars / total_chars


# ---------------------------------------------------------------------------
# ONNX export
# ---------------------------------------------------------------------------

def export_onnx(
    model: nn.Module,
    output_path: str,
    opset_version: int = 17,
) -> None:
    """Export trained model to ONNX opset 17 with dynamic batch.

    The exported graph includes LogSoftmax — output is log-probabilities.
    CTC decoding (greedy argmax) is done in C++, NOT in the ONNX graph.
    """
    model.eval()
    dummy_input = torch.randn(1, 3, 24, 94)

    torch.onnx.export(
        model,
        dummy_input,
        output_path,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={
            "input": {0: "batch"},
            "output": {0: "seq_len"},
        },
        opset_version=opset_version,
    )
    print(f"[onnx] Exported to {output_path} (opset {opset_version})")


# ---------------------------------------------------------------------------
# Training
# ---------------------------------------------------------------------------

def train_epoch(
    model: nn.Module,
    dataloader: DataLoader,
    criterion: nn.CTCLoss,
    optimizer: torch.optim.Optimizer,
    device: torch.device,
) -> float:
    """Run one training epoch. Returns average loss."""
    model.train()
    total_loss = 0.0
    num_batches = 0

    for images, targets, input_lengths, target_lengths, _ in dataloader:
        images = images.to(device)
        targets = targets.to(device)
        input_lengths = input_lengths.to(device)
        target_lengths = target_lengths.to(device)

        optimizer.zero_grad()

        log_probs = model(images)  # [18, B, num_classes]

        loss = criterion(log_probs, targets, input_lengths, target_lengths)
        loss.backward()

        # Gradient clipping to prevent BiLSTM exploding gradients
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=10.0)

        optimizer.step()

        total_loss += loss.item()
        num_batches += 1

    return total_loss / num_batches


@torch.no_grad()
def validate(
    model: nn.Module,
    dataloader: DataLoader,
    criterion: nn.CTCLoss,
    device: torch.device,
) -> Tuple[float, float, float]:
    """Run validation. Returns (avg_loss, char_accuracy, exact_match_rate)."""
    model.eval()
    total_loss = 0.0
    num_batches = 0
    all_log_probs = []
    all_target_lists = []

    for images, targets, input_lengths, target_lengths, target_list in dataloader:
        images = images.to(device)
        targets = targets.to(device)
        input_lengths = input_lengths.to(device)
        target_lengths = target_lengths.to(device)

        log_probs = model(images)
        loss = criterion(log_probs, targets, input_lengths, target_lengths)

        total_loss += loss.item()
        num_batches += 1

        all_log_probs.append(log_probs.cpu())
        all_target_lists.extend(target_list)

    # Concatenate log_probs along batch dim
    full_log_probs = torch.cat(all_log_probs, dim=1)  # [18, total_B, 20]

    avg_loss = total_loss / num_batches
    char_acc = character_accuracy(full_log_probs, all_target_lists)

    # Exact match rate
    decoded = decode_ctc(full_log_probs)
    exact_matches = 0
    total = len(decoded)
    for pred_str, gt_tensor in zip(decoded, all_target_lists):
        gt_str = "".join(CHARS[i] for i in gt_tensor.tolist())
        if pred_str == gt_str:
            exact_matches += 1
    exact_match_rate = exact_matches / total if total > 0 else 0.0

    return avg_loss, char_acc, exact_match_rate


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train LPRNet on synthetic OCR dataset + export ONNX.",
    )
    parser.add_argument(
        "--data-dir",
        default="./dataset",
        help="Dataset root with train/ and test/ subdirectories (default: ./dataset)",
    )
    parser.add_argument(
        "--epochs",
        type=int,
        default=100,
        help="Maximum number of epochs (default: 100)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=128,
        help="Batch size (default: 128)",
    )
    parser.add_argument(
        "--lr",
        type=float,
        default=0.001,
        help="Adam learning rate (default: 0.001)",
    )
    parser.add_argument(
        "--output",
        default="./lprnet.onnx",
        help="Output ONNX path (default: ./lprnet.onnx)",
    )
    parser.add_argument(
        "--patience",
        type=int,
        default=15,
        help="Early stopping patience (default: 15)",
    )
    parser.add_argument(
        "--log-dir",
        default="./runs/lprnet",
        help="TensorBoard log directory (default: ./runs/lprnet)",
    )
    parser.add_argument(
        "--device",
        default="auto",
        help='Device: "auto", "cpu", or "cuda" (default: auto)',
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> None:
    args = parse_args(argv)

    # ---- Resolve device ---------------------------------------------------
    if args.device == "auto":
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    else:
        device = torch.device(args.device)

    print(f"{'=' * 60}")
    print(f"  LPRNet — OCR Training Pipeline")
    print(f"{'=' * 60}")
    print(f"  Data dir   : {args.data_dir}")
    print(f"  Epochs     : {args.epochs}")
    print(f"  Batch size : {args.batch_size}")
    print(f"  Learning LR: {args.lr}")
    print(f"  Device     : {device}")
    print(f"  Output ONNX: {args.output}")
    print(f"{'=' * 60}")
    print()

    # ---- Data -------------------------------------------------------------
    train_dir = os.path.join(args.data_dir, "train")
    test_dir = os.path.join(args.data_dir, "test")

    print("[data] Loading training set...")
    train_dataset = OCRDataset(train_dir)
    print(f"       {len(train_dataset)} samples")

    print("[data] Loading test set...")
    test_dataset = OCRDataset(test_dir)
    print(f"       {len(test_dataset)} samples")

    train_loader = DataLoader(
        train_dataset,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=0,          # 0 for cross-platform compatibility
        collate_fn=collate_fn,
        drop_last=True,
    )
    test_loader = DataLoader(
        test_dataset,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=0,
        collate_fn=collate_fn,
        drop_last=False,
    )

    # ---- Model ------------------------------------------------------------
    print("\n[model] Creating LPRNet (20 classes: 19 active + blank)...")
    model = LPRNet(num_classes=20).to(device)

    total_params = sum(p.numel() for p in model.parameters())
    trainable_params = sum(p.numel() for p in model.parameters() if p.requires_grad)
    print(f"        Total params: {total_params:,}")
    print(f"        Trainable   : {trainable_params:,}")

    # ---- Training setup ---------------------------------------------------
    criterion = nn.CTCLoss(blank=BLANK_IDX, zero_infinity=True)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    # TensorBoard
    writer = SummaryWriter(log_dir=args.log_dir)
    print(f"\n[tensorboard] Logging to {args.log_dir}")
    print(f"  View: tensorboard --logdir {args.log_dir}")

    # ---- Training loop ----------------------------------------------------
    best_val_loss = float("inf")
    best_epoch = -1
    patience_counter = 0
    start_time = time.time()

    print(f"\n{'=' * 60}")
    print(f"  Training started")
    print(f"{'=' * 60}")

    for epoch in range(1, args.epochs + 1):
        epoch_start = time.time()

        train_loss = train_epoch(model, train_loader, criterion, optimizer, device)
        val_loss, val_char_acc, val_exact_match = validate(
            model, test_loader, criterion, device
        )

        epoch_time = time.time() - epoch_start

        writer.add_scalar("Loss/train", train_loss, epoch)
        writer.add_scalar("Loss/val", val_loss, epoch)
        writer.add_scalar("Accuracy/char", val_char_acc, epoch)
        writer.add_scalar("Accuracy/exact_match", val_exact_match, epoch)
        writer.add_scalar("LR", args.lr, epoch)

        # Print progress
        log_line = (
            f"  Epoch {epoch:3d}/{args.epochs}  "
            f"train_loss={train_loss:.4f}  "
            f"val_loss={val_loss:.4f}  "
            f"char_acc={val_char_acc:.4f}  "
            f"exact_match={val_exact_match:.4f}  "
            f"time={epoch_time:.1f}s"
        )
        print(log_line)

        # Early stopping (based on validation loss)
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0
        else:
            patience_counter += 1
            if patience_counter >= args.patience:
                print(f"\n  Early stopping triggered at epoch {epoch} "
                      f"(best epoch {best_epoch}, best val_loss={best_val_loss:.4f})")
                break

    total_time = time.time() - start_time
    print(f"\n{'=' * 60}")
    print(f"  Training complete in {total_time:.1f}s ({total_time/60:.1f} min)")
    print(f"  Best epoch   : {best_epoch}")
    print(f"  Best val_loss: {best_val_loss:.4f}")
    print(f"{'=' * 60}")

    # ---- ONNX export ------------------------------------------------------
    print(f"\n[export] Exporting ONNX to {args.output} ...")
    export_onnx(model, args.output)

    # ---- Save PyTorch checkpoint for resuming -----------------------------
    ckpt_path = os.path.splitext(args.output)[0] + "_last.pt"
    torch.save(
        {
            "epoch": epoch,
            "model_state_dict": model.state_dict(),
            "optimizer_state_dict": optimizer.state_dict(),
            "best_val_loss": best_val_loss,
        },
        ckpt_path,
    )
    print(f"[checkpoint] Saved to {ckpt_path}")

    # ---- Final accuracy ---------------------------------------------------
    print("\n[final] Running final validation...")
    final_loss, final_char_acc, final_exact_match = validate(
        model, test_loader, criterion, device
    )
    print(f"  Final val_loss     : {final_loss:.4f}")
    print(f"  Final char_acc     : {final_char_acc:.4f}")
    print(f"  Final exact_match  : {final_exact_match:.4f}")

    # Show a few sample decodes
    print("\n[decode] Sample decodings (first 10 test samples):")
    sample_log_probs, _, _, _, sample_targets = next(iter(test_loader))
    sample_log_probs = model(sample_log_probs.to(device)).cpu()
    decoded = decode_ctc(sample_log_probs)
    for i, (pred, gt_tensor) in enumerate(zip(decoded, sample_targets)):
        gt_str = "".join(CHARS[j] for j in gt_tensor.tolist())
        ok = "✓" if pred == gt_str else "✗"
        print(f"    [{i}] pred={pred!r:>20s}  gt={gt_str!r:>20s}  {ok}")
        if i >= 9:
            break

    writer.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
