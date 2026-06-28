#!/usr/bin/env python3
"""
generate_integrations.py

Baca descriptors/gateways/*.json dan hasilkan registry C read-only (deterministik).
Jalankan dari root repo: python scripts/generate_integrations.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GATEWAYS_DIR = ROOT / "descriptors" / "gateways"
OUT_C = ROOT / "generated" / "agnc_integrations_gen.c"
OUT_H = ROOT / "generated" / "agnc_integrations_gen.h"

TRANSPORT_MAP = {
    "openai-compatible": "AGNC_TRANSPORT_OPENAI_COMPATIBLE",
    "gemini-native": "AGNC_TRANSPORT_GEMINI_NATIVE",
    "local": "AGNC_TRANSPORT_LOCAL",
    "opencode-native": "AGNC_TRANSPORT_OPENCODE_NATIVE",
    "anthropic-native": "AGNC_TRANSPORT_ANTHROPIC_NATIVE",
}


def c_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def load_gateways() -> list[dict]:
    paths = sorted(GATEWAYS_DIR.glob("*.json"))
    if not paths:
        raise SystemExit(f"Tidak ada descriptor di {GATEWAYS_DIR}")
    gateways = []
    for path in paths:
        with path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        data["_source"] = path.name
        gateways.append(data)
  # urutkan id agar output deterministik
    gateways.sort(key=lambda item: item["id"])
    return gateways


def emit_header(gateways: list[dict]) -> str:
    lines = [
        "/* File ini dihasilkan oleh scripts/generate_integrations.py — jangan edit manual. */",
        "#ifndef AGNC_INTEGRATIONS_GEN_H",
        "#define AGNC_INTEGRATIONS_GEN_H",
        "",
        '#include "agnc/provider.h"',
        "",
        "/* Registry gateway terurut; dipakai agnc_registry_find_gateway(). */",
        f"#define AGNC_GATEWAY_COUNT {len(gateways)}",
        "",
        "const agnc_gateway_descriptor_t *agnc_registry_gateway_at(size_t index);",
        "",
        "#endif /* AGNC_INTEGRATIONS_GEN_H */",
        "",
    ]
    return "\n".join(lines)


def emit_source(gateways: list[dict]) -> str:
    lines: list[str] = [
        "/* File ini dihasilkan oleh scripts/generate_integrations.py — jangan edit manual. */",
        '#include "agnc_integrations_gen.h"',
        "",
    ]

    gateway_symbols: list[str] = []

    for gw in gateways:
        gid = gw["id"].replace("-", "_")
        transport = gw["transport"]["kind"]
        transport_enum = TRANSPORT_MAP.get(transport)
        if transport_enum is None:
            raise SystemExit(f"Transport tidak dikenal '{transport}' di {gw['_source']}")

        models = gw.get("catalog", {}).get("models", []) or []
        model_symbol = f"agnc_models_{gid}"
        if models:
            lines.append(f"static const agnc_model_descriptor_t {model_symbol}[] = {{")
            for model in models:
                caps = model.get("capabilities", {})
                lines.append(
                    "    {"
                    f'"{c_escape(model["id"])}", '
                    f'"{c_escape(model["api_name"])}", '
                    f"{1 if caps.get('streaming', True) else 0}, "
                    f"{1 if caps.get('tool_calls', True) else 0}, "
                    f"{1 if caps.get('reasoning', False) else 0}"
                    "},"
                )
            lines.append("};")
            lines.append("")
            models_ptr = model_symbol
            model_count = str(len(models))
        else:
            models_ptr = "NULL"
            model_count = "0"

        auth = gw["transport"]["auth_header"]
        env_vars = gw.get("setup", {}).get("credential_env_vars", [])
        requires_auth = 1 if gw.get("setup", {}).get("requires_auth", True) else 0
        env_symbol = f"agnc_env_{gid}"
        if env_vars:
            lines.append(f'static const char *const {env_symbol}[] = {{')
            for env_name in env_vars:
                lines.append(f'    "{c_escape(env_name)}",')
            lines.append("    NULL")
            lines.append("};")
            lines.append("")
            env_ptr = env_symbol
            env_count = str(len(env_vars))
        else:
            env_ptr = "NULL"
            env_count = "0"

        gw_symbol = f"agnc_gateway_{gid}"
        gateway_symbols.append(gw_symbol)
        models_path = gw["transport"].get("models_endpoint_path", "/models")
        lines.extend(
            [
                f"static const agnc_gateway_descriptor_t {gw_symbol} = {{",
                f'    "{c_escape(gw["id"])}",',
                f'    "{c_escape(gw["label"])}",',
                f'    "{c_escape(gw["default_base_url"])}",',
                f'    "{c_escape(gw["default_model"])}",',
                f"    {transport_enum},",
                f'    "{c_escape(auth["name"])}",',
                f'    "{c_escape(auth["scheme"])}",',
                f'    "{c_escape(gw["transport"]["endpoint_path"])}",',
                f'    "{c_escape(models_path)}",',
                f"    {1 if gw['transport'].get('supports_streaming', True) else 0},",
                f"    {1 if gw['transport'].get('supports_tool_calls', True) else 0},",
                f"    {requires_auth},",
                f"    {env_ptr},",
                f"    {env_count},",
                f"    {models_ptr},",
                f"    {model_count},",
                "};",
                "",
            ]
        )

    lines.append("static const agnc_gateway_descriptor_t *const agnc_gateway_table[] = {")
    for symbol in gateway_symbols:
        lines.append(f"    &{symbol},")
    lines.append("};")
    lines.append("")

    lines.extend(
        [
            "const agnc_gateway_descriptor_t *agnc_registry_gateway_at(size_t index)",
            "{",
            "    if (index >= AGNC_GATEWAY_COUNT) {",
            "        return NULL;",
            "    }",
            "    return agnc_gateway_table[index];",
            "}",
            "",
        ]
    )

    return "\n".join(lines)


def main() -> int:
    gateways = load_gateways()
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(emit_header(gateways), encoding="utf-8", newline="\n")
    OUT_C.write_text(emit_source(gateways), encoding="utf-8", newline="\n")
    print(f"Generated {len(gateways)} gateways -> {OUT_C.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
