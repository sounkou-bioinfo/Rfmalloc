#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert an official fair-esm ESM-2 checkpoint to an F32 GGUF."""

import argparse
from pathlib import Path

import esm
import gguf


def convert(model_name: str, output: Path) -> None:
    try:
        loader = getattr(esm.pretrained, model_name)
    except AttributeError as exc:
        raise SystemExit(f"fair-esm has no pretrained model {model_name!r}") from exc

    model, alphabet = loader()
    model.eval()
    if not model_name.startswith("esm2_"):
        raise SystemExit("this converter accepts ESM-2 checkpoints only")

    first_layer = model.layers[0]
    writer = gguf.GGUFWriter(output, "esm2")
    writer.add_uint32("esm2.block_count", model.num_layers)
    writer.add_uint32("esm2.embedding_length", model.embed_dim)
    writer.add_uint32(
        "esm2.feed_forward_length", first_layer.fc1.out_features
    )
    writer.add_uint32("esm2.attention.head_count", model.attention_heads)
    writer.add_float32(
        "esm2.attention.layer_norm_epsilon",
        float(model.emb_layer_norm_after.eps),
    )
    writer.add_uint32("esm2.mask_token_id", alphabet.mask_idx)
    writer.add_uint32("esm2.padding_token_id", alphabet.padding_idx)
    writer.add_uint32("esm2.bos_token_id", alphabet.cls_idx)
    writer.add_uint32("esm2.eos_token_id", alphabet.eos_idx)
    writer.add_float32("esm2.token_dropout.training_ratio", 0.15 * 0.8)

    writer.add_tokenizer_model("esm")
    writer.add_token_list(alphabet.all_toks)
    writer.add_bos_token_id(alphabet.cls_idx)
    writer.add_eos_token_id(alphabet.eos_idx)
    writer.add_pad_token_id(alphabet.padding_idx)
    writer.add_mask_token_id(alphabet.mask_idx)

    for name, tensor in model.state_dict().items():
        if name == "lm_head.weight" or name.endswith(".rot_emb.inv_freq"):
            continue
        writer.add_tensor(name, tensor.detach().cpu().numpy())

    writer.write_header_to_file()
    writer.write_kv_data_to_file()
    writer.write_tensors_to_file()
    writer.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--model",
        default="esm2_t6_8M_UR50D",
        help="fair-esm pretrained loader name",
    )
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    convert(args.model, args.output)


if __name__ == "__main__":
    main()
