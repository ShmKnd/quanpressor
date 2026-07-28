#!/usr/bin/env python3

import csv
import math
import sys
from pathlib import Path


MODES = ("FET", "OPTO", "VCA", "VARIMU")
COLORS = {
    "FET": "#d55e00",
    "OPTO": "#cc79a7",
    "VCA": "#0072b2",
    "VARIMU": "#009e73",
}


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    for row in rows:
        row["sample_rate"] = float(row["sample_rate"])
        row["x"] = float(row["x"])
        row["value"] = float(row["value"])
        row["valid"] = row["valid"] == "1"
    return rows


def select(rows, mode, measurement, quality="native"):
    selected = [
        row for row in rows
        if row["mode"] == mode
        and row["measurement"] == measurement
        and row["sample_rate"] == 48000.0
        and row["topology"] == "stereo_linked"
        and row["quality"] == quality
        and row["valid"]
    ]
    return sorted(selected, key=lambda row: row["x"])


def esc(text):
    return (str(text).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


class Svg:
    def __init__(self, width, height, title):
        self.width = width
        self.height = height
        self.items = [
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
            "<style>"
            "text{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;"
            "fill:#252525}.title{font-size:24px;font-weight:700}.label{font-size:13px}"
            ".small{font-size:11px}.grid{stroke:#d8d8d8;stroke-width:1}"
            ".axis{stroke:#555;stroke-width:1.25}.panel{fill:#fff;stroke:#aaa}"
            "</style>",
            '<rect width="100%" height="100%" fill="#f7f7f5"/>',
            f'<text x="40" y="38" class="title">{esc(title)}</text>',
        ]

    def add(self, text):
        self.items.append(text)

    def save(self, path):
        self.items.append("</svg>")
        path.write_text("\n".join(self.items) + "\n", encoding="utf-8")


def map_value(value, low, high, start, end):
    if high == low:
        return start
    return start + (value - low) * (end - start) / (high - low)


def line_path(points):
    if not points:
        return ""
    return " ".join(
        ("M" if index == 0 else "L") + f"{x:.2f},{y:.2f}"
        for index, (x, y) in enumerate(points)
    )


def draw_axes(svg, x, y, width, height, x_range, y_range,
              x_ticks, y_ticks, x_label, y_label):
    svg.add(
        f'<rect class="panel" x="{x}" y="{y}" width="{width}" height="{height}"/>'
    )
    for tick in x_ticks:
        px = map_value(tick, x_range[0], x_range[1], x, x + width)
        svg.add(f'<line class="grid" x1="{px:.2f}" y1="{y}" '
                f'x2="{px:.2f}" y2="{y + height}"/>')
        svg.add(f'<text class="small" text-anchor="middle" x="{px:.2f}" '
                f'y="{y + height + 17}">{tick:g}</text>')
    for tick in y_ticks:
        py = map_value(tick, y_range[0], y_range[1], y + height, y)
        svg.add(f'<line class="grid" x1="{x}" y1="{py:.2f}" '
                f'x2="{x + width}" y2="{py:.2f}"/>')
        svg.add(f'<text class="small" text-anchor="end" x="{x - 7}" '
                f'y="{py + 4:.2f}">{tick:g}</text>')
    svg.add(f'<line class="axis" x1="{x}" y1="{y + height}" '
            f'x2="{x + width}" y2="{y + height}"/>')
    svg.add(f'<line class="axis" x1="{x}" y1="{y}" x2="{x}" y2="{y + height}"/>')
    svg.add(f'<text class="label" text-anchor="middle" x="{x + width / 2}" '
            f'y="{y + height + 38}">{esc(x_label)}</text>')
    svg.add(f'<text class="label" text-anchor="middle" '
            f'transform="translate({x - 42},{y + height / 2}) rotate(-90)">'
            f'{esc(y_label)}</text>')


def plot_static(before, after, output_path):
    svg = Svg(1160, 900, "Quanpressor static curves: baseline vs revised")
    for index, mode in enumerate(MODES):
        column = index % 2
        row = index // 2
        x = 85 + column * 565
        y = 85 + row * 395
        width = 485
        height = 300
        draw_axes(svg, x, y, width, height, (-60, 0), (-60, 0),
                  (-60, -48, -36, -24, -12, 0),
                  (-60, -48, -36, -24, -12, 0),
                  "Input peak (dBFS)", "Output peak (dBFS)")
        svg.add(f'<text class="label" x="{x + 8}" y="{y + 20}" '
                f'font-weight="700">{mode}</text>')
        identity = [
            (map_value(value, -60, 0, x, x + width),
             map_value(value, -60, 0, y + height, y))
            for value in (-60, 0)
        ]
        svg.add(f'<path d="{line_path(identity)}" fill="none" '
                f'stroke="#aaa" stroke-dasharray="3 4"/>')
        for rows, stroke, dash in (
            (select(before, mode, "static_output"), "#666", "7 5"),
            (select(after, mode, "static_output"), COLORS[mode], ""),
        ):
            points = [
                (map_value(item["x"], -60, 0, x, x + width),
                 map_value(item["value"], -60, 0, y + height, y))
                for item in rows
            ]
            svg.add(f'<path d="{line_path(points)}" fill="none" '
                    f'stroke="{stroke}" stroke-width="2.2" '
                    f'stroke-dasharray="{dash}"/>')
    svg.add('<line x1="810" y1="45" x2="850" y2="45" stroke="#666" '
            'stroke-width="2.2" stroke-dasharray="7 5"/>')
    svg.add('<text class="label" x="858" y="50">baseline 6396dcb</text>')
    svg.add('<line x1="970" y1="45" x2="1010" y2="45" stroke="#0072b2" '
            'stroke-width="2.2"/>')
    svg.add('<text class="label" x="1018" y="50">revised</text>')
    svg.save(output_path)


def thd_db(percent):
    return 20.0 * math.log10(max(percent / 100.0, 1.0e-12))


def plot_thd(before, after, output_path):
    svg = Svg(1160, 900, "Quanpressor THD vs Drive: baseline vs revised")
    for index, mode in enumerate(MODES):
        column = index % 2
        row = index // 2
        x = 85 + column * 565
        y = 85 + row * 395
        width = 485
        height = 300
        draw_axes(svg, x, y, width, height, (0, 18), (-120, -10),
                  (0, 3, 6, 9, 12, 15, 18),
                  (-120, -100, -80, -60, -40, -20),
                  "Drive (dB)", "THD relative to fundamental (dB)")
        svg.add(f'<text class="label" x="{x + 8}" y="{y + 20}" '
                f'font-weight="700">{mode}</text>')
        for rows, stroke, dash in (
            (select(before, mode, "thd"), "#666", "7 5"),
            (select(after, mode, "thd"), COLORS[mode], ""),
        ):
            points = [
                (map_value(item["x"], 0, 18, x, x + width),
                 map_value(thd_db(item["value"]), -120, -10, y + height, y))
                for item in rows
            ]
            svg.add(f'<path d="{line_path(points)}" fill="none" '
                    f'stroke="{stroke}" stroke-width="2.2" '
                    f'stroke-dasharray="{dash}"/>')
    svg.save(output_path)


def metric_value(rows, mode, measurement):
    values = select(rows, mode, measurement)
    return values[0]["value"] if values else float("nan")


def metric_value_quality(rows, mode, measurement, quality):
    values = select(rows, mode, measurement, quality)
    return values[0]["value"] if values else float("nan")


def plot_timing_alias(before, after, output_path):
    svg = Svg(1160, 760, "Quanpressor timing and alias measurements at 48 kHz")
    charts = (
        ("attack_t63", "Attack t63 (ms, logarithmic)", 0.01, 100.0),
        ("release_t63", "Release t63 (ms, logarithmic)", 10.0, 4000.0),
        ("alias_energy", "10 kHz alias energy (dBc)", -100.0, 0.0),
    )
    for chart_index, (metric, title, low, high) in enumerate(charts):
        x = 90
        y = 90 + chart_index * 215
        width = 1010
        height = 135
        svg.add(f'<rect class="panel" x="{x}" y="{y}" '
                f'width="{width}" height="{height}"/>')
        svg.add(f'<text class="label" x="{x}" y="{y - 12}" '
                f'font-weight="700">{esc(title)}</text>')
        for mode_index, mode in enumerate(MODES):
            center = x + 100 + mode_index * 245
            values = (
                (metric_value(before, mode, metric), "#777", "baseline"),
                (metric_value(after, mode, metric), COLORS[mode], "revised"),
            )
            for value_index, (value, color, _) in enumerate(values):
                if metric == "alias_energy":
                    position = map_value(value, low, high, y + height, y)
                    base = y + height
                else:
                    log_low = math.log10(low)
                    log_high = math.log10(high)
                    position = map_value(math.log10(max(value, low)),
                                         log_low, log_high, y + height, y)
                    base = y + height
                bar_x = center - 32 + value_index * 38
                svg.add(f'<rect x="{bar_x}" y="{position:.2f}" width="30" '
                        f'height="{max(1.0, base - position):.2f}" '
                        f'fill="{color}" opacity="0.88"/>')
                svg.add(f'<text class="small" text-anchor="middle" '
                        f'x="{bar_x + 15}" y="{max(y + 11, position - 4):.2f}">'
                        f'{value:.2f}</text>')
            svg.add(f'<text class="label" text-anchor="middle" x="{center}" '
                    f'y="{y + height + 20}">{mode}</text>')
    svg.add('<rect x="820" y="28" width="18" height="12" fill="#777"/>')
    svg.add('<text class="label" x="845" y="39">baseline</text>')
    svg.add('<rect x="930" y="28" width="18" height="12" fill="#0072b2"/>')
    svg.add('<text class="label" x="955" y="39">revised</text>')
    svg.save(output_path)


def plot_phase6_alias(before, after, output_path):
    svg = Svg(1160, 650, "Phase 6 oversampling: 10 kHz folded alias energy")
    for quality_index, quality in enumerate(("2x", "4x")):
        x = 90
        y = 95 + quality_index * 260
        width = 1010
        height = 165
        svg.add(f'<rect class="panel" x="{x}" y="{y}" '
                f'width="{width}" height="{height}"/>')
        svg.add(f'<text class="label" x="{x}" y="{y - 12}" '
                f'font-weight="700">{quality} quality (lower is better)</text>')
        for tick in (-100, -80, -60, -40, -20, 0):
            py = map_value(tick, -100, 0, y + height, y)
            svg.add(f'<line class="grid" x1="{x}" y1="{py:.2f}" '
                    f'x2="{x + width}" y2="{py:.2f}"/>')
            svg.add(f'<text class="small" text-anchor="end" x="{x - 7}" '
                    f'y="{py + 4:.2f}">{tick}</text>')
        for mode_index, mode in enumerate(MODES):
            center = x + 115 + mode_index * 245
            values = (
                (metric_value_quality(
                    before, mode, "alias_energy", quality), "#777"),
                (metric_value_quality(
                    after, mode, "alias_energy", quality), COLORS[mode]),
            )
            for value_index, (value, color) in enumerate(values):
                position = map_value(value, -100, 0, y + height, y)
                bar_x = center - 34 + value_index * 40
                svg.add(f'<rect x="{bar_x}" y="{position:.2f}" width="32" '
                        f'height="{max(1.0, y + height - position):.2f}" '
                        f'fill="{color}" opacity="0.88"/>')
                svg.add(f'<text class="small" text-anchor="middle" '
                        f'x="{bar_x + 16}" '
                        f'y="{max(y + 11, position - 4):.2f}">'
                        f'{value:.2f}</text>')
            svg.add(f'<text class="label" text-anchor="middle" x="{center}" '
                    f'y="{y + height + 20}">{mode}</text>')
    svg.add('<rect x="800" y="30" width="18" height="12" fill="#777"/>')
    svg.add('<text class="label" x="825" y="41">pre-Phase 6</text>')
    svg.add('<rect x="930" y="30" width="18" height="12" fill="#0072b2"/>')
    svg.add('<text class="label" x="955" y="41">Phase 6</text>')
    svg.save(output_path)


def main():
    if len(sys.argv) not in (4, 5):
        print("Usage: plot_measurements.py <baseline.csv> <after.csv> "
              "<output-dir> [pre-phase6.csv]", file=sys.stderr)
        return 2
    baseline_path = Path(sys.argv[1])
    after_path = Path(sys.argv[2])
    output_dir = Path(sys.argv[3])
    output_dir.mkdir(parents=True, exist_ok=True)
    before = read_rows(baseline_path)
    after = read_rows(after_path)
    plot_static(before, after, output_dir / "static-curves.svg")
    plot_thd(before, after, output_dir / "thd-drive.svg")
    plot_timing_alias(before, after, output_dir / "timing-alias.svg")
    if len(sys.argv) == 5:
        pre_phase6 = read_rows(Path(sys.argv[4]))
        plot_phase6_alias(
            pre_phase6, after, output_dir / "phase6-alias.svg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
